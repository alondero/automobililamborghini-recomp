#include "lambo_pak_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum {
    FOUR_PORT_SIZE = 4 * LAMBO_PAK_SIZE,
    RETROARCH_SIZE = 296960,
    RETROARCH_PAK_OFFSET = 0x800,
    DEXDRIVE_SIZE = 36928,
    DEXDRIVE_PAK_OFFSET = 0x1040
};

static const unsigned char dexdrive_magic[12] = {
    '1', '2', '3', '-', '4', '5', '6', '-', 'S', 'T', 'D', 0
};

/* Layout ground truth:
 * - Mupen64Plus-Next's libretro_memory.h stores EEPROM[0x800], then four
 *   MEMPAK_SIZE (0x8000) images, SRAM[0x8000], and FlashRAM[0x20000].
 * - MPKEdit's parser strips 0x800 from that SRM and 0x1040 from a DexDrive
 *   file after checking the "123-456-STD" signature.
 * These are container seams only; Controller Pak bytes themselves are never
 * byte-swapped by the referenced emulator/converter implementations. */

typedef struct PakContainer {
    /* load_container owns this allocation; callers free it exactly once. */
    uint8_t* data;
    size_t size;
    size_t pak_offset;
    LamboPakFormat format;
} PakContainer;

typedef struct PakFormatDescriptor {
    LamboPakFormat format;
    size_t size;
    size_t pak_offset;
    const char* name;
    int requires_dexdrive_magic;
} PakFormatDescriptor;

static const PakFormatDescriptor formats[] = {
    { LAMBO_PAK_FORMAT_RAW, LAMBO_PAK_SIZE, 0, "raw MPK", 0 },
    { LAMBO_PAK_FORMAT_FOUR_PORT, FOUR_PORT_SIZE, 0, "four-port MPK", 0 },
    { LAMBO_PAK_FORMAT_RETROARCH, RETROARCH_SIZE, RETROARCH_PAK_OFFSET, "Mupen64Plus-Next SRM", 0 },
    { LAMBO_PAK_FORMAT_DEXDRIVE, DEXDRIVE_SIZE, DEXDRIVE_PAK_OFFSET, "DexDrive N64", 1 }
};

static LamboPakIoResult result_error(const char* message, const char* path) {
    LamboPakIoResult result;
    memset(&result, 0, sizeof(result));
    if (path != NULL) snprintf(result.error, sizeof(result.error), "%s: %s", message, path);
    else snprintf(result.error, sizeof(result.error), "%s", message);
    return result;
}

static LamboPakIoResult result_errno(const char* message, const char* path, int code) {
    LamboPakIoResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.error, sizeof(result.error), "%s (errno=%d: %s): %s",
             message, code, strerror(code), path != NULL ? path : "");
    return result;
}

#if defined(_WIN32)
static LamboPakIoResult result_win32(const char* message, const char* path, DWORD code) {
    LamboPakIoResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.error, sizeof(result.error), "%s (Win32 error=%lu): %s",
             message, (unsigned long)code, path != NULL ? path : "");
    return result;
}

static wchar_t* path_to_wide(const char* path) {
    int count;
    wchar_t* wide;
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (count == 0) {
        errno = EINVAL;
        return NULL;
    }
    wide = (wchar_t*)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count) == 0) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }
    return wide;
}

static FILE* path_open(const char* path, const wchar_t* mode) {
    wchar_t* wide = path_to_wide(path);
    FILE* file;
    if (wide == NULL) return NULL;
    file = _wfopen(wide, mode);
    free(wide);
    return file;
}

static int path_remove(const char* path) {
    wchar_t* wide = path_to_wide(path);
    int removed;
    if (wide == NULL) return -1;
    removed = _wremove(wide);
    free(wide);
    return removed;
}
#else
static FILE* path_open(const char* path, const char* mode) {
    return fopen(path, mode);
}

static int path_remove(const char* path) {
    return remove(path);
}
#endif

static LamboPakIoResult result_ok(const PakContainer* container) {
    LamboPakIoResult result;
    memset(&result, 0, sizeof(result));
    result.ok = 1;
    result.format = container->format;
    result.container_size = container->size;
    result.pak_offset = container->pak_offset;
    return result;
}

static int identify_container(PakContainer* container) {
    size_t index;
    for (index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        const PakFormatDescriptor* descriptor = &formats[index];
        if (container->size != descriptor->size) continue;
        if (descriptor->requires_dexdrive_magic &&
            memcmp(container->data, dexdrive_magic, sizeof(dexdrive_magic)) != 0) return 0;
        container->format = descriptor->format;
        container->pak_offset = descriptor->pak_offset;
        return 1;
    }
    return 0;
}

static int pak_image_is_plausible(const uint8_t image[LAMBO_PAK_SIZE]) {
    static const size_t id_offsets[] = { 0x20, 0x60, 0x80, 0xc0 };
    size_t area;
    for (area = 0; area < sizeof(id_offsets) / sizeof(id_offsets[0]); ++area) {
        const size_t offset = id_offsets[area];
        uint16_t sum = 0;
        uint16_t inverted;
        uint16_t stored_sum;
        uint16_t stored_inverted;
        size_t word;
        for (word = 0; word < 14; ++word) {
            const size_t pos = offset + word * 2;
            sum = (uint16_t)(sum + (uint16_t)((image[pos] << 8) | image[pos + 1]));
        }
        inverted = (uint16_t)(0xfff2u - sum);
        stored_sum = (uint16_t)((image[offset + 0x1c] << 8) | image[offset + 0x1d]);
        stored_inverted = (uint16_t)((image[offset + 0x1e] << 8) | image[offset + 0x1f]);
        /* DexDrive software is known to toggle bit 0x000c in the inverted checksum. */
        if (stored_sum == sum &&
            (stored_inverted == inverted || (uint16_t)(stored_inverted ^ 0x000cu) == inverted))
            return 1;
    }
    return 0;
}

static LamboPakIoResult load_container(const char* path, PakContainer* container) {
    FILE* file;
    long length;
    size_t read_count;

    memset(container, 0, sizeof(*container));
    file = path_open(path,
#if defined(_WIN32)
                     L"rb"
#else
                     "rb"
#endif
    );
    if (file == NULL) return result_errno("cannot open Controller Pak file", path, errno);
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        const int code = errno != 0 ? errno : EIO;
        fclose(file);
        return result_errno("cannot determine Controller Pak file size", path, code);
    }
    container->size = (size_t)length;
    if (container->size == 0 || container->size > RETROARCH_SIZE) {
        fclose(file);
        return result_error("unsupported Controller Pak file size", path);
    }
    container->data = (uint8_t*)malloc(container->size);
    if (container->data == NULL) {
        fclose(file);
        return result_error("out of memory while reading Controller Pak", path);
    }
    read_count = fread(container->data, 1, container->size, file);
    if (fclose(file) != 0 || read_count != container->size) {
        const int code = errno != 0 ? errno : EIO;
        free(container->data);
        container->data = NULL;
        return result_errno("cannot read complete Controller Pak file", path, code);
    }
    if (!identify_container(container)) {
        free(container->data);
        container->data = NULL;
        return result_error("unsupported Controller Pak format (expected 32/128 KiB MPK, 290 KiB SRM, or DexDrive N64)", path);
    }
    if (!pak_image_is_plausible(container->data + container->pak_offset)) {
        free(container->data);
        container->data = NULL;
        return result_error("Controller Pak has no checksum-valid ID block", path);
    }
    return result_ok(container);
}

static LamboPakIoResult publish_bytes(const char* path, const uint8_t* data, size_t size,
                                      LamboPakFormat format, size_t pak_offset) {
    char* temporary;
    FILE* file;
    size_t written;
    LamboPakIoResult result;
    PakContainer view;
    const size_t path_length = strlen(path);

    temporary = (char*)malloc(path_length + 5);
    if (temporary == NULL) return result_error("out of memory while saving Controller Pak", path);
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", 5);
    file = path_open(temporary,
#if defined(_WIN32)
                     L"wb"
#else
                     "wb"
#endif
    );
    if (file == NULL) {
        result = result_errno("cannot create temporary Controller Pak file", temporary, errno);
        free(temporary);
        return result;
    }
    written = fwrite(data, 1, size, file);
    if (fclose(file) != 0 || written != size) {
        const int code = errno != 0 ? errno : EIO;
        path_remove(temporary);
        result = result_errno("cannot write complete Controller Pak file", temporary, code);
        free(temporary);
        return result;
    }
#if defined(_WIN32)
    {
        wchar_t* wide_temporary = path_to_wide(temporary);
        wchar_t* wide_path = path_to_wide(path);
        DWORD code = ERROR_SUCCESS;
        int attempt;
        int moved = 0;
    /* Unlike MSVCRT rename(), MoveFileEx replaces without first deleting the live
     * save. A failed publish therefore leaves the prior emulator container intact. */
        if (wide_temporary != NULL && wide_path != NULL) {
            for (attempt = 0; attempt < 5; ++attempt) {
                if (MoveFileExW(wide_temporary, wide_path,
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    moved = 1;
                    break;
                }
                code = GetLastError();
                if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION) break;
                Sleep((DWORD)(10 * (attempt + 1)));
            }
        } else {
            code = ERROR_NO_UNICODE_TRANSLATION;
        }
        if (!moved) {
            free(wide_temporary);
            free(wide_path);
            path_remove(temporary);
            free(temporary);
            return result_win32("cannot replace Controller Pak file", path, code);
        }
        free(wide_temporary);
        free(wide_path);
    }
#else
    if (rename(temporary, path) != 0) {
        const int code = errno;
        path_remove(temporary);
        free(temporary);
        return result_errno("cannot replace Controller Pak file", path, code);
    }
#endif
    free(temporary);
    memset(&view, 0, sizeof(view));
    view.size = size;
    view.format = format;
    view.pak_offset = pak_offset;
    result = result_ok(&view);
    return result;
}

LamboPakIoResult lambo_pak_read_file(const char* path, uint8_t image[LAMBO_PAK_SIZE]) {
    PakContainer container;
    LamboPakIoResult result = load_container(path, &container);
    if (!result.ok) return result;
    memcpy(image, container.data + container.pak_offset, LAMBO_PAK_SIZE);
    free(container.data);
    return result;
}

LamboPakIoResult lambo_pak_probe_file(const char* path) {
    PakContainer container;
    LamboPakIoResult result = load_container(path, &container);
    free(container.data);
    return result;
}

typedef enum TargetState {
    TARGET_MISSING,
    TARGET_BLANK_RAW,
    TARGET_OTHER
} TargetState;

static TargetState inspect_unrecognised_target(const char* path) {
    FILE* file = path_open(path,
#if defined(_WIN32)
                           L"rb"
#else
                           "rb"
#endif
    );
    long length;
    size_t index;
    int byte;
    if (file == NULL) return errno == ENOENT ? TARGET_MISSING : TARGET_OTHER;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return TARGET_OTHER;
    }
    if (length == 0) {
        fclose(file);
        return TARGET_BLANK_RAW;
    }
    if ((size_t)length != LAMBO_PAK_SIZE) {
        fclose(file);
        return TARGET_OTHER;
    }
    for (index = 0; index < LAMBO_PAK_SIZE; ++index) {
        byte = fgetc(file);
        if (byte != 0) {
            fclose(file);
            return TARGET_OTHER;
        }
    }
    fclose(file);
    return TARGET_BLANK_RAW;
}

LamboPakIoResult lambo_pak_write_file(const char* path, const uint8_t image[LAMBO_PAK_SIZE]) {
    PakContainer container;
    LamboPakIoResult loaded = load_container(path, &container);

    if (!loaded.ok) {
        const TargetState state = inspect_unrecognised_target(path);
        if (state == TARGET_OTHER) return loaded;
        memset(&container, 0, sizeof(container));
        container.size = LAMBO_PAK_SIZE;
        container.format = LAMBO_PAK_FORMAT_RAW;
        container.data = (uint8_t*)malloc(container.size);
        if (container.data == NULL) return result_error("out of memory while saving Controller Pak", path);
    }

    memcpy(container.data + container.pak_offset, image, LAMBO_PAK_SIZE);
    loaded = publish_bytes(path, container.data, container.size,
                           container.format, container.pak_offset);
    free(container.data);
    return loaded;
}

static int paths_refer_to_same_file(const char* first, const char* second) {
#if defined(_WIN32)
    wchar_t* wide_first = path_to_wide(first);
    wchar_t* wide_second = path_to_wide(second);
    HANDLE first_handle;
    HANDLE second_handle;
    BY_HANDLE_FILE_INFORMATION first_info;
    BY_HANDLE_FILE_INFORMATION second_info;
    int same = 0;
    if (wide_first == NULL || wide_second == NULL) {
        free(wide_first);
        free(wide_second);
        return 0;
    }
    first_handle = CreateFileW(wide_first, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    second_handle = CreateFileW(wide_second, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (first_handle != INVALID_HANDLE_VALUE && second_handle != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(first_handle, &first_info) &&
        GetFileInformationByHandle(second_handle, &second_info)) {
        same = first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
               first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
               first_info.nFileIndexLow == second_info.nFileIndexLow;
    }
    if (first_handle != INVALID_HANDLE_VALUE) CloseHandle(first_handle);
    if (second_handle != INVALID_HANDLE_VALUE) CloseHandle(second_handle);
    free(wide_first);
    free(wide_second);
    return same;
#else
    struct stat first_info;
    struct stat second_info;
    return stat(first, &first_info) == 0 && stat(second, &second_info) == 0 &&
           first_info.st_dev == second_info.st_dev && first_info.st_ino == second_info.st_ino;
#endif
}

LamboPakIoResult lambo_pak_import_file(const char* source_path, const char* destination_path) {
    uint8_t image[LAMBO_PAK_SIZE];
    LamboPakIoResult source;
    LamboPakIoResult written;
    if (paths_refer_to_same_file(source_path, destination_path))
        return result_error("import source and destination must be different", source_path);
    source = lambo_pak_read_file(source_path, image);
    if (!source.ok) return source;

    /* Imports intentionally produce raw MPK. Do not let an existing destination's
     * container format affect the result, and never modify the source file. */
    written = publish_bytes(destination_path, image, LAMBO_PAK_SIZE,
                            LAMBO_PAK_FORMAT_RAW, 0);
    if (written.ok) {
        written.format = source.format;
        written.container_size = source.container_size;
        written.pak_offset = source.pak_offset;
    }
    return written;
}

const char* lambo_pak_format_name(LamboPakFormat format) {
    size_t index;
    for (index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        if (formats[index].format == format) return formats[index].name;
    }
    return "unknown";
}
