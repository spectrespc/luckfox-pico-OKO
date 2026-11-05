#pragma once

#include <stddef.h>

/* Directory and per-file quota.  The HTTP request is buffered in RAM
 * (HTTP_MAX_BUF_SIZE == 4 MiB), so a larger upload would be closed without a
 * reply; the API refuses it explicitly instead. */
#define MODELS_DIR "/userdata/models"
#define MODELS_QUOTA (3 * 1024 * 1024)

/* Creates MODELS_DIR when it is missing. */
void models_start(void);

/* JSON document for GET /api/models. */
size_t models_list_json(char *out, size_t size);

/* Stores an uploaded *.rknn atomically (*.part -> fsync -> rename). */
int models_upload(const char *filename, const char *data, size_t length,
    char *error, size_t error_size);

/* Loads the model into the running detector and persists yolo.model. */
int models_activate(const char *name, char *error, size_t error_size);

/* Removes a model; the active one is refused. */
int models_delete(const char *name, char *error, size_t error_size);

/* Cheap SHA-256 for the models.json integrity field. */
void models_sha256(const void *data, size_t length, char out[65]);