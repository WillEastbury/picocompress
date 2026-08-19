#ifndef PICOCOMPRESS_HOST_H
#define PICOCOMPRESS_HOST_H

#include <stddef.h>
#include "codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(PCX_HOST_STATIC)
#  if defined(PCX_HOST_BUILD)
#    define PCX_HOST_API __declspec(dllexport)
#  else
#    define PCX_HOST_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define PCX_HOST_API __attribute__((visibility("default")))
#else
#  define PCX_HOST_API
#endif

typedef struct pcx_registry {
    void *head;
    size_t count;
} pcx_registry;

PCX_HOST_API void pcx_registry_init(pcx_registry *registry);
PCX_HOST_API void pcx_registry_close(pcx_registry *registry);

/* Register a codec already linked into the process. This is the embedded /
 * no-dynamic-loader path and uses exactly the same ABI validation as plugins. */
PCX_HOST_API pcx_result pcx_registry_register_static(
    pcx_registry *registry, const pcx_codec_v1 *codec);

/* Load one dynamic codec module. The module must export
 * picocompress_codec_query and return a pcx_codec_v1 descriptor. */
PCX_HOST_API pcx_result pcx_registry_load(
    pcx_registry *registry, const char *path);

/* Load all conventionally named codec modules from a directory. Returns the
 * number successfully loaded. Missing directories are not fatal. */
PCX_HOST_API size_t pcx_registry_load_directory(
    pcx_registry *registry, const char *directory);

/* Split a platform path-list (';' on Windows, ':' elsewhere) and load every
 * directory. Useful for PICOCOMPRESS_CODEC_PATH. */
PCX_HOST_API size_t pcx_registry_load_pathlist(
    pcx_registry *registry, const char *path_list);

PCX_HOST_API const pcx_codec_v1 *pcx_registry_find(
    const pcx_registry *registry, const char *name);
PCX_HOST_API size_t pcx_registry_count(const pcx_registry *registry);
PCX_HOST_API const pcx_codec_v1 *pcx_registry_at(
    const pcx_registry *registry, size_t index);

#ifdef __cplusplus
}
#endif

#endif
