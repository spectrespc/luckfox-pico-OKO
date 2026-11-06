.DEFAULT_GOAL := divinus

SRC = $(shell find ./ -name '*.c')
OBJ = ../divinus
GIT_REV := $(shell git rev-parse --short HEAD)$(shell git diff --quiet HEAD || echo "-mod")

# Vendor rkaiq headers are confined to hal/plus/rk_af_stats.o.  The set mirrors
# project/app/rkipc/Makefile; the rest of divinus never sees these paths.
MEDIA_INCLUDE ?=
AF_INCLUDES := -I$(MEDIA_INCLUDE)/rkaiq -I$(MEDIA_INCLUDE)/rkaiq/uAPI2 \
               -I$(MEDIA_INCLUDE)/rkaiq/algos -I$(MEDIA_INCLUDE)/rkaiq/common \
               -I$(MEDIA_INCLUDE)/rkaiq/xcore -I$(MEDIA_INCLUDE)/rkaiq/iq_parser \
               -I$(MEDIA_INCLUDE)/rkaiq/iq_parser_v2
hal/plus/rk_af_stats.o: EXTRA_CFLAGS = $(AF_INCLUDES)

# IMPORT_STR uses .incbin, so make must know about the embedded files.
server.o: ../res/index.html
onvif.o: $(wildcard ../res/onvif/*.xml)

.PHONY: clean divinus
divinus: $(OBJ)

$(OBJ): $(SRC:%.c=%.o)
	$(CC) $^ -rdynamic $(OPT) -DGIT_REV=\"$(GIT_REV)\" -o $@

%.o: %.c Makefile
	$(CC) -c $< $(OPT) $(EXTRA_CFLAGS) -I. -DGIT_REV=\"$(GIT_REV)\" -o $@

clean:
	rm -rf $(SRC:%.c=%.o) $(OBJ)
