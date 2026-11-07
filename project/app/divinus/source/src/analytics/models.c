#if defined(__ARM_PCS_VFP)

#include "models.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/macros.h"
#include "yolo.h"

#define MODELS_MAX 16

typedef struct {
    char name[96];
    unsigned long long size;
    char sha[65];
    int classes;
    int width;
    int height;
    int runtime;        /* 0 = unknown, 1 = RKNN 1.x, 2 = RKNN 2.x */
} model_meta;

/* yolo.model_dir may be changed in the config; the compiled default is used
 * when the key is absent. */
static const char *models_dir(void) {
    return app_config.yolo_model_dir[0] ? app_config.yolo_model_dir :
        MODELS_DIR;
}

static void models_json_path(char *out, size_t size, const char *suffix) {
    snprintf(out, size, "%s/models.json%s", models_dir(), suffix);
}

static model_meta metadata[MODELS_MAX];
static int metadataCount;

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-4), used for the integrity field of models.json.  */

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} sha256_ctx;

static uint32_t sha256_rotr(uint32_t value, int count)
{
    return (value >> count) | (value << (32 - count));
}

static void sha256_transform(sha256_ctx *ctx, const unsigned char *data)
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
        0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
        0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
        0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)data[i * 4] << 24) |
            ((uint32_t)data[i * 4 + 1] << 16) |
            ((uint32_t)data[i * 4 + 2] << 8) | data[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^
            (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^
            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bits = 0;
    ctx->used = 0;
}

static void sha256_update(sha256_ctx *ctx, const void *data, size_t length)
{
    const unsigned char *input = data;

    ctx->bits += (uint64_t)length * 8;
    while (length) {
        size_t space = 64 - ctx->used;
        size_t take = length < space ? length : space;
        memcpy(ctx->block + ctx->used, input, take);
        ctx->used += take;
        input += take;
        length -= take;
        if (ctx->used == 64) {
            sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, unsigned char digest[32])
{
    uint64_t bits = ctx->bits;
    unsigned char pad = 0x80;
    unsigned char zero = 0;

    sha256_update(ctx, &pad, 1);
    while (ctx->used != 56)
        sha256_update(ctx, &zero, 1);
    for (int i = 7; i >= 0; i--) {
        unsigned char byte = (unsigned char)(bits >> (i * 8));
        sha256_update(ctx, &byte, 1);
    }

    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (unsigned char)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)ctx->state[i];
    }
}

void models_sha256(const void *data, size_t length, char out[65])
{
    sha256_ctx ctx;
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";

    sha256_init(&ctx);
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
}

/* ------------------------------------------------------------------ */

static const char *models_active_name(void)
{
    const char *base = strrchr(app_config.yolo_model, '/');
    return base ? base + 1 : app_config.yolo_model;
}

static int models_name_ok(const char *name)
{
    size_t length;

    if (!name || !*name) return 0;
    length = strlen(name);
    if (length < 6 || length >= sizeof(metadata[0].name)) return 0;
    if (strcmp(name + length - 5, ".rknn")) return 0;
    if (strstr(name, "..")) return 0;
    for (const char *p = name; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')
            continue;
        return 0;
    }
    return 1;
}

static model_meta *models_find(const char *name)
{
    for (int i = 0; i < metadataCount; i++)
        if (!strcmp(metadata[i].name, name)) return &metadata[i];
    return NULL;
}

static void models_json_save(void);

static model_meta *models_slot(const char *name)
{
    model_meta *entry = models_find(name);

    if (entry) return entry;
    if (metadataCount >= MODELS_MAX) return NULL;

    entry = &metadata[metadataCount++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    return entry;
}

/* Minimal parser for the document written below. */
static void models_json_load(void)
{
    char jsonPath[256];
    FILE *file;
    char *text;
    long length;

    models_json_path(jsonPath, sizeof(jsonPath), "");
    file = fopen(jsonPath, "r");

    metadataCount = 0;
    if (!file) return;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        length > 64 * 1024) {
        fclose(file);
        return;
    }
    rewind(file);
    text = malloc((size_t)length + 1);
    if (!text) {
        fclose(file);
        return;
    }
    if (fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        return;
    }
    text[length] = 0;
    fclose(file);

    for (char *cursor = text; (cursor = strstr(cursor, "{\"name\":\"")); ) {
        model_meta *entry;
        char name[96] = {0};
        char hash[65] = {0};
        unsigned long long size = 0;
        int classes = 0, width = 0, height = 0, runtime = 0;
        char *end;

        cursor += strlen("{\"name\":\"");
        end = strchr(cursor, '"');
        if (!end) break;
        if ((size_t)(end - cursor) >= sizeof(name)) {
            cursor = end;
            continue;
        }
        snprintf(name, sizeof(name), "%.*s", (int)(end - cursor), cursor);
        cursor = end + 1;

        if (!models_name_ok(name)) continue;
        entry = models_slot(name);
        if (!entry) break;

        if ((end = strstr(cursor, "\"size\":"))) {
            size = strtoull(end + strlen("\"size\":"), NULL, 10);
        }
        if ((end = strstr(cursor, "\"sha256\":\""))) {
            end += strlen("\"sha256\":\"");
            if (strchr(end, '"'))
                snprintf(hash, sizeof(hash), "%.*s",
                    (int)(strchr(end, '"') - end), end);
        }
        if ((end = strstr(cursor, "\"classes\":")))
            classes = (int)strtol(end + strlen("\"classes\":"), NULL, 10);
        if ((end = strstr(cursor, "\"width\":")))
            width = (int)strtol(end + strlen("\"width\":"), NULL, 10);
        if ((end = strstr(cursor, "\"height\":")))
            height = (int)strtol(end + strlen("\"height\":"), NULL, 10);
        if ((end = strstr(cursor, "\"runtime\":")))
            runtime = (int)strtol(end + strlen("\"runtime\":"), NULL, 10);

        entry->size = size;
        snprintf(entry->sha, sizeof(entry->sha), "%s", hash);
        entry->classes = classes;
        entry->width = width;
        entry->height = height;
        entry->runtime = runtime;
        cursor++;
    }

    free(text);
}

static void models_json_save(void)
{
    char jsonPath[256], partPath[256];
    FILE *file;

    models_json_path(jsonPath, sizeof(jsonPath), "");
    models_json_path(partPath, sizeof(partPath), ".part");
    file = fopen(partPath, "w");
    if (!file) return;
    fprintf(file, "{\"models\":[");
    for (int i = 0; i < metadataCount; i++) {
        model_meta *entry = &metadata[i];
        fprintf(file, "%s{\"name\":\"%s\",\"size\":%llu,\"sha256\":\"%s\","
            "\"classes\":%d,\"width\":%d,\"height\":%d,\"runtime\":%d}",
            i ? "," : "", entry->name, entry->size, entry->sha,
            entry->classes, entry->width, entry->height, entry->runtime);
    }
    fprintf(file, "]}\n");
    fflush(file);
    fsync(fileno(file));
    fclose(file);
    rename(partPath, jsonPath);
}

void models_start(void)
{
    if (mkdir(models_dir(), 0755) && errno != EEXIST)
        HAL_WARNING("models", "Cannot create %s\n", models_dir());
    models_json_load();
}

static int models_free_bytes(unsigned long long *freeBytes)
{
    struct statvfs stat;

    if (statvfs(models_dir(), &stat)) return -1;
    if (freeBytes)
        *freeBytes = (unsigned long long)stat.f_bavail * stat.f_frsize;
    return 0;
}

/* Bounds-safe append for the JSON listing: snprintf returns the untruncated
 * length, and adding that to `used` would underflow the next size argument. */
static void models_append(char *out, size_t size, size_t *used,
    const char *format, ...)
{
    size_t left = *used < size ? size - *used : 0;
    va_list args;
    int written;

    if (!left) return;
    va_start(args, format);
    written = vsnprintf(out + *used, left, format, args);
    va_end(args);
    if (written < 0) return;
    *used += (size_t)written < left ? (size_t)written : left - 1;
}

static void models_json_escape(const char *in, char *out, size_t size)
{
    size_t used = 0;

    for (; *in && used + 1 < size; in++) {
        if (*in == '"' || *in == '\\')
            out[used++] = '\\';
        out[used++] = *in;
    }
    out[used] = 0;
}

size_t models_list_json(char *out, size_t size)
{
    DIR *dir;
    struct dirent *entry;
    char names[MODELS_MAX][96];
    int count = 0;
    size_t used = 0;
    unsigned long long freeBytes = 0;
    const char *active = models_active_name();

    if (!out || !size) return 0;
    out[0] = 0;

    dir = opendir(models_dir());
    while (dir && (entry = readdir(dir)) && count < MODELS_MAX) {
        struct stat st;
        char path[256];
        size_t length = strlen(entry->d_name);

        if (entry->d_name[0] == '.' || length < 6 ||
            strcmp(entry->d_name + length - 5, ".rknn"))
            continue;
        if (!models_name_ok(entry->d_name)) continue;
        snprintf(path, sizeof(path), "%s/%s", models_dir(), entry->d_name);
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        snprintf(names[count], sizeof(names[count]), "%s", entry->d_name);
        count++;
    }
    if (dir) closedir(dir);

    /* Keep the listing stable; the directory order is arbitrary on UBIFS. */
    for (int i = 1; i < count; i++) {
        char key[96];
        int j = i;
        snprintf(key, sizeof(key), "%s", names[i]);
        while (j > 0 && strcmp(names[j - 1], key) > 0) {
            memcpy(names[j], names[j - 1], sizeof(names[j]));
            j--;
        }
        snprintf(names[j], sizeof(names[j]), "%s", key);
    }

    models_free_bytes(&freeBytes);

    models_append(out, size, &used,
        "{\"dir\":\"%s\",\"quota\":%d,\"free\":%llu,\"active\":\"",
        models_dir(), MODELS_QUOTA, freeBytes);
    {
        char escaped[192];
        models_json_escape(active, escaped, sizeof(escaped));
        models_append(out, size, &used, "%s", escaped);
    }
    models_append(out, size, &used, "\",\"models\":[");

    for (int i = 0; i < count && used + 256 < size; i++) {
        char path[256];
        char escaped[192];
        struct stat st;
        model_meta *meta = models_find(names[i]);

        snprintf(path, sizeof(path), "%s/%s", models_dir(), names[i]);
        memset(&st, 0, sizeof(st));
        stat(path, &st);
        models_json_escape(names[i], escaped, sizeof(escaped));

        models_append(out, size, &used,
            "%s{\"name\":\"%s\",\"size\":%llu,\"sha256\":\"%s\","
            "\"classes\":%d,\"width\":%d,\"height\":%d,\"runtime\":%d,"
            "\"active\":%s}",
            i ? "," : "", escaped,
            (unsigned long long)(S_ISREG(st.st_mode) ? st.st_size : 0),
            meta ? meta->sha : "", meta ? meta->classes : 0,
            meta ? meta->width : 0, meta ? meta->height : 0,
            meta ? meta->runtime : 0,
            !strcmp(names[i], active) ? "true" : "false");
    }

    models_append(out, size, &used, "]}");
    return used;
}

int models_upload(const char *filename, const char *data, size_t length,
    char *error, size_t errorSize)
{
    char path[256], part[256];
    unsigned long long freeBytes = 0;
    int fd = -1;

    memset(error, 0, errorSize);
    if (!models_name_ok(filename)) {
        snprintf(error, errorSize,
            "the file name must end with .rknn and use letters, digits, "
            "dot, underscore or dash");
        return EXIT_FAILURE;
    }
    if (!data || !length) {
        snprintf(error, errorSize, "the uploaded file is empty");
        return EXIT_FAILURE;
    }
    if (length > MODELS_QUOTA) {
        snprintf(error, errorSize,
            "the model is larger than the 3 MiB upload limit");
        return EXIT_FAILURE;
    }
    if (models_free_bytes(&freeBytes)) {
        snprintf(error, errorSize, "cannot read the free space of %s", models_dir());
        return EXIT_FAILURE;
    }
    if (freeBytes < length + 128 * 1024) {
        snprintf(error, errorSize,
            "not enough space in /userdata (%llu KiB free)", freeBytes / 1024);
        return EXIT_FAILURE;
    }

    snprintf(path, sizeof(path), "%s/%s", models_dir(), filename);
    snprintf(part, sizeof(part), "%s.part", path);

    fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(error, errorSize, "cannot create %s", part);
        return EXIT_FAILURE;
    }
    {
        size_t done = 0;
        while (done < length) {
            ssize_t written = write(fd, data + done, length - done);
            if (written < 0) {
                if (errno == EINTR) continue;
                break;
            }
            done += (size_t)written;
        }
        if (done != length || fsync(fd)) {
            snprintf(error, errorSize, "writing %s failed", part);
            close(fd);
            unlink(part);
            return EXIT_FAILURE;
        }
    }
    close(fd);

    if (rename(part, path)) {
        snprintf(error, errorSize, "cannot rename the uploaded model");
        unlink(part);
        return EXIT_FAILURE;
    }

    {
        model_meta *entry = models_slot(filename);
        if (entry) {
            entry->size = length;
            entry->runtime = 0;     /* detected on the next activation */
            models_sha256(data, length, entry->sha);
            if (strcmp(filename, models_active_name()))
                entry->classes = entry->width = entry->height = 0;
            models_json_save();
        }
    }

    HAL_INFO("models", "Stored %s (%zu bytes)\n", filename, length);
    return EXIT_SUCCESS;
}

/* A model that fails on the selected runtime may still be valid for the
 * other one; probing it turns a generic rknn_init error into an actionable
 * "set yolo.runtime=N" message. */
static void models_explain_runtime(const char *path, char *error,
    size_t errorSize)
{
    int other = app_config.yolo_runtime == 2 ? 1 : 2;
    yolo_model_info alt;
    char altError[192];

    if (yolo_model_probe_runtime(path, other, &alt, altError,
            sizeof(altError)) == 0)
        snprintf(error, errorSize,
            "model needs RKNN runtime %d (SDK %s); set yolo.runtime=%d "
            "and retry", other, alt.sdk[0] ? alt.sdk : "unknown", other);
}

int models_activate(const char *name, char *error, size_t errorSize)
{
    char path[256];
    yolo_model_info info;
    model_meta *entry;

    memset(error, 0, errorSize);
    if (!models_name_ok(name)) {
        snprintf(error, errorSize, "invalid model name");
        return EXIT_FAILURE;
    }
    snprintf(path, sizeof(path), "%s/%s", models_dir(), name);
    if (access(path, R_OK)) {
        snprintf(error, errorSize, "model %s is missing", name);
        return EXIT_FAILURE;
    }

    /* models.json records the runtime that last loaded this file; a model
     * converted by toolkit 2.x must not be handed to the 1.6.0 runtime. */
    entry = models_find(name);
    if (entry && entry->runtime &&
        entry->runtime != (int)app_config.yolo_runtime) {
        snprintf(error, errorSize,
            "model was activated with RKNN runtime %d; set yolo.runtime=%d",
            entry->runtime, entry->runtime);
        return EXIT_FAILURE;
    }

    /* While the detector runs the context is swapped under its mutex; while
     * it is disabled a probe is enough and the NPU memory is released. */
    if (yolo_running()) {
        if (yolo_model_swap(path, error, errorSize)) {
            models_explain_runtime(path, error, errorSize);
            return EXIT_FAILURE;
        }
        memset(&info, 0, sizeof(info));
        info.width = (int)app_config.yolo_input_width;
        info.height = (int)app_config.yolo_input_height;
        info.classes = (int)app_config.yolo_classes;
        info.runtime = (int)app_config.yolo_runtime;
    } else if (yolo_model_probe(path, &info, error, errorSize)) {
        models_explain_runtime(path, error, errorSize);
        return EXIT_FAILURE;
    }

    snprintf(app_config.yolo_model, sizeof(app_config.yolo_model), "%s", path);
    if (app_config_save()) {
        snprintf(error, errorSize, "the model works but the configuration "
            "could not be saved");
        return EXIT_FAILURE;
    }

    entry = models_slot(name);
    if (entry) {
        struct stat st;
        entry->width = info.width;
        entry->height = info.height;
        entry->classes = info.classes;
        entry->runtime = info.runtime ? info.runtime :
            (int)app_config.yolo_runtime;
        if (!stat(path, &st)) entry->size = (unsigned long long)st.st_size;
        models_json_save();
    }

    HAL_INFO("models", "Activated %s (%dx%d, %d classes, runtime %s)\n",
        name, info.width, info.height, info.classes,
        info.sdk[0] ? info.sdk : "unknown");
    return EXIT_SUCCESS;
}

int models_delete(const char *name, char *error, size_t errorSize)
{
    char path[256];

    memset(error, 0, errorSize);
    if (!models_name_ok(name)) {
        snprintf(error, errorSize, "invalid model name");
        return EXIT_FAILURE;
    }
    if (!strcmp(name, models_active_name())) {
        snprintf(error, errorSize,
            "the active model cannot be deleted; activate another one first");
        return EXIT_FAILURE;
    }

    snprintf(path, sizeof(path), "%s/%s", models_dir(), name);
    if (unlink(path)) {
        snprintf(error, errorSize, "cannot delete %s", name);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < metadataCount; i++) {
        if (strcmp(metadata[i].name, name)) continue;
        memmove(&metadata[i], &metadata[i + 1],
            (size_t)(metadataCount - i - 1) * sizeof(metadata[0]));
        metadataCount--;
        break;
    }
    models_json_save();
    HAL_INFO("models", "Deleted %s\n", name);
    return EXIT_SUCCESS;
}

#endif