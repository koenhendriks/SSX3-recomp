/*
 * Settings changed while playing, to try them on a problem as it happens:
 *   F4  HD textures on / off
 *   F5  draw distance: Normal (1x) -> Far (1.5x) -> Maximum (2x)
 *   F6  rider shadows on / off
 *   F7  boost cheat on / off for player 1 (endless top-speed boost; the
 *       game's own cheat, ssx3_overrides.c sub_0013FD20) -- kept on through
 *       every event until pressed again
 *   F8  autopilot on / off for player 1 (rides the course's AI line,
 *       autopilot.c), kept on through every event until pressed again
 * Each change is shown on screen for a few seconds and logged as
 * INFO [SETTINGS] "live: ..." (the launcher's choice is what the next start
 * uses again). Polled from mark.c's input thread.
 */
#include "log.h"
#include <stdio.h>

extern int xbox_FramebufferKeyPresses(int vk);
extern void xbox_FramebufferOsd(const char *text);
extern int ssx3_LiveHdTextures(int on);              /* src/vk/vkr_backend.c */
extern int ssx3_LiveHdTexturesOn(void);
extern void ssx3_LiveShadows(int on);
extern float ssx3_LiveDrawDistNext(void);            /* ssx3_overrides.c */
extern int ssx3_BoostCheatToggle(void);              /* ssx3_overrides.c */
extern int ssx3_AutopilotToggle(void);               /* autopilot.c */
extern void ssx3_AutopilotPoll(void);

#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77

static void say(const char *what, const char *key)
{
    char osd[64];
    snprintf(osd, sizeof osd, "%s (%s)", what, key);
    xbox_FramebufferOsd(osd);
    log_msg(LOG_INFO, "SETTINGS", "live: %s (%s)", what, key);
}

void ssx3_LiveKeysPoll(void)
{
    static int shadows = 1;
    int k;
    for (k = xbox_FramebufferKeyPresses(VK_F4); k > 0; k--)
        say(ssx3_LiveHdTextures(!ssx3_LiveHdTexturesOn()) ? "HD textures on" : "HD textures off", "F4");
    for (k = xbox_FramebufferKeyPresses(VK_F5); k > 0; k--) {
        float d = ssx3_LiveDrawDistNext();
        say(d < 1.25f ? "draw distance Normal 1x" : d < 1.75f ? "draw distance Far 1.5x"
            : "draw distance Maximum 2x", "F5");
    }
    for (k = xbox_FramebufferKeyPresses(VK_F6); k > 0; k--) {
        shadows = !shadows;
        ssx3_LiveShadows(shadows);
        say(shadows ? "rider shadows on" : "rider shadows off", "F6");
    }
    for (k = xbox_FramebufferKeyPresses(VK_F7); k > 0; k--)
        say(ssx3_BoostCheatToggle() ? "boost cheat on" : "boost cheat off", "F7");
    for (k = xbox_FramebufferKeyPresses(VK_F8); k > 0; k--)
        say(ssx3_AutopilotToggle() ? "autopilot on" : "autopilot off", "F8");
    ssx3_AutopilotPoll();
}
