/*
 * RECOMP_AUTONAV: drive the frontend from inside the process, from boot to a
 * race, by the game's own state -- the in-process twin of tools\to_race.ps1.
 *
 *   RECOMP_AUTONAV=race[,rider=N][,peak=N][,mode=N][,event=N][,brief][,flyover]
 *
 * rider = right presses on Select Character, peak = down presses on the
 * peak page (0-2: up presses, the list is Peak 3, 2, 1 with the cursor on 1;
 * Peaks 2/3 need the unlocked save), mode = down presses on the mode
 * page (0 Race, 1 Freestyle), event = down presses on the event page (Peak 1
 * Race: 0 Snow Jam, 1 Metro-City, 2 Happiness); "brief" stops at the race
 * briefing (the course fly-over before it is skipped) instead of pressing A
 * to start the race; "flyover" lets the course fly-over play, as a player
 * sees it, instead of skipping it.
 *
 * A host thread samples the app state (game_state.c) and the active frontend
 * screen (ssx3_overrides.c: vtable, id and the screen's live state, 5 =
 * accepting input) every 20 ms and presses buttons through the pad layer
 * (xbox_PadInject) as soon as a screen is up and has settled -- no pad file,
 * no polling scripts, no fixed waits. A press a screen drops is repeated
 * when the same screen is still up a while later. Every action is logged as
 * [AUTONAV]; "[AUTONAV] t=<ms> at the briefing" marks the arrival (the
 * scripts wait for it). After the briefing it re-arms: back in the frontend
 * after the race, it drives the menus to the next race the same way.
 * Boot -> briefing ~27 s (tools\to_race.ps1 without it: ~40 s); what is
 * left is the game's own: ~4.5 s boot, the title's 5 s logo before it takes
 * Start, 5 s "Checking hard disk", the load and the course fly-over.
 */
#ifdef _WIN32
#include <windows.h>
#else
#include "platform/win32_compat.h"
#endif
#include "recomp/gen/recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_PTR_VA    0x003A930Cu
#define VT_FRONTEND   0x00289618u
#define VT_EVENTENTER 0x0029AD48u
#define VT_LOADING    0x0029B0DCu
#define VT_INGAME     0x00299D7Cu
#define VT_BRIEFING   0x00297E30u   /* race briefing panel (ui screen) */
#define VT_BRIEFING2  0x00297F68u   /* briefing of a one-on-one race (The Throne) */
#define VT_PAUSE      0x002981D8u   /* pause menu */
#define VT_QUIT       0x00297B60u   /* quit dialog */

extern int xbox_PadInject(const char *line);
extern volatile uint32_t g_ssx3_ui_screen_vt, g_ssx3_ui_screen_id, g_ssx3_ui_screen_ptr;

static int s_rider, s_peak, s_mode, s_event, s_brief, s_flyover;
static DWORD s_t0;
/* Set when autonav reaches the briefing (RECOMP_VKR_TRACE_BRIEF times from it). */
volatile int g_ssx3_at_briefing;

static int guest_ok(uint32_t va)
{
    return va >= 0x10000u && (va < 0x04000000u || (va >= 0x80000000u && va < 0x84000000u));
}

static void press(const char *b, const char *why)
{
    fprintf(stderr, "[AUTONAV] t=%lu %s: %s\n", (unsigned long)(GetTickCount() - s_t0), why, b);
    fflush(stderr);
    xbox_PadInject(b);
}

static void presses(const char *b, int n, DWORD gap_ms, const char *why)
{
    int i;
    for (i = 0; i < n; i++) {
        press(b, why);
        Sleep(gap_ms);
    }
}

static uint32_t app_vt(void)
{
    uint32_t app = MEM32(APP_PTR_VA), cur;
    if (!guest_ok(app))
        return 0;
    cur = MEM32(app + 4);
    return guest_ok(cur) ? MEM32(cur) : 0;
}

static DWORD WINAPI autonav_thread(LPVOID unused)
{
    uint32_t act_ptr = 0, done_ptr = 0;
    DWORD act_since = 0, done_at = 0, ingame_since = 0, brief_since = 0;
    int loading_seen = 0, skipped = 0;
    uint32_t last_sp = 0, pre_ingame_sp = 0;
    unsigned last_sst = 99;
    (void)unused;

    for (;;) {
        DWORD now;
        uint32_t vt, sp, id;
        unsigned sst;

        Sleep(20);
        now = GetTickCount();
        vt = app_vt();
        if (vt == VT_EVENTENTER || vt == VT_LOADING)
            loading_seen = 1;

        if (vt == VT_INGAME && loading_seen) {
            /* After the load: a fly-over of the course ("Press A to skip"),
             * then the briefing panel (screen vtable 00297E30, one item,
             * "Continue"; 00297F68 for a one-on-one race, shown at once). A skips the fly-over; the briefing takes A once
             * it accepts input. */
            if (!ingame_since) {
                ingame_since = now;
                pre_ingame_sp = g_ssx3_ui_screen_ptr;
                fprintf(stderr, "[AUTONAV] t=%lu in game\n", (unsigned long)(now - s_t0));
                fflush(stderr);
            }
            sp = g_ssx3_ui_screen_ptr;
            sst = sp ? (unsigned)((MEM16(sp + 0x1C) >> 8) & 0x3F) : 0;
            if (sp != last_sp || sst != last_sst) {
                fprintf(stderr, "[AUTONAV] t=%lu in-game screen vt %08X state %u\n",
                        (unsigned long)(now - s_t0), g_ssx3_ui_screen_vt, sst);
                fflush(stderr);
                last_sp = sp;
                last_sst = sst;
            }
            /* The briefing: the first in-game menu screen after the load
             * (00297E30 races, 00297F68 one-on-one, others for the other
             * event types); never the pause menu or the quit dialog. The
             * HUD is not recorded as a screen at all. */
            if (sp && sst == 5 && (g_ssx3_ui_screen_vt & 0xFFFF0000u) == 0x00290000u
                && g_ssx3_ui_screen_vt != VT_PAUSE && g_ssx3_ui_screen_vt != VT_QUIT
                && sp != pre_ingame_sp) {
                if (!brief_since)
                    brief_since = now;
                else if (now - brief_since >= 300) {
                    fprintf(stderr, "[AUTONAV] t=%lu at the briefing\n", (unsigned long)(now - s_t0));
                    fflush(stderr);
                    g_ssx3_at_briefing = 1;
                    if (!s_brief)
                        press("a:200", "briefing");
                    /* Re-arm: back in the frontend after this race (quit,
                     * results), drive the menus to the next one. */
                    loading_seen = skipped = 0;
                    ingame_since = brief_since = 0;
                    act_ptr = done_ptr = 0;
                    continue;
                }
            } else {
                brief_since = 0;
                /* No briefing up yet: a fly-over (one-on-one races show
                 * their briefing at once, and A there would start the race). */
                if (!skipped && !s_flyover && now - ingame_since >= 1000) {
                    skipped = 1;
                    press("a:150", "skip fly-over");
                }
            }
            continue;
        }
        if (vt != VT_FRONTEND)
            continue;

        sp = g_ssx3_ui_screen_ptr;
        id = g_ssx3_ui_screen_id;
        sst = sp ? (unsigned)((MEM16(sp + 0x1C) >> 8) & 0x3F) : 0;
        if (sp != last_sp || sst != last_sst) {
            fprintf(stderr, "[AUTONAV] t=%lu screen %u state %u\n",
                    (unsigned long)(now - s_t0), (unsigned)id, sst);
            fflush(stderr);
            last_sp = sp;
            last_sst = sst;
        }
        if (sst != 5 || !sp) {
            act_ptr = 0;
            continue;
        }
        if (sp != act_ptr) {
            act_ptr = sp;
            act_since = now;
        }
        if (now - act_since < 250)                  /* let it settle */
            continue;
        if (id == 1)                    /* "Checking hard disk": goes on by itself */
            continue;
        if (sp == done_ptr) {
            /* Still up: the press was dropped (or a page sequence is still
             * running on the Peak/Mode/Event screen). Retry, plainly. */
            DWORD retry = id == 19 ? 6000 : 1500;
            if (now - done_at < retry)
                continue;
            press(id == 0 ? "start:150" : "a:150", "retry");
            done_at = now;
            continue;
        }
        done_ptr = sp;
        done_at = now;
        switch (id) {
        case 0:                                     /* title */
            press("start:150", "title");
            break;
        case 9:                                     /* Select Character */
            presses("right:100", s_rider, 300, "rider");
            press("a:150", "rider");
            break;
        case 19:                                    /* Peak -> Mode -> Event */
            presses("up:100", s_peak, 300, "peak");     /* list: Peak 3, 2, 1; starts on 1 */
            press("a:150", "peak");
            Sleep(1500);
            presses("down:100", s_mode, 300, "mode");
            press("a:150", "mode");
            Sleep(1500);
            presses("down:100", s_event, 300, "event");
            press("a:150", "event");
            break;
        default:                                    /* main, setup, rules, ... */
            {
                char why[32];
                snprintf(why, sizeof why, "screen %u", (unsigned)id);
                press("a:150", why);
            }
            break;
        }
        done_at = GetTickCount();
    }
}

static int opt(const char *spec, const char *key)
{
    const char *p = strstr(spec, key);
    return p ? atoi(p + strlen(key)) : 0;
}

void ssx3_AutonavStart(void)
{
    const char *e = getenv("RECOMP_AUTONAV");
    if (!e || strncmp(e, "race", 4) != 0)
        return;
    s_rider = opt(e, "rider=");
    s_peak = opt(e, "peak=");
    s_mode = opt(e, "mode=");
    s_event = opt(e, "event=");
    s_brief = strstr(e, "brief") != NULL;
    s_flyover = strstr(e, "flyover") != NULL;
    s_t0 = GetTickCount();
    fprintf(stderr, "[AUTONAV] to a race: rider %d, peak %d, mode %d, event %d%s\n",
            s_rider, s_peak, s_mode, s_event, s_brief ? ", stop at the briefing" : "");
    fflush(stderr);
    CloseHandle(CreateThread(NULL, 0, autonav_thread, NULL, 0, NULL));
}
