#include "picocompress/host.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

typedef struct pcx_module_node {
    const pcx_codec_v1 *codec;
    void *handle;
    struct pcx_module_node *next;
} pcx_module_node;

static int pcx_has_capability_contract(const pcx_codec_v1 *codec)
{
    if (!codec || codec->abi_version != PCX_CODEC_ABI_V1 ||
        codec->struct_size < sizeof(pcx_codec_v1) || !codec->name || !codec->name[0])
        return 0;

    if (codec->capabilities & PCX_CODEC_CAP_COMPRESS) {
        if (!codec->encoder_state_size || !codec->encoder_state_align ||
            !codec->encoder_init || !codec->encoder_sink || !codec->encoder_finish)
            return 0;
    }
    if (codec->capabilities & PCX_CODEC_CAP_DECOMPRESS) {
        if (!codec->decoder_state_size || !codec->decoder_state_align ||
            !codec->decoder_init || !codec->decoder_sink || !codec->decoder_finish)
            return 0;
    }
    if ((codec->capabilities & (PCX_CODEC_CAP_COMPRESS | PCX_CODEC_CAP_DECOMPRESS)) &&
        !(codec->capabilities & PCX_CODEC_CAP_STREAMING))
        return 0;
    return 1;
}

static int pcx_alias_match(const char *aliases, const char *name)
{
    const char *start;
    const char *p;
    size_t wanted;
    if (!aliases || !name) return 0;
    wanted = strlen(name);
    start = aliases;
    p = aliases;
    for (;;) {
        if (*p == ',' || *p == ';' || *p == '\0') {
            const char *left = start;
            const char *right = p;
            while (left < right && (*left == ' ' || *left == '\t')) ++left;
            while (right > left && (right[-1] == ' ' || right[-1] == '\t')) --right;
            if ((size_t)(right - left) == wanted && memcmp(left, name, wanted) == 0)
                return 1;
            if (*p == '\0') break;
            start = p + 1;
        }
        ++p;
    }
    return 0;
}

static void pcx_close_handle(void *handle)
{
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static pcx_result pcx_register(pcx_registry *registry,
                               const pcx_codec_v1 *codec,
                               void *handle)
{
    pcx_module_node *node;
    if (!registry || !pcx_has_capability_contract(codec)) {
        pcx_close_handle(handle);
        return PCX_ERR_ABI;
    }
    if (pcx_registry_find(registry, codec->name)) {
        pcx_close_handle(handle);
        return PCX_ERR_DUPLICATE;
    }
    node = (pcx_module_node *)calloc(1, sizeof(*node));
    if (!node) {
        pcx_close_handle(handle);
        return PCX_ERR_MEMORY;
    }
    node->codec = codec;
    node->handle = handle;
    node->next = (pcx_module_node *)registry->head;
    registry->head = node;
    ++registry->count;
    return PCX_OK;
}

void pcx_registry_init(pcx_registry *registry)
{
    if (!registry) return;
    registry->head = NULL;
    registry->count = 0;
}

void pcx_registry_close(pcx_registry *registry)
{
    pcx_module_node *node;
    if (!registry) return;
    node = (pcx_module_node *)registry->head;
    while (node) {
        pcx_module_node *next = node->next;
        pcx_close_handle(node->handle);
        free(node);
        node = next;
    }
    registry->head = NULL;
    registry->count = 0;
}

pcx_result pcx_registry_register_static(pcx_registry *registry,
                                        const pcx_codec_v1 *codec)
{
    return pcx_register(registry, codec, NULL);
}

pcx_result pcx_registry_load(pcx_registry *registry, const char *path)
{
    void *handle = NULL;
    pcx_codec_query_fn query = NULL;
    const pcx_codec_v1 *codec;
    if (!registry || !path || !path[0]) return PCX_ERR_INPUT;
#if defined(_WIN32)
    handle = (void *)LoadLibraryA(path);
    if (!handle) return PCX_ERR_LOAD;
    query = (pcx_codec_query_fn)(void *)GetProcAddress((HMODULE)handle, PCX_CODEC_QUERY_SYMBOL);
#else
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return PCX_ERR_LOAD;
    query = (pcx_codec_query_fn)dlsym(handle, PCX_CODEC_QUERY_SYMBOL);
#endif
    if (!query) {
        pcx_close_handle(handle);
        return PCX_ERR_ABI;
    }
    codec = query();
    return pcx_register(registry, codec, handle);
}

static int pcx_ends_with(const char *name, const char *suffix)
{
    size_t n;
    size_t s;
    if (!name || !suffix) return 0;
    n = strlen(name);
    s = strlen(suffix);
    return n >= s && memcmp(name + n - s, suffix, s) == 0;
}

static int pcx_module_filename(const char *name)
{
#if defined(_WIN32)
    return name && strncmp(name, "picocodec_", 10) == 0 && pcx_ends_with(name, ".dll");
#elif defined(__APPLE__)
    return name && strncmp(name, "libpicocodec_", 13) == 0 && pcx_ends_with(name, ".dylib");
#else
    return name && strncmp(name, "libpicocodec_", 13) == 0 && pcx_ends_with(name, ".so");
#endif
}

static char *pcx_join_path(const char *directory, const char *name)
{
    size_t d;
    size_t n;
    char *out;
    char sep;
    int need_sep;
    if (!directory || !name) return NULL;
    d = strlen(directory);
    n = strlen(name);
#if defined(_WIN32)
    sep = '\\';
#else
    sep = '/';
#endif
    need_sep = d && directory[d - 1] != '/' && directory[d - 1] != '\\';
    out = (char *)malloc(d + (size_t)need_sep + n + 1);
    if (!out) return NULL;
    memcpy(out, directory, d);
    if (need_sep) out[d++] = sep;
    memcpy(out + d, name, n + 1);
    return out;
}

size_t pcx_registry_load_directory(pcx_registry *registry, const char *directory)
{
    size_t loaded = 0;
    if (!registry || !directory || !directory[0]) return 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE find;
    char *pattern = pcx_join_path(directory, "picocodec_*.dll");
    if (!pattern) return 0;
    find = FindFirstFileA(pattern, &data);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        char *path;
        pcx_result result;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!pcx_module_filename(data.cFileName)) continue;
        path = pcx_join_path(directory, data.cFileName);
        if (!path) continue;
        result = pcx_registry_load(registry, path);
        if (result == PCX_OK) ++loaded;
        free(path);
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(directory);
    struct dirent *entry;
    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL) {
        char *path;
        pcx_result result;
        if (!pcx_module_filename(entry->d_name)) continue;
        path = pcx_join_path(directory, entry->d_name);
        if (!path) continue;
        result = pcx_registry_load(registry, path);
        if (result == PCX_OK) ++loaded;
        free(path);
    }
    closedir(dir);
#endif
    return loaded;
}

size_t pcx_registry_load_pathlist(pcx_registry *registry, const char *path_list)
{
    size_t loaded = 0;
    char *copy;
    char *start;
    char *p;
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    if (!registry || !path_list || !path_list[0]) return 0;
    copy = (char *)malloc(strlen(path_list) + 1);
    if (!copy) return 0;
    strcpy(copy, path_list);
    start = copy;
    p = copy;
    for (;;) {
        if (*p == separator || *p == '\0') {
            char terminal = *p;
            *p = '\0';
            if (*start) loaded += pcx_registry_load_directory(registry, start);
            if (terminal == '\0') break;
            start = p + 1;
        }
        ++p;
    }
    free(copy);
    return loaded;
}

const pcx_codec_v1 *pcx_registry_find(const pcx_registry *registry,
                                      const char *name)
{
    pcx_module_node *node;
    if (!registry || !name) return NULL;
    node = (pcx_module_node *)registry->head;
    while (node) {
        if (strcmp(node->codec->name, name) == 0 ||
            pcx_alias_match(node->codec->aliases, name))
            return node->codec;
        node = node->next;
    }
    return NULL;
}

size_t pcx_registry_count(const pcx_registry *registry)
{
    return registry ? registry->count : 0;
}

const pcx_codec_v1 *pcx_registry_at(const pcx_registry *registry, size_t index)
{
    pcx_module_node *node;
    size_t i = 0;
    if (!registry) return NULL;
    node = (pcx_module_node *)registry->head;
    while (node) {
        if (i++ == index) return node->codec;
        node = node->next;
    }
    return NULL;
}
