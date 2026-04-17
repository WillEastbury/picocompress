/* pc_q3.c — Q3 profile (medium MCU) */
#define PC_HASH_BITS        10u
#define PC_HASH_CHAIN_DEPTH  2u
#define PC_HISTORY_SIZE   1024u
#define PC_LAZY_STEPS        2u

#define pc_compress_buffer   pc_q3_compress_buffer
#define pc_decompress_buffer pc_q3_decompress_buffer
#define pc_compress_bound    pc_q3_compress_bound
#define pc_encoder_init      pc_q3_encoder_init
#define pc_encoder_sink      pc_q3_encoder_sink
#define pc_encoder_finish    pc_q3_encoder_finish
#define pc_encoder_get_stats pc_q3_encoder_get_stats
#define pc_decoder_init      pc_q3_decoder_init
#define pc_decoder_sink      pc_q3_decoder_sink
#define pc_decoder_finish    pc_q3_decoder_finish
#define pc_encoder           pc_q3_encoder
#define pc_decoder           pc_q3_decoder
#define pc_encoder_stats     pc_q3_encoder_stats
#undef PICOCOMPRESS_H
#include "picocompress.h"
#include "picocompress.c"
