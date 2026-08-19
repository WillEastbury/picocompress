#include "picocompress/host.h"
#include "picocompress/codecs/micro.h"

#include <stdio.h>

int main(void)
{
    pcx_registry registry;
    const pcx_codec_v1 *micro;

    pcx_registry_init(&registry);
    micro = picocompress_micro_codec();
    if (!micro) return 10;
    if (pcx_registry_register_static(&registry, micro) != PCX_OK) return 11;
    if (pcx_registry_find(&registry, "micro") != micro) return 12;
    if (pcx_registry_find(&registry, "picocompress") != micro) return 13;
    if (pcx_registry_find(&registry, "pc") != micro) return 14;
    if ((micro->capabilities & (PCX_CODEC_CAP_COMPRESS | PCX_CODEC_CAP_DECOMPRESS)) !=
        (PCX_CODEC_CAP_COMPRESS | PCX_CODEC_CAP_DECOMPRESS)) return 15;

    pcx_registry_close(&registry);
    puts("static micro registration: ok");
    return 0;
}
