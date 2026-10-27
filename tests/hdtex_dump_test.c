/* src/vk/vkr_hdtex.c: the texture dump writes a texture once, and only the first
 * frames of a surface the game redraws every frame (the rider's shadow). */
#include "../src/vk/vkr_hdtex.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* What vkr_hdtex.c links against: no renderer. */
void log_msg(int level, const char *cat, const char *fmt, ...) { (void)level; (void)cat; (void)fmt; }
int vkr_has_bc7(VkrContext *c) { (void)c; return 0; }
VkrTex *vkr_tex_create_bc7(VkrContext *c, uint64_t key, uint32_t w, uint32_t h,
                           uint32_t mips, const void *data, size_t size, uint32_t flags)
{
    (void)c; (void)key; (void)w; (void)h; (void)mips; (void)data; (void)size; (void)flags;
    return NULL;
}

static char dir[256];

static int files(void)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d)))
        n += e->d_name[0] != '.';
    if (d)
        closedir(d);
    return n;
}

int main(void)
{
    uint32_t px[4 * 4] = {0};
    int i;

    snprintf(dir, sizeof dir, "/tmp/hdtex_dump_test.XXXXXX");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    setenv("RECOMP_TEX_DUMP", dir, 1);
    setenv("RECOMP_HDTEX", "0", 1);

    /* A shadow: one address, new pixels every frame (30 FPS for a second). */
    for (i = 0; i < 30; i++)
        hdtex_dump(0x1000u + (uint64_t)i, 0x8228D700u, i * 33.0, px, 4, 4, 6);
    CHECK(files() == 8);                  /* its first frames only */

    /* A texture drawn every frame: the same pixels, dumped once. */
    for (i = 0; i < 30; i++)
        hdtex_dump(0x2000u, 0x81000000u, i * 33.0, px, 4, 4, 6);
    CHECK(files() == 9);

    /* A new texture in a freed address seconds later is a texture, not a redraw. */
    hdtex_dump(0x3000u, 0x81000000u, 5000.0, px, 4, 4, 6);
    CHECK(files() == 10);

    /* The shadow address long after its last redraw: its next pixels are dumped. */
    hdtex_dump(0x4000u, 0x8228D700u, 9000.0, px, 4, 4, 6);
    CHECK(files() == 11);

    /* One texture drawn with four palettes in every frame: all four are dumped. */
    for (i = 0; i < 40; i++)
        hdtex_dump(0x5000u + (uint64_t)(i & 3), 0x82000000u, 10000.0 + i * 4.0, px, 4, 4, 6);
    CHECK(files() == 15);

    /* Two redrawn surfaces 128 KB apart (they shared a slot once): each stops. */
    for (i = 0; i < 30; i++) {
        hdtex_dump(0x6000u + (uint64_t)i, 0x8228D700u, 20000.0 + i * 33.0, px, 4, 4, 6);
        hdtex_dump(0x7000u + (uint64_t)i, 0x822AD700u, 20000.0 + i * 33.0, px, 4, 4, 6);
    }
    CHECK(files() == 31);

    /* Address 0 is an address like any other. */
    for (i = 0; i < 30; i++)
        hdtex_dump(0x8000u + (uint64_t)i, 0, 30000.0 + i * 33.0, px, 4, 4, 6);
    CHECK(files() == 39);

    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
        if (system(cmd)) {}
    }
    if (fails)
        return 1;
    printf("hdtex_dump_test: ok\n");
    return 0;
}
