/* pc_balanced.c — Balanced profile (default: b9 d2 h504 lazy1) */
#define pc_compress_buffer   pc_bal_compress_buffer
#define pc_decompress_buffer pc_bal_decompress_buffer
#define pc_compress_bound    pc_bal_compress_bound
#define pc_encoder_init      pc_bal_encoder_init
#define pc_encoder_sink      pc_bal_encoder_sink
#define pc_encoder_finish    pc_bal_encoder_finish
#define pc_encoder_get_stats pc_bal_encoder_get_stats
#define pc_decoder_init      pc_bal_decoder_init
#define pc_decoder_sink      pc_bal_decoder_sink
#define pc_decoder_finish    pc_bal_decoder_finish
#define pc_encoder           pc_bal_encoder
#define pc_decoder           pc_bal_decoder
#define pc_encoder_stats     pc_bal_encoder_stats
#undef PICOCOMPRESS_H
#include "picocompress.h"
#include "picocompress.c"
