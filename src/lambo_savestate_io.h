// Asynchronous publisher for developer save-state images.
//
// The game thread owns guest RAM, so it builds the complete on-disk image
// (header + RDRAM + race-field extension) at the frame boundary and hands that
// owned buffer to this module. The worker performs the filesystem work
// (temporary file, flush, atomic rename) without stalling the frame.
//
// Lifecycle: the worker starts on the first submit and is joined by an atexit
// handler. Paths that bypass atexit with _Exit() must call
// lambo_savestate_flush() first, like the Controller Pak exit paths do.

#ifndef LAMBO_SAVESTATE_IO_H
#define LAMBO_SAVESTATE_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Takes ownership of data (allocated with malloc) and publishes it to path
// atomically. Returns 0 when accepted, -1 when the job was rejected (data is
// still released). A newer job replaces an unstarted older one so a burst of
// save requests cannot grow memory without bound.
int lambo_savestate_io_submit(const char* path, uint8_t* data, size_t size);

// Wait for all accepted jobs to be published. Used by explicit exit paths and
// by tests that need the completed file before continuing.
void lambo_savestate_io_flush(void);

// Flush, stop, and join the worker. The next submit starts a fresh worker.
void lambo_savestate_io_shutdown(void);

// Monotonic microseconds; the C snapshot path uses it for debug timing.
uint64_t lambo_savestate_io_now_us(void);

#if defined(LAMBO_SAVESTATE_IO_TESTING)
// Test seam: runs on the writer thread immediately before touching the file.
typedef void (*LamboSavestateBeforeWriteHook)(void);
void lambo_savestate_io_set_before_write_hook(LamboSavestateBeforeWriteHook hook);
#endif

#ifdef __cplusplus
}
#endif

#endif // LAMBO_SAVESTATE_IO_H
