#include "lambo_pak_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

enum {
    FOUR_PORT_SIZE = 4 * LAMBO_PAK_SIZE,
    RETROARCH_SIZE = 296960,
    RETROARCH_PAK_OFFSET = 0x800,
    DEXDRIVE_SIZE = 36928,
    DEXDRIVE_PAK_OFFSET = 0x1040
};

static int failures;

static void expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int write_bytes(const char* path, const unsigned char* data, size_t size) {
    FILE* file = fopen(path, "wb");
    size_t count;
    if (file == NULL) return 0;
    count = fwrite(data, 1, size, file);
    return fclose(file) == 0 && count == size;
}

static unsigned char* read_bytes(const char* path, size_t expected_size) {
    unsigned char* data = (unsigned char*)malloc(expected_size);
    FILE* file = fopen(path, "rb");
    if (data == NULL || file == NULL) {
        free(data);
        if (file != NULL) fclose(file);
        return NULL;
    }
    if (fread(data, 1, expected_size, file) != expected_size || fgetc(file) != EOF || fclose(file) != 0) {
        free(data);
        return NULL;
    }
    return data;
}

static void fill_pattern(unsigned char* data) {
    size_t index;
    for (index = 0; index < LAMBO_PAK_SIZE; ++index)
        data[index] = (unsigned char)((index * 37u + 11u) & 0xffu);
}

static void make_valid_pak(unsigned char* data) {
    /* Keep fixture construction independent of production code so tests fail
     * if either side drifts from the SDK-compatible ID checksum contract. */
    const size_t offset = 0x20;
    unsigned short sum = 0;
    unsigned short inverted;
    size_t word;
    for (word = 0; word < 14; ++word) {
        const size_t pos = offset + word * 2;
        sum = (unsigned short)(sum + (unsigned short)((data[pos] << 8) | data[pos + 1]));
    }
    inverted = (unsigned short)(0xfff2u - sum);
    data[offset + 0x1c] = (unsigned char)(sum >> 8);
    data[offset + 0x1d] = (unsigned char)sum;
    data[offset + 0x1e] = (unsigned char)(inverted >> 8);
    data[offset + 0x1f] = (unsigned char)inverted;
}

int main(void) {
    static const char* raw_path = "pak_io_test_raw.mpk";
    static const char* four_path = "pak_io_test_four.mpk";
    static const char* srm_path = "pak_io_test_retroarch.srm";
    static const char* dex_path = "pak_io_test_dexdrive.n64";
    static const char* bad_path = "pak_io_test_eeprom.eep";
    static const char* import_path = "pak_io_test_imported.mpk";
    static const char* empty_path = "pak_io_test_empty.mpk";
    static const char* zero_path = "pak_io_test_zero.mpk";
    static const char* missing_path = "pak_io_missing_dir/save.mpk";
    unsigned char expected[LAMBO_PAK_SIZE];
    unsigned char actual[LAMBO_PAK_SIZE];
    unsigned char* container;
    LamboPakIoResult result;
    size_t index;

    remove(raw_path); remove(four_path); remove(srm_path);
    remove(dex_path); remove(bad_path); remove(import_path);
    remove(empty_path); remove(zero_path);
    fill_pattern(expected);
    make_valid_pak(expected);

    result = lambo_pak_write_file(raw_path, expected);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_RAW, "new saves are portable raw MPK files");
    memset(actual, 0, sizeof(actual));
    result = lambo_pak_read_file(raw_path, actual);
    expect(result.ok && memcmp(actual, expected, sizeof(actual)) == 0, "raw MPK round-trips");

    container = (unsigned char*)malloc(FOUR_PORT_SIZE);
    memset(container, 0xa5, FOUR_PORT_SIZE);
    memcpy(container, expected, LAMBO_PAK_SIZE);
    expect(write_bytes(four_path, container, FOUR_PORT_SIZE), "four-port fixture writes");
    result = lambo_pak_read_file(four_path, actual);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_FOUR_PORT &&
           memcmp(actual, expected, sizeof(actual)) == 0, "four-port MPK imports controller 1");
    free(container);

    container = (unsigned char*)malloc(RETROARCH_SIZE);
    memset(container, 0x5a, RETROARCH_SIZE);
    memcpy(container + RETROARCH_PAK_OFFSET, expected, LAMBO_PAK_SIZE);
    expect(write_bytes(srm_path, container, RETROARCH_SIZE), "RetroArch fixture writes");
    free(container);
    result = lambo_pak_read_file(srm_path, actual);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_RETROARCH &&
           result.pak_offset == RETROARCH_PAK_OFFSET &&
           memcmp(actual, expected, sizeof(actual)) == 0, "RetroArch SRM imports controller 1 at 0x800");
    for (index = 0x500; index < LAMBO_PAK_SIZE; ++index) actual[index] ^= 0xffu;
    result = lambo_pak_write_file(srm_path, actual);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_RETROARCH, "RetroArch SRM updates in place");
    container = read_bytes(srm_path, RETROARCH_SIZE);
    expect(container != NULL && container[0] == 0x5a &&
           container[RETROARCH_PAK_OFFSET - 1] == 0x5a &&
           container[RETROARCH_PAK_OFFSET + LAMBO_PAK_SIZE] == 0x5a,
           "RetroArch EEPROM, other paks, SRAM, and FlashRAM bytes are preserved");
    expect(container != NULL && memcmp(container + RETROARCH_PAK_OFFSET, actual, LAMBO_PAK_SIZE) == 0,
           "RetroArch controller 1 bytes are replaced");
    free(container);

    container = (unsigned char*)malloc(DEXDRIVE_SIZE);
    memset(container, 0x33, DEXDRIVE_SIZE);
    memcpy(container, "123-456-STD\0", 12);
    memcpy(container + DEXDRIVE_PAK_OFFSET, expected, LAMBO_PAK_SIZE);
    expect(write_bytes(dex_path, container, DEXDRIVE_SIZE), "DexDrive fixture writes");
    free(container);
    result = lambo_pak_read_file(dex_path, actual);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_DEXDRIVE &&
           result.pak_offset == DEXDRIVE_PAK_OFFSET &&
           memcmp(actual, expected, sizeof(actual)) == 0, "DexDrive N64 wrapper imports at 0x1040");

    container = (unsigned char*)calloc(1, LAMBO_PAK_SIZE);
    expect(write_bytes(bad_path, container, LAMBO_PAK_SIZE), "invalid same-sized fixture writes");
    result = lambo_pak_read_file(bad_path, actual);
    expect(!result.ok, "same-sized data without a Controller Pak ID block is rejected");
    free(container);

    container = (unsigned char*)calloc(1, 2048);
    expect(write_bytes(bad_path, container, 2048), "EEPROM fixture writes");
    free(container);
    result = lambo_pak_read_file(bad_path, actual);
    expect(!result.ok, "2 KiB cartridge EEPROM is not mistaken for a Controller Pak");

    result = lambo_pak_import_file(srm_path, import_path);
    expect(result.ok && result.format == LAMBO_PAK_FORMAT_RETROARCH, "SRM import reports its source format");
    container = read_bytes(import_path, LAMBO_PAK_SIZE);
    for (index = 0x500; index < LAMBO_PAK_SIZE; ++index) expected[index] ^= 0xffu;
    expect(container != NULL && memcmp(container, expected, LAMBO_PAK_SIZE) == 0,
           "import creates a raw MPK containing only controller 1");
    free(container);
    result = lambo_pak_import_file(import_path, import_path);
    expect(!result.ok, "import refuses to overwrite its own source");
    result = lambo_pak_import_file(import_path, "./pak_io_test_imported.mpk");
    expect(!result.ok, "import refuses a lexical alias of its own source");

    expect(write_bytes(empty_path, expected, 0), "empty target fixture writes");
    result = lambo_pak_write_file(empty_path, expected);
    expect(result.ok, "an explicitly selected empty target becomes a formatted raw MPK");
    memset(actual, 0, sizeof(actual));
    result = lambo_pak_read_file(empty_path, actual);
    expect(result.ok && memcmp(actual, expected, sizeof(actual)) == 0,
           "formatted empty target round-trips");

    container = (unsigned char*)calloc(1, LAMBO_PAK_SIZE);
    expect(write_bytes(zero_path, container, LAMBO_PAK_SIZE), "zero-filled target fixture writes");
    free(container);
    result = lambo_pak_write_file(zero_path, expected);
    expect(result.ok, "an explicitly selected zero-filled raw target becomes formatted");

    result = lambo_pak_write_file(missing_path, expected);
    expect(!result.ok && (strstr(result.error, "errno=") != NULL ||
                          strstr(result.error, "Win32 error=") != NULL),
           "I/O failures include the operating-system error code");

#if defined(_WIN32)
    {
        static const wchar_t unicode_dir[] = L"pak_io_t\u00e9st";
        static const char unicode_path[] = "pak_io_t\xC3\xA9st/save.mpk";
        CreateDirectoryW(unicode_dir, NULL);
        result = lambo_pak_write_file(unicode_path, expected);
        expect(result.ok, "UTF-8 Controller Pak paths work on Windows");
        memset(actual, 0, sizeof(actual));
        result = lambo_pak_read_file(unicode_path, actual);
        expect(result.ok && memcmp(actual, expected, sizeof(actual)) == 0,
               "UTF-8 Controller Pak path round-trips on Windows");
        DeleteFileW(L"pak_io_t\u00e9st\\save.mpk");
        RemoveDirectoryW(unicode_dir);
    }
#endif

    remove(raw_path); remove(four_path); remove(srm_path);
    remove(dex_path); remove(bad_path); remove(import_path);
    remove(empty_path); remove(zero_path);
    return failures == 0 ? 0 : 1;
}
