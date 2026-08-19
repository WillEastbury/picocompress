#ifndef PICOCOMPRESS_CODEC_H
#define PICOCOMPRESS_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCX_CODEC_ABI_V1 1u
#define PCX_CODEC_QUERY_SYMBOL "picocompress_codec_query"

#if defined(_WIN32)
#define PCX_CODEC_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PCX_CODEC_EXPORT __attribute__((visibility("default")))
#else
#define PCX_CODEC_EXPORT
#endif

typedef enum pcx_result {
    PCX_OK = 0,
    PCX_ERR_WRITE = -1,
    PCX_ERR_INPUT = -2,
    PCX_ERR_CORRUPT = -3,
    PCX_ERR_OUTPUT_TOO_SMALL = -4,
    PCX_ERR_UNSUPPORTED = -5,
    PCX_ERR_ABI = -6,
    PCX_ERR_NO_CODEC = -7,
    PCX_ERR_LOAD = -8,
    PCX_ERR_MEMORY = -9,
    PCX_ERR_DUPLICATE = -10
} pcx_result;

#define PCX_CODEC_CAP_COMPRESS   0x00000001u
#define PCX_CODEC_CAP_DECOMPRESS 0x00000002u
#define PCX_CODEC_CAP_STREAMING  0x00000004u

/* Codec-specific options are intentionally stringly typed at the ABI boundary.
 * A codec must reject options it does not understand rather than silently
 * changing semantics. */
typedef struct pcx_option {
    const char *key;
    const char *value;
} pcx_option;

typedef struct pcx_options {
    const pcx_option *items;
    size_t count;
} pcx_options;

typedef int (*pcx_write_fn)(void *user, const uint8_t *data, size_t len);

typedef struct pcx_codec_v1 {
    uint32_t abi_version;
    uint32_t struct_size;

    const char *name;
    const char *description;
    /* Comma-separated alternative names, or NULL. */
    const char *aliases;
    uint32_t capabilities;

    size_t encoder_state_size;
    size_t encoder_state_align;
    size_t decoder_state_size;
    size_t decoder_state_align;

    pcx_result (*encoder_init)(void *state, const pcx_options *options);
    pcx_result (*encoder_sink)(void *state, const uint8_t *data, size_t len,
                               pcx_write_fn write_fn, void *write_user);
    pcx_result (*encoder_finish)(void *state, pcx_write_fn write_fn,
                                 void *write_user);

    pcx_result (*decoder_init)(void *state, const pcx_options *options);
    pcx_result (*decoder_sink)(void *state, const uint8_t *data, size_t len,
                               pcx_write_fn write_fn, void *write_user);
    pcx_result (*decoder_finish)(void *state);

    /* Optional convenience functions. Streaming entrypoints above remain the
     * canonical shell path so large inputs are never forced into memory. */
    size_t (*compress_bound)(size_t input_len);
    pcx_result (*compress_buffer)(const uint8_t *input, size_t input_len,
                                  uint8_t *output, size_t output_cap,
                                  size_t *output_len);
    pcx_result (*decompress_buffer)(const uint8_t *input, size_t input_len,
                                    uint8_t *output, size_t output_cap,
                                    size_t *output_len);
} pcx_codec_v1;

typedef const pcx_codec_v1 *(*pcx_codec_query_fn)(void);

#ifdef __cplusplus
}
#endif

#endif
