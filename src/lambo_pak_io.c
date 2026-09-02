#include "lambo_pak_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    file = fopen(path, "rb");
    if (file == NULL) return result_error("cannot open Controller Pak file", path);
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return result_error("cannot determine Controller Pak file size", path);
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
        free(container->data);
        container->data = NULL;
        return result_error("cannot read complete Controller Pak file", path);
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

static LamboPakIoResult publish_container(const char* path, const PakContainer* container) {
    char temporary[1024];
    FILE* file;
    size_t written;
    LamboPakIoResult result;

    if ((int)snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
        return result_error("Controller Pak path is too long", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return result_error("cannot create temporary Controller Pak file", temporary);
    written = fwrite(container->data, 1, container->size, file);
    if (fclose(file) != 0 || written != container->size) {
        remove(temporary);
        return result_error("cannot write complete Controller Pak file", temporary);
    }
#if defined(_WIN32)
    /* Unlike MSVCRT rename(), MoveFileEx replaces without first deleting the live
     * save. A failed publish therefore leaves the prior emulator container intact. */
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(temporary);
        return result_error("cannot replace Controller Pak file", path);
    }
#else
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return result_error("cannot replace Controller Pak file", path);
    }
#endif
    result = result_ok(container);
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

LamboPakIoResult lambo_pak_write_file(const char* path, const uint8_t image[LAMBO_PAK_SIZE]) {
    PakContainer container;
    LamboPakIoResult loaded = load_container(path, &container);

    if (!loaded.ok) {
        FILE* probe = fopen(path, "rb");
        if (probe != NULL) {
            fclose(probe);
            return loaded; /* Never replace an existing unrecognised file. */
        }
        memset(&container, 0, sizeof(container));
        container.size = LAMBO_PAK_SIZE;
        container.format = LAMBO_PAK_FORMAT_RAW;
        container.data = (uint8_t*)malloc(container.size);
        if (container.data == NULL) return result_error("out of memory while saving Controller Pak", path);
    }

    memcpy(container.data + container.pak_offset, image, LAMBO_PAK_SIZE);
    loaded = publish_container(path, &container);
    free(container.data);
    return loaded;
}

LamboPakIoResult lambo_pak_import_file(const char* source_path, const char* destination_path) {
    uint8_t image[LAMBO_PAK_SIZE];
    LamboPakIoResult source;
    LamboPakIoResult written;
    if (strcmp(source_path, destination_path) == 0)
        return result_error("import source and destination must be different", source_path);
    source = lambo_pak_read_file(source_path, image);
    if (!source.ok) return source;

    /* Imports intentionally produce raw MPK. Do not let an existing destination's
     * container format affect the result, and never modify the source file. */
    {
        PakContainer container;
        memset(&container, 0, sizeof(container));
        container.data = image;
        container.size = LAMBO_PAK_SIZE;
        container.format = LAMBO_PAK_FORMAT_RAW;
        written = publish_container(destination_path, &container);
    }
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
