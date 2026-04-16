#ifndef PICOCOMPRESS_H
#define PICOCOMPRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PC_BLOCK_SIZE
#define PC_BLOCK_SIZE 508u
#endif

#define PC_LITERAL_MAX 128u
#define PC_MATCH_MIN 3u
#define PC_MATCH_CODE_BITS 6u
#define PC_MATCH_MAX (PC_MATCH_MIN + ((1u << PC_MATCH_CODE_BITS) - 1u))
#define PC_OFFSET_BITS 9u
#define PC_OFFSET_MAX ((1u << PC_OFFSET_BITS) - 1u)
#define PC_BLOCK_MAX_COMPRESSED (PC_BLOCK_SIZE + (PC_BLOCK_SIZE / PC_LITERAL_MAX) + 16u)

#if PC_BLOCK_SIZE == 0u || PC_BLOCK_SIZE > PC_OFFSET_MAX
#error "PC_BLOCK_SIZE must be in the range [1, PC_OFFSET_MAX]."
#endif

#if PC_LITERAL_MAX > 128u
#error "PC_LITERAL_MAX must be <= 128 for the current token format."
#endif

#if (PC_MATCH_MAX - PC_MATCH_MIN) > ((1u << PC_MATCH_CODE_BITS) - 1u)
#error "PC_MATCH_MAX exceeds token format capacity."
#endif

#ifndef PC_HASH_BITS
#if PC_BLOCK_SIZE <= 256u
#define PC_HASH_BITS 8u
#elif PC_BLOCK_SIZE <= 512u
#define PC_HASH_BITS 9u
#else
#define PC_HASH_BITS 10u
#endif
#endif

#ifndef PC_HASH_CHAIN_DEPTH
#if PC_HASH_BITS <= 9u
#define PC_HASH_CHAIN_DEPTH 2u
#else
#define PC_HASH_CHAIN_DEPTH 1u
#endif
#endif

#if PC_HASH_CHAIN_DEPTH < 1u
#error "PC_HASH_CHAIN_DEPTH must be >= 1."
#endif

#define PC_DECODER_PAYLOAD_SIZE PC_BLOCK_SIZE

typedef enum pc_result {
    PC_OK = 0,
    PC_ERR_WRITE = -1,
    PC_ERR_INPUT = -2,
    PC_ERR_CORRUPT = -3,
    PC_ERR_OUTPUT_TOO_SMALL = -4
} pc_result;

typedef int (*pc_write_fn)(void *user, const uint8_t *data, size_t len);

typedef struct pc_encoder {
    uint8_t block[PC_BLOCK_SIZE];
    uint16_t block_len;
} pc_encoder;

typedef struct pc_decoder {
    uint8_t header[4];
    uint8_t header_len;
    uint16_t raw_len;
    uint16_t comp_len;
    uint16_t payload_len;
    uint8_t payload[PC_DECODER_PAYLOAD_SIZE];
    uint8_t raw[PC_BLOCK_SIZE];
} pc_decoder;

void pc_encoder_init(pc_encoder *enc);
pc_result pc_encoder_sink(
    pc_encoder *enc,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
);
pc_result pc_encoder_finish(pc_encoder *enc, pc_write_fn write_fn, void *user);

void pc_decoder_init(pc_decoder *dec);
pc_result pc_decoder_sink(
    pc_decoder *dec,
    const uint8_t *data,
    size_t len,
    pc_write_fn write_fn,
    void *user
);
pc_result pc_decoder_finish(pc_decoder *dec);

size_t pc_compress_bound(size_t input_len);
pc_result pc_compress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
);
pc_result pc_decompress_buffer(
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t output_cap,
    size_t *output_len
);

#ifdef __cplusplus
}
#endif

#endif
