/* HD texture replacement and texture dump (vkr_hdtex.c). */
#ifndef VKR_HDTEX_H
#define VKR_HDTEX_H
#include <stdint.h>
#include "vkr.h"

int      hdtex_active(void);                  /* dump or replacements on */
uint64_t hdtex_key(const uint32_t *px, uint32_t w, uint32_t h);
void     hdtex_dump(uint64_t key, const uint32_t *px, uint32_t w, uint32_t h, uint32_t format);
/* 1: *out is the replacement (cached under cache_key); 0: none;
 * -1: there is one but it cannot be made this frame -- try again later. */
int      hdtex_get(VkrContext *vk, uint64_t key, uint64_t cache_key, VkrTex **out);
/* The pack is read into memory by a background thread once its map is
 * loaded; 1 when that has finished (within timeout_ms), else 0. */
int      hdtex_preload_wait(unsigned timeout_ms);
#endif
