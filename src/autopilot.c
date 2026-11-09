/*
 * Autopilot for player 1 (2026-11-09): rides an event on its own, for
 * testing whole courses (rendering, streaming, crashes) without playing.
 *
 * The game tracks every rider's place on the course's AI line -- the line
 * the computer riders follow -- in the rider base (what the rider manager's
 * slots point to; offsets found by scanning player 1's rider in a Snow Jam
 * race, and matching the PS2 decomp's Rider_UpdateAIPathProgress fields):
 *   +0x180  position (world, Z up)          +0x250  velocity
 *   +0x4F0  nearest point on the AI line    +0x500  look-ahead point
 *   +0x528  distance off the line           +0x52C  line heading
 *   +0x530  distance to the finish
 * Each frame (from the player's cheat-detector update, ssx3_overrides.c,
 * on the game thread) the stick is pointed at the computer riders' steer
 * target -- the look-ahead point pulled back toward the line -- and boost
 * is held while the rider is on line. The input goes in through the
 * toolkit's pad report (xbox_PadAuto), so the game sees an ordinary pad and
 * the real one still works for everything else.
 *
 * F8 (live.c) or RECOMP_AUTOPILOT=1 turns it on (kept on through every
 * event until F8 again). RECOMP_AUTOPILOT_GAIN / _PULL / _SIGN tune it.
 */
#include "log.h"
#ifdef _WIN32
#include <windows.h>
#else
#include "platform/win32_compat.h"
#endif
#include "recomp/gen/recomp_types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern void xbox_PadAuto(int pad, int on, int lx, int ly, unsigned analog_mask);
extern uint32_t xbox_TickCountMs(void);
extern int ssx3_InGameMenuUp(void);                  /* ssx3_overrides.c */
extern volatile int g_ssx3_event_entries;            /* game_state.c */

#define PAD_X 0x04u                     /* analog button X = boost */

static volatile LONG s_on = -1;         /* -1 = not read yet */
static volatile LONG s_driving;
static volatile uint32_t s_last_ms;
static int s_done_at = -1, s_told_at = -1;   /* event entry finished / announced */
static float s_gain = 2.5f, s_pull = 0.5f, s_sign = -1.0f;   /* stick right = clockwise seen from above */

static float envf(const char *k, float d)
{
    const char *e = getenv(k);
    return e && *e ? (float)atof(e) : d;
}

int ssx3_AutopilotOn(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_AUTOPILOT");
        s_gain = envf("RECOMP_AUTOPILOT_GAIN", s_gain);
        s_pull = envf("RECOMP_AUTOPILOT_PULL", s_pull);
        s_sign = envf("RECOMP_AUTOPILOT_SIGN", s_sign) < 0 ? -1.0f : 1.0f;
        InterlockedCompareExchange(&s_on, e && e[0] && e[0] != '0' ? 1 : 0, -1);
        if (s_on)
            log_msg(LOG_INFO, "SETTINGS", "autopilot on for every event (RECOMP_AUTOPILOT)");
    }
    return (int)s_on;
}

/* F8: returns the new setting. */
int ssx3_AutopilotToggle(void)
{
    int on = !ssx3_AutopilotOn();
    InterlockedExchange(&s_on, on);
    if (!on && s_driving) {
        xbox_PadAuto(0, 0, 0, 0, 0);
        InterlockedExchange(&s_driving, 0);
    }
    return on;
}

/* From the input thread (mark.c, every 50 ms): hand the pad back once the
 * rider updates stop (menus, pause, loading). */
void ssx3_AutopilotPoll(void)
{
    if (s_driving && xbox_TickCountMs() - s_last_ms > 250) {
        xbox_PadAuto(0, 0, 0, 0, 0);
        InterlockedExchange(&s_driving, 0);
    }
}

static float wrap_pi(float a)
{
    while (a > 3.14159265f)
        a -= 6.28318531f;
    while (a < -3.14159265f)
        a += 6.28318531f;
    return a;
}

/* Player 1's rider base, once per game frame. */
void ssx3_AutopilotStep(uint32_t r)
{
    static uint32_t last_log;
    float px, py, nx, ny, lx, ly, vx, vy, tx, ty, head, want, err, off, stick;
    unsigned boost;
    uint32_t now;

    if (!ssx3_AutopilotOn())
        return;
    /* Not on the briefing, pause, results or the replay that follows (the
     * rider updates go on under those screens, and X would press there). */
    /* ... and not after the finish line (the replay rewinds the distance)
     * until the next event or restart. */
    if (MEMF(r + 0x530) <= 0.0f && s_done_at != g_ssx3_event_entries) {
        s_done_at = g_ssx3_event_entries;
        log_msg(LOG_INFO, "PLAY", "autopilot: over the finish line");
    }
    if (ssx3_InGameMenuUp() || s_done_at == g_ssx3_event_entries) {
        if (s_driving) {
            xbox_PadAuto(0, 0, 0, 0, 0);
            InterlockedExchange(&s_driving, 0);
        }
        return;
    }
    px = MEMF(r + 0x180); py = MEMF(r + 0x184);
    nx = MEMF(r + 0x4F0); ny = MEMF(r + 0x4F4);
    lx = MEMF(r + 0x500); ly = MEMF(r + 0x504);
    vx = MEMF(r + 0x250); vy = MEMF(r + 0x254);
    off = MEMF(r + 0x528);
    if (!isfinite(px) || !isfinite(nx) || !isfinite(lx) || !isfinite(vx))
        return;
    /* The computer riders' steer target: look-ahead plus part of the way
     * back to the line. */
    tx = lx + s_pull * (nx - px);
    ty = ly + s_pull * (ny - py);
    if (vx * vx + vy * vy > 200.0f * 200.0f)
        head = atan2f(vy, vx);
    else
        head = atan2f(ly - ny, lx - nx);   /* standing: the line's heading */
    want = atan2f(ty - py, tx - px);
    err = wrap_pi(want - head);
    stick = err * s_gain * s_sign;
    if (stick > 1.0f)
        stick = 1.0f;
    if (stick < -1.0f)
        stick = -1.0f;
    boost = fabsf(err) < 0.35f && fabsf(off) < 800.0f ? PAD_X : 0;
    xbox_PadAuto(0, 1, (int)(stick * 32767.0f), 0, boost);
    now = xbox_TickCountMs();
    s_last_ms = now;
    if (!s_driving)
        InterlockedExchange(&s_driving, 1);
    if (s_told_at != g_ssx3_event_entries) {
        s_told_at = g_ssx3_event_entries;
        log_msg(LOG_INFO, "PLAY", "autopilot riding (%.0f m to the finish)", MEMF(r + 0x530) / 100.0f);
    }
    if (now - last_log >= 5000) {
        last_log = now;
        log_msg(LOG_DEBUG, "PLAY", "autopilot: %.0f m to go, %.1f m off the line, heading error %.0f deg, "
                "stick %.2f, boost %s (strength %.2f), %.0f km/h", MEMF(r + 0x530) / 100.0f, off / 100.0f,
                err * 57.29578f, stick, boost ? "held" : "off", MEMF(r + 0x368),
                sqrtf(vx * vx + vy * vy) * 0.036f);
    }
}
