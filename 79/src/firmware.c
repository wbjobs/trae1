#include "firmware.h"
#include "logger.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int hex_char_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int parse_hex_byte(const char *str)
{
    int hi = hex_char_val(str[0]);
    int lo = hex_char_val(str[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

static firmware_t *parse_hex_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_ERROR("Cannot open firmware file: %s", path);
        return NULL;
    }

    firmware_t *fw = (firmware_t *)calloc(1, sizeof(firmware_t));
    if (!fw) {
        fclose(fp);
        return NULL;
    }

    fw->data = (uint8_t *)calloc(MAX_FIRMWARE_SIZE, 1);
    if (!fw->data) {
        free(fw);
        fclose(fp);
        return NULL;
    }

    char     line[512];
    uint32_t base_addr = 0;
    uint32_t min_addr  = 0xFFFFFFFF;
    uint32_t max_addr  = 0;
    int      has_data  = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] != ':') continue;

        int len  = parse_hex_byte(line + 1);
        int addr = (parse_hex_byte(line + 3) << 8) | parse_hex_byte(line + 5);
        int type = parse_hex_byte(line + 7);

        if (len < 0 || addr < 0 || type < 0) continue;

        switch (type) {
            case 0x00: {
                uint32_t full_addr = base_addr + (uint32_t)addr;
                if (full_addr + (uint32_t)len > MAX_FIRMWARE_SIZE) {
                    LOG_WARN("Hex file exceeds max firmware size at %08X", full_addr);
                    break;
                }
                for (int i = 0; i < len; i++) {
                    int byte = parse_hex_byte(line + 9 + i * 2);
                    if (byte < 0) break;
                    fw->data[full_addr + i] = (uint8_t)byte;
                }
                if (full_addr < min_addr) min_addr = full_addr;
                if (full_addr + (uint32_t)len > max_addr) max_addr = full_addr + (uint32_t)len;
                has_data = 1;
                break;
            }
            case 0x01:
                goto done;
            case 0x02:
                base_addr = ((uint32_t)parse_hex_byte(line + 9) << 12) |
                            ((uint32_t)parse_hex_byte(line + 11) << 4);
                break;
            case 0x04:
                base_addr = ((uint32_t)parse_hex_byte(line + 9) << 24) |
                            ((uint32_t)parse_hex_byte(line + 11) << 16);
                break;
            default:
                break;
        }
    }

done:
    fclose(fp);

    if (!has_data) {
        LOG_ERROR("No valid data in hex file: %s", path);
        free(fw->data);
        free(fw);
        return NULL;
    }

    fw->base_addr = min_addr;
    fw->size      = max_addr - min_addr;

    if (min_addr > 0) {
        memmove(fw->data, fw->data + min_addr, fw->size);
    }

    LOG_INFO("Parsed HEX: base=%08X size=%u bytes", fw->base_addr, (unsigned)fw->size);
    return fw;
}

static firmware_t *parse_bin_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Cannot open firmware file: %s", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || (size_t)fsize > MAX_FIRMWARE_SIZE) {
        LOG_ERROR("Invalid firmware size: %ld bytes", fsize);
        fclose(fp);
        return NULL;
    }

    firmware_t *fw = (firmware_t *)calloc(1, sizeof(firmware_t));
    if (!fw) {
        fclose(fp);
        return NULL;
    }

    fw->data = (uint8_t *)malloc((size_t)fsize);
    if (!fw->data) {
        free(fw);
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(fw->data, 1, (size_t)fsize, fp);
    fclose(fp);

    if (read_bytes != (size_t)fsize) {
        LOG_ERROR("Failed to read entire firmware file");
        free(fw->data);
        free(fw);
        return NULL;
    }

    fw->size      = (size_t)fsize;
    fw->base_addr = 0;

    LOG_INFO("Loaded BIN: size=%u bytes", (unsigned)fw->size);
    return fw;
}

firmware_t *firmware_load(const char *path)
{
    if (!path || !path[0]) return NULL;

    const char *ext = strrchr(path, '.');
    if (!ext) {
        LOG_ERROR("Cannot determine file type from: %s", path);
        return NULL;
    }

    if (strcasecmp(ext, ".hex") == 0) {
        return parse_hex_file(path);
    } else if (strcasecmp(ext, ".bin") == 0) {
        return parse_bin_file(path);
    } else {
        LOG_ERROR("Unsupported file extension: %s (use .bin or .hex)", ext);
        return NULL;
    }
}

void firmware_free(firmware_t *fw)
{
    if (fw) {
        if (fw->data) free(fw->data);
        free(fw);
    }
}

int firmware_save(const char *path, const uint8_t *data, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Cannot save firmware to: %s", path);
        return -1;
    }

    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (written != size) {
        LOG_ERROR("Failed to write entire firmware to file");
        return -1;
    }

    LOG_INFO("Firmware saved to: %s (%u bytes)", path, (unsigned)size);
    return 0;
}

static const uint32_t crc32_table[] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBB, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3B1E, 0xB2BD0B88,
    0x2BB45A32, 0x5CB36A44, 0xC2D7FE3A, 0xB5D0CEAC, 0x2CD99D16, 0x5BDEAF40,
    0x9D0D9060, 0xEA0AA0F6, 0x7303F14C, 0x0404C1DA, 0x9A605479, 0xED6764EF,
    0x746E3555, 0x036905C3, 0x93D61952, 0xE4D12944, 0x7DD8787E, 0x0ADF48E8,
    0x94BBFD4B, 0xE3BCCDDD, 0x7AB59C67, 0x0DB2ACA1, 0x806083C4, 0xF767B352,
    0x6E6EE2E8, 0x1969D27E, 0x870D47DD, 0xF00A774B, 0x690326F1, 0x1E041667,
    0x8EBB0BF6, 0xF9BC3B60, 0x60B54ADA, 0x17B27A4C, 0x89D6CCEF, 0xFED1FC79,
    0x67D8ADE3, 0x10DF9D75, 0xA0ABD2DC, 0xD7AC924A, 0x4EA5B3F0, 0x39A28366,
    0xA7C616C5, 0xD0C12653, 0x49C877E9, 0x3ECF477F, 0xA9DB4BEE, 0xDEDC7B78,
    0x47D52AC2, 0x30D21A54, 0xAEB68FF7, 0xD9B1BF61, 0x40B8EEDB, 0x37BFDE4D,
    0xBD1CA1B8, 0xCA1B912E, 0x5312C094, 0x2415F002, 0xBA71E5A1, 0xCD76D537,
    0x547F848D, 0x2378B41B, 0xB3C7288A, 0xC4C0181C, 0x5DC949A6, 0x2ACE7930,
    0xB4AADC93, 0xC3ADE605, 0x5AA4BDBF, 0x2DA38D29, 0xA0AF0AB0, 0xD7A83A26,
    0x4EA16B9C, 0x39A65B0A, 0xA7C2CEA9, 0xD0C5FE3F, 0x49CCAF85, 0x3ECB9F13,
    0xAEC58182, 0xD9C2B114, 0x40CBE0AE, 0x37CCD038, 0xA9A8453B, 0xDEAF75AD,
    0x47A62417, 0x30A11481, 0xBDB81BD4, 0xCABF2B42, 0x53B67AF8, 0x24B14A6E,
    0xBAD5DECD, 0xCDD2EE5B, 0x54DBBFE1, 0x23DC8F77, 0xB36F92E6, 0xC468A270,
    0x5D61F3CA, 0x2A66C35C, 0xB402D7DF, 0xC305E749, 0x5A0CB6F3, 0x2D0B8665,
    0x90B93E80, 0xE7BE0E16, 0x7EB75FAC, 0x09B06F3A, 0x97D4F599, 0xE0D3C50F,
    0x79DA94B5, 0x0EDDA423, 0x9ED0B1B2, 0xE9D78124, 0x70DED09E, 0x07D9E008,
    0x99BD75AB, 0xEEBA453D, 0x77B31487, 0x00B42411, 0x8D0E2EE4, 0xFA091E72,
    0x63004FC8, 0x14077F5E, 0x8A63EBFD, 0xFD64DB6B, 0x646D8AD1, 0x136ABA47,
    0x83D5B1D6, 0xF4D28140, 0x6DDBD0FA, 0x1ADCE06C, 0x84B8D5CF, 0xF3BFE559,
    0x6AB6B4E3, 0x1DB18475
};

uint32_t firmware_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}
