#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

typedef struct {
    uint16_t size;
    uint16_t val2;
    uint16_t resolution;
    uint16_t val4;
} SubSongHeader;

int read_le32(FILE *f, uint32_t *val) {
    uint8_t bytes[4];
    if (fread(bytes, 1, 4, f) != 4) {
        return 0;
    }
    *val = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | 
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

int read_le16(FILE *f, uint16_t *val) {
    uint8_t bytes[2];
    if (fread(bytes, 1, 2, f) != 2) {
        return 0;
    }
    *val = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return 1;
}

void split_filename(const char *filename, char *base, char *ext) {
    const char *dot = strrchr(filename, '.');
    if (dot && dot != filename) {
        size_t base_len = dot - filename;
        strncpy(base, filename, base_len);
        base[base_len] = '\0';
        strcpy(ext, dot);
    } else {
        strcpy(base, filename);
        strcpy(ext, "");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Dynamix song -> MID converter\n");
        printf("Usage: %s song.rol song.mid\n", argv[0]);
        return 1;
    }

    FILE *infile = fopen(argv[1], "rb");
    if (!infile) {
        perror("Error opening input file");
        return 1;
    }

    fseek(infile, 0, SEEK_END);
    long musSize = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    uint32_t first_song_pos = musSize;
    uint32_t *song_offsets = NULL;
    size_t num_songs = 0;

    while (1) {
        long current_pos = ftell(infile);
        if (current_pos >= first_song_pos) break;

        uint32_t fofs;
        if (!read_le32(infile, &fofs)) {
            fprintf(stderr, "Error reading TOC at position %ld\n", current_pos);
            fclose(infile);
            return 1;
        }

        if (fofs < first_song_pos)
            first_song_pos = fofs;

        num_songs++;
        song_offsets = realloc(song_offsets, num_songs * sizeof(uint32_t));
        if (!song_offsets) {
            perror("Memory allocation failed");
            fclose(infile);
            return 1;
        }
        song_offsets[num_songs - 1] = fofs;
    }

    char base[256], ext[256];
    split_filename(argv[2], base, ext);

    for (size_t i = 0; i < num_songs; i++) {
        uint32_t offset = song_offsets[i];
        fseek(infile, offset, SEEK_SET);

        SubSongHeader header;
        if (!read_le16(infile, &header.size) ||
            !read_le16(infile, &header.val2) ||
            !read_le16(infile, &header.resolution) ||
            !read_le16(infile, &header.val4)) {
            fprintf(stderr, "Error reading sub-song header at 0x%04X\n", offset);
            continue;
        }

        uint32_t payload_len = header.size - 8;

        unsigned char mid_hdr[22] = {
            0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06,
            0x00, 0x00, 0x00, 0x01,
            (uint8_t)(header.resolution >> 8), (uint8_t)header.resolution,
            0x4D, 0x54, 0x72, 0x6B,
            (uint8_t)(payload_len >> 24), (uint8_t)(payload_len >> 16),
            (uint8_t)(payload_len >> 8), (uint8_t)payload_len
        };

        char out_filename[256];
        snprintf(out_filename, sizeof(out_filename), "%s%zu%s", base, i, ext);

        FILE *outfile = fopen(out_filename, "wb");
        if (!outfile) {
            perror("Error opening output file");
            continue;
        }

        fwrite(mid_hdr, 1, 22, outfile);

        uint8_t *payload = malloc(payload_len);
        if (!payload) {
            perror("Memory allocation failed");
            fclose(outfile);
            continue;
        }

        if (fread(payload, 1, payload_len, infile) != payload_len) {
            fprintf(stderr, "Error reading payload for song %zu\n", i);
            free(payload);
            fclose(outfile);
            continue;
        }

        fwrite(payload, 1, payload_len, outfile);
        free(payload);
        fclose(outfile);

        printf("Song %zu: offset = 0x%04X, size = 0x%04X, val2 = 0x%04X, resolution = %u, val4 = 0x%04X\n",
               i, (unsigned)offset, (unsigned)header.size, 
               (unsigned)header.val2, header.resolution, (unsigned)header.val4);
    }

    free(song_offsets);
    fclose(infile);
    return 0;
}