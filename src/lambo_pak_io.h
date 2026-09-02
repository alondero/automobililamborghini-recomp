#ifndef LAMBO_PAK_IO_H
#define LAMBO_PAK_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAMBO_PAK_SIZE 0x8000u

typedef enum LamboPakFormat {
    LAMBO_PAK_FORMAT_RAW,
    LAMBO_PAK_FORMAT_FOUR_PORT,
    LAMBO_PAK_FORMAT_RETROARCH,
    LAMBO_PAK_FORMAT_DEXDRIVE
} LamboPakFormat;

typedef struct LamboPakIoResult {
    int ok;
    LamboPakFormat format;
    size_t container_size;
    size_t pak_offset;
    char error[192];
} LamboPakIoResult;

LamboPakIoResult lambo_pak_probe_file(const char* path);

LamboPakIoResult lambo_pak_read_file(const char* path, uint8_t image[LAMBO_PAK_SIZE]);

/*
 * Atomically update controller 1's Pak. If path already contains a recognised
 * container, bytes outside the Pak are retained; a new path is created as raw MPK.
 */
LamboPakIoResult lambo_pak_write_file(const char* path, const uint8_t image[LAMBO_PAK_SIZE]);

/* The source is never modified; source and destination must be different files. */
LamboPakIoResult lambo_pak_import_file(const char* source_path, const char* destination_path);

const char* lambo_pak_format_name(LamboPakFormat format);

void lambo_pak_set_default_path(const char* path);

/* Explicit CLI selection takes precedence over LAMBO_CONTROLLER_PAK_FILE. */
void lambo_pak_override_path(const char* path);

#ifdef __cplusplus
}
#endif

#endif
