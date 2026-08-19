#include "picocompress/codec.h"
#include "picocompress/codecs/micro.h"
#include "picocompress.h"

#if defined(_MSC_VER)
#define PCX_ALIGNOF(type) __alignof(type)
#else
#define PCX_ALIGNOF(type) _Alignof(type)
#endif

static pcx_result pcx_from_pc(pc_result result)
{
    switch (result) {
        case PC_OK: return PCX_OK;
        case PC_ERR_WRITE: return PCX_ERR_WRITE;
        case PC_ERR_INPUT: return PCX_ERR_INPUT;
        case PC_ERR_CORRUPT: return PCX_ERR_CORRUPT;
        case PC_ERR_OUTPUT_TOO_SMALL: return PCX_ERR_OUTPUT_TOO_SMALL;
        default: return PCX_ERR_CORRUPT;
    }
}

static pcx_result micro_encoder_init(void *state, const pcx_options *options)
{
    if (!state) return PCX_ERR_INPUT;
    if (options && options->count) return PCX_ERR_UNSUPPORTED;
    pc_encoder_init((pc_encoder *)state);
    return PCX_OK;
}

static pcx_result micro_encoder_sink(void *state, const uint8_t *data, size_t len,
                                     pcx_write_fn write_fn, void *write_user)
{
    if (!state || (!data && len) || !write_fn) return PCX_ERR_INPUT;
    return pcx_from_pc(pc_encoder_sink((pc_encoder *)state, data, len,
                                       (pc_write_fn)write_fn, write_user));
}

static pcx_result micro_encoder_finish(void *state, pcx_write_fn write_fn,
                                       void *write_user)
{
    if (!state || !write_fn) return PCX_ERR_INPUT;
    return pcx_from_pc(pc_encoder_finish((pc_encoder *)state,
                                         (pc_write_fn)write_fn, write_user));
}

static pcx_result micro_decoder_init(void *state, const pcx_options *options)
{
    if (!state) return PCX_ERR_INPUT;
    if (options && options->count) return PCX_ERR_UNSUPPORTED;
    pc_decoder_init((pc_decoder *)state);
    return PCX_OK;
}

static pcx_result micro_decoder_sink(void *state, const uint8_t *data, size_t len,
                                     pcx_write_fn write_fn, void *write_user)
{
    if (!state || (!data && len) || !write_fn) return PCX_ERR_INPUT;
    return pcx_from_pc(pc_decoder_sink((pc_decoder *)state, data, len,
                                       (pc_write_fn)write_fn, write_user));
}

static pcx_result micro_decoder_finish(void *state)
{
    if (!state) return PCX_ERR_INPUT;
    return pcx_from_pc(pc_decoder_finish((pc_decoder *)state));
}

static size_t micro_compress_bound(size_t input_len)
{
    return pc_compress_bound(input_len);
}

static pcx_result micro_compress_buffer(const uint8_t *input, size_t input_len,
                                        uint8_t *output, size_t output_cap,
                                        size_t *output_len)
{
    return pcx_from_pc(pc_compress_buffer(input, input_len, output,
                                          output_cap, output_len));
}

static pcx_result micro_decompress_buffer(const uint8_t *input, size_t input_len,
                                          uint8_t *output, size_t output_cap,
                                          size_t *output_len)
{
    return pcx_from_pc(pc_decompress_buffer(input, input_len, output,
                                            output_cap, output_len));
}

static const pcx_codec_v1 micro_codec = {
    PCX_CODEC_ABI_V1,
    sizeof(pcx_codec_v1),
    "micro",
    "PicoCompress native v3 micro codec",
    "picocompress,pc",
    PCX_CODEC_CAP_COMPRESS | PCX_CODEC_CAP_DECOMPRESS | PCX_CODEC_CAP_STREAMING,
    sizeof(pc_encoder),
    PCX_ALIGNOF(pc_encoder),
    sizeof(pc_decoder),
    PCX_ALIGNOF(pc_decoder),
    micro_encoder_init,
    micro_encoder_sink,
    micro_encoder_finish,
    micro_decoder_init,
    micro_decoder_sink,
    micro_decoder_finish,
    micro_compress_bound,
    micro_compress_buffer,
    micro_decompress_buffer
};

const pcx_codec_v1 *picocompress_micro_codec(void)
{
    return &micro_codec;
}

#ifndef PCX_CODEC_STATIC
PCX_CODEC_EXPORT const pcx_codec_v1 *picocompress_codec_query(void)
{
    return picocompress_micro_codec();
}
#endif
