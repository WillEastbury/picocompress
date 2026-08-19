#include "picocompress/host.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCX_IO_CHUNK (64u * 1024u)

static void usage(FILE *out)
{
    fprintf(out,
        "picocompress - modular compression shell\n"
        "\n"
        "usage:\n"
        "  picocompress [--codec-dir DIR] [--plugin FILE] list\n"
        "  picocompress [--codec-dir DIR] [--plugin FILE] compress CODEC INPUT OUTPUT\n"
        "  picocompress [--codec-dir DIR] [--plugin FILE] decompress CODEC INPUT OUTPUT\n"
        "\n"
        "INPUT/OUTPUT may be '-' for stdin/stdout. Codec directories are also\n"
        "loaded from PICOCOMPRESS_CODEC_PATH, then '.', then './codecs'.\n");
}

static void *state_alloc(size_t size, size_t align)
{
    uintptr_t raw_addr;
    uintptr_t aligned_addr;
    void *raw;
    if (!size || !align || (align & (align - 1u)) != 0) return NULL;
    if (size > (size_t)-1 - align - sizeof(void *)) return NULL;
    raw = malloc(size + align - 1u + sizeof(void *));
    if (!raw) return NULL;
    raw_addr = (uintptr_t)raw + sizeof(void *);
    aligned_addr = (raw_addr + (uintptr_t)align - 1u) & ~((uintptr_t)align - 1u);
    ((void **)aligned_addr)[-1] = raw;
    return (void *)aligned_addr;
}

static void state_free(void *state)
{
    if (state) free(((void **)state)[-1]);
}

static int file_sink(void *user, const uint8_t *data, size_t len)
{
    FILE *out = (FILE *)user;
    if (!len) return 0;
    return fwrite(data, 1, len, out) == len ? 0 : -1;
}

static pcx_result run_stream(const pcx_codec_v1 *codec, int encode,
                             const char *input_path, const char *output_path)
{
    FILE *input = NULL;
    FILE *output = NULL;
    uint8_t *buffer = NULL;
    void *state = NULL;
    pcx_result result = PCX_OK;
    size_t state_size;
    size_t state_align;
    pcx_options options = { NULL, 0 };
    int close_input = 0;
    int close_output = 0;

    if (!codec || !input_path || !output_path) return PCX_ERR_INPUT;
    if (encode) {
        if (!(codec->capabilities & PCX_CODEC_CAP_COMPRESS)) return PCX_ERR_UNSUPPORTED;
        state_size = codec->encoder_state_size;
        state_align = codec->encoder_state_align;
    } else {
        if (!(codec->capabilities & PCX_CODEC_CAP_DECOMPRESS)) return PCX_ERR_UNSUPPORTED;
        state_size = codec->decoder_state_size;
        state_align = codec->decoder_state_align;
    }

    if (strcmp(input_path, "-") == 0) input = stdin;
    else {
        input = fopen(input_path, "rb");
        if (!input) return PCX_ERR_INPUT;
        close_input = 1;
    }
    if (strcmp(output_path, "-") == 0) output = stdout;
    else {
        output = fopen(output_path, "wb");
        if (!output) {
            if (close_input) fclose(input);
            return PCX_ERR_WRITE;
        }
        close_output = 1;
    }

    buffer = (uint8_t *)malloc(PCX_IO_CHUNK);
    state = state_alloc(state_size, state_align);
    if (!buffer || !state) {
        result = PCX_ERR_MEMORY;
        goto done;
    }

    if (encode) result = codec->encoder_init(state, &options);
    else result = codec->decoder_init(state, &options);
    if (result != PCX_OK) goto done;

    for (;;) {
        size_t got = fread(buffer, 1, PCX_IO_CHUNK, input);
        if (got) {
            if (encode)
                result = codec->encoder_sink(state, buffer, got, file_sink, output);
            else
                result = codec->decoder_sink(state, buffer, got, file_sink, output);
            if (result != PCX_OK) goto done;
        }
        if (got < PCX_IO_CHUNK) {
            if (ferror(input)) result = PCX_ERR_INPUT;
            break;
        }
    }

    if (result == PCX_OK) {
        if (encode) result = codec->encoder_finish(state, file_sink, output);
        else result = codec->decoder_finish(state);
    }
    if (result == PCX_OK && fflush(output) != 0) result = PCX_ERR_WRITE;

done:
    state_free(state);
    free(buffer);
    if (close_input) fclose(input);
    if (close_output && fclose(output) != 0 && result == PCX_OK) result = PCX_ERR_WRITE;
    if (result != PCX_OK && close_output) remove(output_path);
    return result;
}

static const char *capabilities(const pcx_codec_v1 *codec)
{
    if ((codec->capabilities & PCX_CODEC_CAP_COMPRESS) &&
        (codec->capabilities & PCX_CODEC_CAP_DECOMPRESS)) return "compress,decompress";
    if (codec->capabilities & PCX_CODEC_CAP_COMPRESS) return "compress";
    if (codec->capabilities & PCX_CODEC_CAP_DECOMPRESS) return "decompress";
    return "metadata";
}

static void load_default_codecs(pcx_registry *registry)
{
    const char *env = getenv("PICOCOMPRESS_CODEC_PATH");
    if (env && *env) pcx_registry_load_pathlist(registry, env);
    pcx_registry_load_directory(registry, ".");
    pcx_registry_load_directory(registry, "./codecs");
}

int main(int argc, char **argv)
{
    pcx_registry registry;
    const char **positional;
    int positional_count = 0;
    int i;
    int exit_code = 0;

    pcx_registry_init(&registry);
    load_default_codecs(&registry);

    positional = (const char **)calloc((size_t)(argc > 0 ? argc : 1), sizeof(*positional));
    if (!positional) return 3;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--plugin") == 0) {
            pcx_result result;
            if (++i >= argc) { usage(stderr); exit_code = 2; goto done; }
            result = pcx_registry_load(&registry, argv[i]);
            if (result != PCX_OK && result != PCX_ERR_DUPLICATE) {
                fprintf(stderr, "failed to load codec module: %s (%d)\n", argv[i], (int)result);
                exit_code = 3; goto done;
            }
        } else if (strcmp(argv[i], "--codec-dir") == 0) {
            if (++i >= argc) { usage(stderr); exit_code = 2; goto done; }
            pcx_registry_load_directory(&registry, argv[i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout); goto done;
        } else {
            positional[positional_count++] = argv[i];
        }
    }

    if (positional_count == 1 && strcmp(positional[0], "list") == 0) {
        size_t n;
        if (!pcx_registry_count(&registry)) {
            fprintf(stdout, "no codecs installed\n");
            goto done;
        }
        for (n = 0; n < pcx_registry_count(&registry); ++n) {
            const pcx_codec_v1 *codec = pcx_registry_at(&registry, n);
            fprintf(stdout, "%s\t%s\t%s\n", codec->name,
                    capabilities(codec), codec->description ? codec->description : "");
        }
        goto done;
    }

    if (positional_count == 4 &&
        (strcmp(positional[0], "compress") == 0 || strcmp(positional[0], "decompress") == 0)) {
        int encode = strcmp(positional[0], "compress") == 0;
        const pcx_codec_v1 *codec = pcx_registry_find(&registry, positional[1]);
        pcx_result result;
        if (!codec) {
            fprintf(stderr, "codec not installed: %s\n", positional[1]);
            exit_code = 4; goto done;
        }
        result = run_stream(codec, encode, positional[2], positional[3]);
        if (result != PCX_OK) {
            fprintf(stderr, "%s failed codec=%s result=%d\n",
                    encode ? "compress" : "decompress", codec->name, (int)result);
            exit_code = 5;
        }
        goto done;
    }

    usage(stderr);
    exit_code = 2;

done:
    free(positional);
    pcx_registry_close(&registry);
    return exit_code;
}
