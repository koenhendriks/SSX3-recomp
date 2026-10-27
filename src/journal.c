/* The session story in the log: see journal.h and docs/LOGGING.md. */
#include "journal.h"
#include "log.h"
#include "build_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
extern char **environ;
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

static int _stricmp_portable(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (x != y)
            return x - y;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int _strnicmp_portable(const char *a, const char *b, size_t n)
{
    for (; n && *a && *b; a++, b++, n--) {
        int x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (x != y)
            return x - y;
    }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

static void cpu_name(char *out, size_t n)
{
    unsigned r[12] = {0};
    int ok = 0;
#if defined(_WIN32)
    int i, regs[4];
    __cpuid(regs, (int)0x80000000);
    if ((unsigned)regs[0] >= 0x80000004u) {
        for (i = 0; i < 3; i++) {
            __cpuid(regs, (int)(0x80000002u + (unsigned)i));
            memcpy(&r[i * 4], regs, 16);
        }
        ok = 1;
    }
#elif defined(__x86_64__) || defined(__i386__)
    unsigned i, a, b, c, d;
    if (__get_cpuid(0x80000000u, &a, &b, &c, &d) && a >= 0x80000004u) {
        for (i = 0; i < 3; i++) {
            __get_cpuid(0x80000002u + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
        }
        ok = 1;
    }
#endif
    if (ok) {
        const char *p = (const char *)r;
        size_t k;
        while (*p == ' ') p++;
        snprintf(out, n, "%.48s", p);
        for (k = strlen(out); k && out[k - 1] == ' '; k--)
            out[k - 1] = 0;
    } else {
#ifndef _WIN32
        /* ARM Linux (Asahi): /proc/cpuinfo has no brand string; the machine */
        FILE *f = fopen("/proc/device-tree/model", "r");
        if (f) {
            size_t k = fread(out, 1, n - 1, f);
            out[k] = 0;
            fclose(f);
            return;
        }
#endif
        snprintf(out, n, "unknown CPU");
    }
}

static void system_line(void)
{
    char cpu[64], os[256];
    double ram_gb = 0.0;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    DEVMODEA dm;
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    RtlGetVersion_t rgv = (RtlGetVersion_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                                                  "RtlGetVersion");
    OSVERSIONINFOW vi;
    memset(&vi, 0, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
    if (rgv && rgv(&vi) == 0)
        snprintf(os, sizeof os, "Windows %s (%lu.%lu build %lu)",
                 vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000 ? "11" : vi.dwMajorVersion == 10 ? "10" : "",
                 vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    else
        snprintf(os, sizeof os, "Windows");
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms))
        ram_gb = (double)ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    cpu_name(cpu, sizeof cpu);
    log_msg(LOG_INFO, "SYSTEM", "%s, %s, %.1f GB RAM", os, cpu, ram_gb);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm))
        log_msg(LOG_INFO, "SYSTEM", "main monitor %lux%lu at %lu Hz, %d monitor(s)",
                dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, GetSystemMetrics(SM_CMONITORS));
#else
    struct utsname u;
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGE_SIZE);
    if (uname(&u) == 0)
        snprintf(os, sizeof os, "%s %s %s", u.sysname, u.release, u.machine);
    else
        snprintf(os, sizeof os, "POSIX");
    if (pages > 0 && psz > 0)
        ram_gb = (double)pages * (double)psz / (1024.0 * 1024.0 * 1024.0);
    cpu_name(cpu, sizeof cpu);
    log_msg(LOG_INFO, "SYSTEM", "%s, %s, %.1f GB RAM", os, cpu, ram_gb);
#endif
}

/* The XBE certificate: title id, name (UTF-16) and version. */
static void xbe_line(const uint8_t *xbe, size_t size, const char *game_dir)
{
    uint32_t base, cert, off, id, ver;
    char name[41];
    int i;
    if (!xbe || size < 0x200 || memcmp(xbe, "XBEH", 4)) {
        log_msg(LOG_WARN, "SYSTEM", "game folder %s: default.xbe has no XBE header", game_dir);
        return;
    }
    memcpy(&base, xbe + 0x104, 4);
    memcpy(&cert, xbe + 0x118, 4);
    off = cert - base;
    if (off + 0xB0 > size) {
        log_msg(LOG_WARN, "SYSTEM", "game folder %s: XBE certificate out of range", game_dir);
        return;
    }
    memcpy(&id, xbe + off + 8, 4);
    memcpy(&ver, xbe + off + 0xAC, 4);
    for (i = 0; i < 40; i++) {
        uint16_t c;
        memcpy(&c, xbe + off + 0xC + i * 2, 2);
        name[i] = c && c < 128 ? (char)c : (c ? '?' : 0);
        if (!c) break;
    }
    name[40] = 0;
    log_msg(LOG_INFO, "SYSTEM", "game: \"%s\", title id %08X, version %u, %zu bytes, folder %s",
            name, id, ver, size, game_dir);
    if (id != 0x4541003Cu)
        log_msg(LOG_WARN, "SYSTEM", "this is not SSX 3 (4541003C): expect trouble");
}

/* The launcher's choices, by name, from the RECOMP_* settings in effect. */
static const char *env_or(const char *name, const char *dflt)
{
    const char *v = getenv(name);
    return v && *v ? v : dflt;
}

static int env_on(const char *name, int dflt)
{
    const char *v = getenv(name);
    if (!v || !*v)
        return dflt;
    return !(v[0] == '0' && !v[1]);
}

/* RECOMP_RES / RECOMP_ASPECT / RECOMP_WIDESCREEN in words (main.c sets the
 * defaults before the header is written) */
static const char *res_words(char *out, size_t n)
{
    const char *r = env_or("RECOMP_RES", "");
    double v = atof(r);
    if (!*r)
        snprintf(out, n, "the monitor's height");
    else if (v >= 240)
        snprintf(out, n, "%.0fp", v);
    else if (v == 1)
        snprintf(out, n, "native 480p");
    else
        snprintf(out, n, "%sx 480p", r);
    return out;
}

static const char *aspect_words(void)
{
    const char *a = getenv("RECOMP_ASPECT"), *w = env_or("RECOMP_WIDESCREEN", "auto");
    if (a && *a)
        return a;
    if (w[0] == '0' && !w[1])
        return "4:3 (widescreen off)";
    if (w[0] == '1' && !w[1])
        return "16:9";
    return !strcmp(w, "auto") ? "the window's" : w;
}

static void settings_lines(void)
{
    char buf[1024], res[32];
    size_t n = 0;
    const char *hd = getenv("RECOMP_HDTEX");
#define ADD(...) do { n += (size_t)snprintf(buf + n, n < sizeof buf ? sizeof buf - n : 0, __VA_ARGS__); } while (0)
    ADD("resolution %s, aspect %s, MSAA %sx, anisotropic %sx, draw distance %sx, shadows %sx,",
        res_words(res, sizeof res), aspect_words(),
        env_or("RECOMP_MSAA", "4"), env_or("RECOMP_ANISO", "16"), env_or("RECOMP_DRAW_DIST", "1.5"),
        env_or("RECOMP_SHADOW_RES", "4"));
    ADD(" audio %s channels, %s, HD textures %s, HUD at the edges %s",
        env_or("RECOMP_AUDIO_CHANNELS", "2"), env_on("RECOMP_FULLSCREEN", 0) ? "fullscreen" : "windowed",
        hd && hd[0] == '0' && !hd[1] ? "off" : "on", env_on("RECOMP_HUD_EDGES", 1) ? "on" : "off");
    log_msg(LOG_INFO, "SETTINGS", "%s", buf);
    n = 0;
    ADD("CRT %s, FPS counter %s, motion blur %s, modern camera %s, skip intro %s, fast boot %s,",
        env_on("RECOMP_CRT", 0) ? "on" : "off", env_on("RECOMP_FPS_OVERLAY", 0) ? "on" : "off",
        env_on("RECOMP_MBLUR", 0) ? env_or("RECOMP_MBLUR", "") : "off",
        env_on("RECOMP_CAM", 0) ? "on" : "off", env_on("RECOMP_SKIP_INTRO", 1) ? "on" : "off",
        env_on("RECOMP_FAST_BOOT", 1) ? "on" : "off");
    ADD(" debug menu %s, money key %s, log level %s",
        env_on("RECOMP_DEBUG_MENU", 0) ? "on" : "off", env_on("RECOMP_MONEY_CHEAT", 0) ? "on" : "off",
        log_level_name(log_level()));
    log_msg(LOG_INFO, "SETTINGS", "%s", buf);
#undef ADD
}

/* Every RECOMP_* in effect, at DEBUG: the raw form for bug hunts. */
static void env_lines(void)
{
#ifdef _WIN32
    char *block = GetEnvironmentStringsA(), *p;
    if (!block)
        return;
    for (p = block; *p; p += strlen(p) + 1)
        if (!strncmp(p, "RECOMP_", 7))
            log_msg(LOG_DEBUG, "SETTINGS", "%s", p);
    FreeEnvironmentStringsA(block);
#else
    char **e;
    for (e = environ; e && *e; e++)
        if (!strncmp(*e, "RECOMP_", 7))
            log_msg(LOG_DEBUG, "SETTINGS", "%s", *e);
#endif
}

/* B7: saves and loads, from the title's file opens (toolkit hook
 * xbox_file_hook, kernel_file.c). A save is Data.ssx in a save folder,
 * UDATA\\4541003c\\<save id>\\ (with SaveMeta.xbx and SaveImage.xbx):
 * written = saved, read = loaded. The same event for the same save within
 * 3 s is one line (a save writes in pieces). */
#ifdef _WIN32
#define NTSTATUS_T long
#else
#define NTSTATUS_T int32_t
#endif
extern void (*volatile xbox_file_hook)(const char *xbox_path, int write, NTSTATUS_T status);

static double s_save_t = -100.0;
static char s_save_last[96];

static void file_hook(const char *path, int write, NTSTATUS_T status)
{
    const char *u, *name, *p;
    char id[48], key[96];
    size_t n;
    double t;
    if (!path)
        return;
    {
        /* game files: D:\data\... or \Device\CdRom0\data\... (the disc,
         * i.e. game\data).
         * A disc file that will not open is missing or unreadable -- the
         * install is broken (once per path). Elsewhere a failed open is the
         * game probing for optional files (t:\$u\contentmeta.xbx). */
        const char *d = strstr(path, "CdRom");
        const char *rel = (path[0] == 'D' || path[0] == 'd') && path[1] == ':' ? path + 2
                          : d ? strchr(d, '\\') : NULL;
        if (status < 0) {
            static char said[16][128];
            static int nsaid;
            int k;
            if (rel && !write) {
                for (k = 0; k < nsaid && strcmp(said[k], rel + 1); k++)
                    ;
                if (k == nsaid) {
                    if (nsaid < 16)
                        snprintf(said[nsaid++], sizeof said[0], "%s", rel + 1);
                    log_msg(LOG_ERROR, "FILE", "game file missing or unreadable: %s (status %08lX)", rel + 1,
                            (unsigned long)status);
                }
            } else {
                log_msg(LOG_DEBUG, "FILE", "open failed: %s (%s, status %08lX)", path,
                        write ? "write" : "read", (unsigned long)status);
            }
        } else if (rel && !_strnicmp_portable(rel + 1, "data\\worlds\\", 12)) {
            /* the world an event loads (a .big each; opened more than once) */
            static char last_world[64];
            const char *w = rel + 13, *dot = strrchr(w, '.');
            if (dot && !_stricmp_portable(dot, ".big") && _stricmp_portable(w, last_world)) {
                snprintf(last_world, sizeof last_world, "%s", w);
                log_msg(LOG_INFO, "PLAY", "loading world file %s", w);
            }
        }
    }
    name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    if (_stricmp_portable(name, "Data.ssx"))
        return;
    /* the save folder, when the path has it (the game opens the file
     * relative to the save folder's handle, then the path is the name) */
    id[0] = 0;
    if ((u = strstr(path, "UDATA")) != NULL && name > u + 1) {
        for (p = name - 1; p > u && p[-1] != '\\'; p--)
            ;
        n = (size_t)(name - 1 - p);
        if (n >= sizeof id) n = sizeof id - 1;
        memcpy(id, p, n);
        id[n] = 0;
    }
    if (status < 0) {
        if (write)
            log_msg(LOG_WARN, "SAVE", "saving failed (status %08lX)", (unsigned long)status);
        return;                       /* a read that fails: looking for a save */
    }
    snprintf(key, sizeof key, "%c%s", write ? 'w' : 'r', id);
    t = log_uptime();
    if (!strcmp(key, s_save_last) && t - s_save_t < 3.0)
        return;
    snprintf(s_save_last, sizeof s_save_last, "%s", key);
    s_save_t = t;
    /* the game also reads the save before it overwrites it, so a read is
     * a load only when no "game saved" follows */
    log_msg(LOG_INFO, "SAVE", "%s%s%s%s", write ? "game saved" : "save data read", id[0] ? " (save " : "",
            id, id[0] ? ")" : "");
}

void journal_session_header(const uint8_t *xbe, size_t xbe_size, const char *game_dir)
{
    xbox_file_hook = file_hook;
    log_msg(LOG_INFO, "SESSION", "SSX 3 recomp, build %s%s%s (compiled %s %s), log level %s", SSX3_BUILD_COMMIT,
            SSX3_BUILD_DATE[0] ? " of " : "", SSX3_BUILD_DATE, __DATE__, __TIME__,
            log_level_name(log_level()));
    system_line();
    xbe_line(xbe, xbe_size, game_dir);
    settings_lines();
    env_lines();
}

void journal_session_end(void)
{
    extern volatile int g_ssx3_events_started, g_ssx3_events_finished;   /* game_state.c */
    unsigned warn, err, crit;
    const char *how;
    double t = log_uptime();
    int s = (int)t;
    {
        extern void ssx3_PerfSessionEnd(void);    /* perf.c */
        ssx3_PerfSessionEnd();
    }
    log_session_counts(&warn, &err, &crit, &how);
    log_msg(LOG_INFO, "SESSION", "session ended: %s, after %d:%02d:%02d; %d event%s started, %d with a result",
            how, s / 3600, s / 60 % 60, s % 60, g_ssx3_events_started,
            g_ssx3_events_started == 1 ? "" : "s", g_ssx3_events_finished);
    log_msg(warn + err + crit ? LOG_WARN : LOG_INFO, "SESSION",
            "this log has %u warning%s, %u error%s, %u critical", warn, warn == 1 ? "" : "s", err,
            err == 1 ? "" : "s", crit);
}

void journal_crash_context(void)
{
    extern void ssx3_WhereNow(char *out, size_t n);       /* game_state.c */
    extern const char *ssx3_ScreenNow(void);              /* ssx3_overrides.c */
    char where[128];
    const char *scr = ssx3_ScreenNow();
    ssx3_WhereNow(where, sizeof where);
    fprintf(stderr, "  while: %s%s%s\n", where, strcmp(scr, "in game") ? "; " : "",
            strcmp(scr, "in game") ? scr : "");
    log_recent_play();
    fflush(stderr);
}
