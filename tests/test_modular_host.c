#include "picocompress/host.h"

#include <stdio.h>
#include <string.h>

static const pcx_codec_v1 fake_codec = {
    PCX_CODEC_ABI_V1,
    sizeof(pcx_codec_v1),
    "fake",
    "registry test codec",
    "alias-one,alias-two",
    0,
    0, 0, 0, 0,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL, NULL, NULL
};

static const pcx_codec_v1 bad_codec = {
    999,
    sizeof(pcx_codec_v1),
    "bad",
    "bad ABI",
    NULL,
    0,
    0, 0, 0, 0,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL, NULL, NULL
};

int main(void)
{
    pcx_registry registry;
    const pcx_codec_v1 *found;
    pcx_registry_init(&registry);

    if (pcx_registry_register_static(&registry, &fake_codec) != PCX_OK) return 10;
    if (pcx_registry_count(&registry) != 1) return 11;
    if (pcx_registry_register_static(&registry, &fake_codec) != PCX_ERR_DUPLICATE) return 12;
    if (pcx_registry_register_static(&registry, &bad_codec) != PCX_ERR_ABI) return 13;

    found = pcx_registry_find(&registry, "fake");
    if (found != &fake_codec) return 14;
    if (pcx_registry_find(&registry, "alias-one") != &fake_codec) return 15;
    if (pcx_registry_find(&registry, "alias-two") != &fake_codec) return 16;
    if (pcx_registry_find(&registry, "missing") != NULL) return 17;
    if (pcx_registry_at(&registry, 0) != &fake_codec) return 18;
    if (pcx_registry_at(&registry, 1) != NULL) return 19;

    pcx_registry_close(&registry);
    if (pcx_registry_count(&registry) != 0) return 20;
    puts("modular host registry: ok");
    return 0;
}
