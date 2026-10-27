/* Draws two overlapping triangles with depth and writes selftest.bmp.
 * Usage: vkr_selftest [out.bmp] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vkr.h"

#define W 640
#define H 480

static VkrVertex vtx(float x, float y, float z, float w, float r, float g, float b)
{
    VkrVertex v;
    memset(&v, 0, sizeof v);
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = z; v.pos[3] = w;
    v.d0[0] = r; v.d0[1] = g; v.d0[2] = b; v.d0[3] = 1.0f;
    return v;
}

int main(int argc, char **argv)
{
    static uint8_t px[W * H * 4];
    const float clear[4] = {0.1f, 0.1f, 0.2f, 1.0f};
    /* Far triangle (z 0.8 of range) drawn last must lose to the near one. */
    const float zm = 16777215.0f;
    VkrVertex v[6] = {
        vtx(100, 80, 0.2f * zm, 1, 1, 0, 0), vtx(500, 120, 0.2f * zm, 1, 0, 1, 0),
        vtx(300, 420, 0.2f * zm, 1, 0, 0, 1),
        vtx(200, 60, 0.8f * zm, 1, 1, 1, 0), vtx(560, 300, 0.8f * zm, 1, 1, 1, 0),
        vtx(120, 440, 0.8f * zm, 1, 1, 1, 0),
    };
    const uint32_t idx[6] = {0, 1, 2, 3, 4, 5};
    VkrContext *c = vkr_create();
    FILE *f;
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t sz = 54 + W * H * 4, off = 54, i;
    int y;

    if (!c) return 1;
    if (!vkr_set_target(c, W, H, zm)) return 1;
    vkr_clear(c, clear, 1, 1.0f);
    {
        /* Four 16 MB textures in one frame overfill the 32 MB upload buffer, as a
         * texture pack does at a race start: none may fail, nor the draw after. */
        uint32_t *big = calloc((size_t)2048 * 2048, 4);
        int k, ok = big != NULL;
        for (k = 0; k < 4 && ok; k++)
            ok = vkr_tex_create(c, 0xB16000ull + k, 2048, 2048, big) != NULL;
        free(big);
        VkrDrawState st;
        memset(&st, 0, sizeof st);
        st.depth_test = st.depth_write = 1; st.depth_func = 0x203; st.write_mask = 0xF;
        ok = ok && vkr_draw(c, v, 6, idx, 6, &st);
        printf("upload buffer overflow: %s\n", ok ? "ok" : "FAIL (a texture or the draw was dropped)");
        if (!ok) return 2;
    }
    if (!vkr_read_back(c, px)) return 1;

    memcpy(hdr + 2, &sz, 4); memcpy(hdr + 10, &off, 4);
    i = 40; memcpy(hdr + 14, &i, 4);
    i = W; memcpy(hdr + 18, &i, 4);
    i = (uint32_t)-H; memcpy(hdr + 22, &i, 4);          /* top-down */
    hdr[26] = 1; hdr[28] = 32;
    f = fopen(argc > 1 ? argv[1] : "selftest.bmp", "wb");
    if (!f) return 1;
    fwrite(hdr, 1, 54, f);
    for (y = 0; y < H; y++)
        fwrite(px + (size_t)y * W * 4, 1, W * 4, f);
    fclose(f);
    printf("pixel(300,200) = %02x%02x%02x (expect red-ish, near tri wins)\n",
           px[(200 * W + 300) * 4 + 2], px[(200 * W + 300) * 4 + 1], px[(200 * W + 300) * 4]);
    printf("pixel(520,290) = %02x%02x%02x (expect yellow, far tri only)\n",
           px[(290 * W + 520) * 4 + 2], px[(290 * W + 520) * 4 + 1], px[(290 * W + 520) * 4]);
    vkr_destroy(c);
    return 0;
}
