#ifndef PICOCOMPRESS_CODECS_MICRO_H
#define PICOCOMPRESS_CODECS_MICRO_H

#include "../codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codec-specific accessor for statically linked / embedded builds.
 * Dynamic modules additionally export the generic
 * picocompress_codec_query symbol expected by the host loader. */
const pcx_codec_v1 *picocompress_micro_codec(void);

#ifdef __cplusplus
}
#endif

#endif
