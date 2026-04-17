/* pc_micro.c — Micro profile (Cortex-M0, 2K SRAM) */
#define PC_BLOCK_SIZE       192u
#define PC_HASH_BITS         8u
#define PC_HASH_CHAIN_DEPTH  1u
#define PC_HISTORY_SIZE     64u
#define PC_LAZY_STEPS        0u

/* Rename public API to avoid symbol conflicts */
#define pc_compress_buffer   pc_micro_compress_buffer
#define pc_decompress_buffer pc_micro_decompress_buffer
#define pc_compress_bound    pc_micro_compress_bound
#define pc_encoder_init      pc_micro_encoder_init
#define pc_encoder_sink      pc_micro_encoder_sink
#define pc_encoder_finish    pc_micro_encoder_finish
#define pc_encoder_get_stats pc_micro_encoder_get_stats
#define pc_decoder_init      pc_micro_decoder_init
#define pc_decoder_sink      pc_micro_decoder_sink
#define pc_decoder_finish    pc_micro_decoder_finish
#define pc_encoder           pc_micro_encoder
#define pc_decoder           pc_micro_decoder
#define pc_encoder_stats     pc_micro_encoder_stats
#undef PICOCOMPRESS_H
#include "picocompress.h"
#include "picocompress.c"
