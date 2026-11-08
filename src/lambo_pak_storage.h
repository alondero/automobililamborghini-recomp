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

/* Records the live image for the debounced worker and returns without filesystem I/O.
 * Bursts are coalesced into one full-image copy at the deadline. The caller must keep
 * the image alive until a later flush or shutdown completes. */
void lambo_pak_storage_schedule_save(const uint8_t image[LAMBO_PAK_SIZE]);

/*
 * Apply a Joybus block write to the live image. Uses the same lock as the
 * worker's debounce snapshot so the single full-image copy cannot observe a
 * half-written block. Call schedule_save() afterwards.
 */
void lambo_pak_storage_write_block(uint8_t image[LAMBO_PAK_SIZE], unsigned address,
                                   const uint8_t* data, size_t size);

/* Debug measurement: full-image copies made for save bursts since process start. */
uint64_t lambo_pak_storage_copy_count(void);

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
