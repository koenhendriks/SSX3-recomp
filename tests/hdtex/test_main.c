/*
 * hdtex test: the HD texture pack is read into memory by a background
 * thread (vkr_hdtex.c), so a texture's first use does no disk I/O on the
 * GPU executor. Vulkan is stubbed: vkr_tex_create_bc7 records its input.
 */
#include "../../src/vk/vkr_hdtex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_one(p) mkdir(p, 0755)
#endif

static int s_fail;
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        s_fail = 1;
}

/* ---- stubs for what vkr_hdtex.c calls -------------------------------- */
static uint32_t s_w, s_h;
static size_t s_len;
static uint8_t s_first;
int vkr_has_bc7(VkrContext *c) { (void)c; return 1; }
VkrTex *vkr_tex_create_bc7(VkrContext *c, uint64_t key, uint32_t w, uint32_t h, uint32_t mips,
                           const void *data, size_t len, uint32_t flags)
{
    (void)c; (void)key; (void)mips; (void)flags;
    s_w = w; s_h = h; s_len = len; s_first = ((const uint8_t *)data)[0];
    return (VkrTex *)(uintptr_t)1;
}
void log_msg(int level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }

/* A 4x4 BC7 DDS with a DX10 header: 148 bytes of header, one 16-byte block. */
static void write_dds(const char *path, uint8_t fill)
{
    uint8_t hdr[148] = {0}, block[16];
    uint32_t v;
    FILE *f = fopen(path, "wb");
    memcpy(hdr, "DDS ", 4);
    v = 4; memcpy(hdr + 12, &v, 4); memcpy(hdr + 16, &v, 4);
    v = 1; memcpy(hdr + 28, &v, 4);
    memcpy(hdr + 84, "DX10", 4);
    v = 98; memcpy(hdr + 128, &v, 4);
    memset(block, fill, sizeof block);
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(block, 1, sizeof block, f);
    fclose(f);
}

int main(void)
{
    const char *dir = "hdtex_test_pack";
    char path[256];
    FILE *f;
    VkrTex *tex = NULL;
    int r;

    printf("hdtex\n");
    mkdir_one(dir);
    snprintf(path, sizeof path, "%s/a.dds", dir);
    write_dds(path, 0xAB);
    snprintf(path, sizeof path, "%s/ssx3_map.txt", dir);
    f = fopen(path, "wb");
    fprintf(f, "00000000000000aa a.dds\n");
    fclose(f);
#ifdef _WIN32
    _putenv_s("RECOMP_HDTEX", dir);
#else
    setenv("RECOMP_HDTEX", dir, 1);
#endif

    check(hdtex_active(), "the pack's map is read");
    check(hdtex_preload_wait(5000), "the pack is preloaded in the background");
    snprintf(path, sizeof path, "%s/a.dds", dir);
    remove(path);                                  /* from now on, only memory has it */
    r = hdtex_get(NULL, 0xaa, 0x1234, &tex);
    check(r == 1 && tex, "a preloaded texture is made without its file");
    check(s_w == 4 && s_h == 4 && s_len == 16 && s_first == 0xAB, "from the file's own data");
    r = hdtex_get(NULL, 0xbb, 0x5678, &tex);
    check(r == 0, "an unmapped texture is left to the game");
    snprintf(path, sizeof path, "%s/ssx3_map.txt", dir);
    remove(path);
    printf(s_fail ? "FAILED\n" : "all passed\n");
    return s_fail;
}
