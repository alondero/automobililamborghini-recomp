#ifndef LAMBO_PAK_STORAGE_H
#define LAMBO_PAK_STORAGE_H

#include "lambo_pak_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paths use UTF-8. Reconfiguration flushes and stops any previous writer. */
void lambo_pak_storage_configure(const char* path);
const char* lambo_pak_storage_path(void);
LamboPakIoResult lambo_pak_storage_load(uint8_t image[LAMBO_PAK_SIZE]);

/* Copies the image and returns without filesystem I/O. Bursts are coalesced. */
void lambo_pak_storage_schedule_save(const uint8_t image[LAMBO_PAK_SIZE]);

/* Synchronously publish pending data; application exit paths must call this. */
LamboPakIoResult lambo_pak_storage_flush(void);
void lambo_pak_storage_shutdown(void);

#if defined(LAMBO_PAK_STORAGE_TESTING)
typedef LamboPakIoResult (*LamboPakStorageWriteHook)(
    const char* path, const uint8_t image[LAMBO_PAK_SIZE]);
void lambo_pak_storage_set_write_hook(LamboPakStorageWriteHook hook);
#endif

#ifdef __cplusplus
}
#endif

#endif
