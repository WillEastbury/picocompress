/* pc_minimal.c — Minimal profile (tiny MCUs) */
#define PC_HASH_BITS         8u
#define PC_HASH_CHAIN_DEPTH  1u
#define PC_HISTORY_SIZE    128u
#define PC_LAZY_STEPS        1u

#define pc_compress_buffer   pc_min_compress_buffer
#define pc_decompress_buffer pc_min_decompress_buffer
#define pc_compress_bound    pc_min_compress_bound
#define pc_encoder_init      pc_min_encoder_init
#define pc_encoder_sink      pc_min_encoder_sink
#define pc_encoder_finish    pc_min_encoder_finish
#define pc_encoder_get_stats pc_min_encoder_get_stats
#define pc_decoder_init      pc_min_decoder_init
#define pc_decoder_sink      pc_min_decoder_sink
#define pc_decoder_finish    pc_min_decoder_finish
#define pc_encoder           pc_min_encoder
#define pc_decoder           pc_min_decoder
#define pc_encoder_stats     pc_min_encoder_stats
#undef PICOCOMPRESS_H
#include "picocompress.h"
#include "picocompress.c"
