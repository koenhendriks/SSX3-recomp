/*
 * launcher (Linux) -- the SDL2 counterpart of launcher.c: pick the game
 * files and settings, then start the game.
 *
 * Same settings, same launcher.ini (section [launcher], same keys and
 * indices) and the same RECOMP_* hand-off as the Windows launcher, so an ini
 * carries over either way. Differences, all from what the Linux build of the
 * game can do:
 *  - no Audio output row: 5.1 output is XAudio2 only (its ini key is kept).
 *    Fullscreen, FPS counter, motion blur and the modern camera are here as
 *    on Windows (the SDL game window handles F11 / F3 too).
 *  - macOS (SSX3.app): launcher.ini, keys.ini, logs/ and the F12 marks are in
 *    ~/Library/Application Support/SSX3, the game's working folder, instead
 *    of inside the app bundle.
 *  - "Auto" resolution is resolved here from the desktop the launcher is on;
 *    main.c asks the monitor only on Windows. "Auto" aspect ratio is passed
 *    on as "auto": the renderer follows the game window's shape.
 *  - RECOMP_USB_PADS is set to the number of controllers, as main.c does
 *    from XInput on Windows, so a second controller makes Multi Play.
 *  - the game's output goes to logs/ beside the game folder when the
 *    launcher has no terminal, as the Windows game does when double-clicked.
 *
 * The widgets are drawn here (SDL2 has none): text with SDL2_ttf in the
 * desktop's sans-serif font (fontconfig), the folder picker is zenity or
 * kdialog, and the path can always be typed.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL.h>
#include <SDL_ttf.h>
#if defined(__APPLE__)
/* macOS: system fonts by path, the folder picker through AppleScript,
 * Finder to open the logs. */
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <fontconfig/fontconfig.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "xiso.h"                 /* the toolkit's disc image reader */
#include "build_info.h"           /* SSX3_BUILD_COMMIT: v0.2.4, or a commit */

extern char **environ;

#define GAME_EXE   "ssx3_recomp"
#define TITLE_ID   0x4541003Cu      /* SSX 3, PAL: what this build was made from */
#define WIN_MIN_W  560
#define WIN_MIN_H  320
#define FOOTER_H   56         /* the pinned row of buttons at the bottom */
/* The window is resizable: the form takes the width, scrolls when taller
 * than the window (wheel / trackpad), and its buttons stay pinned below. */
static int s_win_w = WIN_MIN_W, s_win_h = 760, s_scroll, s_content_h;
#define WIN_W      s_win_w

typedef struct { const char *label, *value; } Choice;

/* The console language the game is told (RECOMP_LANGUAGE): the PAL SSX 3
 * has English, French, German and Spanish. "Auto" = the system's. */
static const Choice k_lang[] = {
    {"Auto (system language)", ""},
    {"English", "en"},
    {"Fran\xc3\xa7" "ais", "fr"},
    {"Deutsch", "de"},
    {"Espa\xc3\xb1" "ol", "es"},
};

static const Choice k_res[] = {
    {"Auto (your monitor's height)", ""},
    {"480p (original, sharp pixels)", "480"},
    {"720p", "720"},
    {"1080p", "1080"},
    {"1440p", "1440"},
    {"2160p (4K)", "2160"},
};
static const Choice k_aspect[] = {
    {"Auto (follows the window)", "auto"},
    {"16:9", "16:9"},
    {"21:9", "21:9"},
    {"32:9", "32:9"},
    {"4:3 (original)", "0"},
};
static const Choice k_aniso[] = {
    {"Trilinear", "0"},
    {"4x anisotropic", "4"},
    {"8x anisotropic", "8"},
    {"16x anisotropic (sharpest)", "16"},
};
static const Choice k_drawdist[] = {
    {"Original", "1"},
    {"Far (1.5x)", "1.5"},
    {"Maximum (2x)", "2"},
};
static const Choice k_loglevel[] = {
    {"Errors only", "error"},
    {"Warnings and errors", "warn"},
    {"Normal (what you did, and problems)", "info"},
    {"Detailed (for bug reports)", "debug"},
    {"Everything (large logs)", "trace"},
};
static const Choice k_shadows[] = {
    {"Original (128x128)", "1"},
    {"High (512x512)", "4"},
    {"Ultra (1024x1024)", "8"},
};
static const Choice k_msaa[] = {
    {"Off", "0"},
    {"2x MSAA", "2"},
    {"4x MSAA (smooth edges)", "4"},
    {"8x MSAA", "8"},
};

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- settings: launcher.ini ---------------------------------------------- */

typedef struct { char key[64], val[PATH_MAX]; } IniEntry;

static IniEntry s_ini_kv[64];
static int s_ini_n;
static char s_dir[PATH_MAX];        /* the launcher's folder */
static char s_ini[PATH_MAX];

static void ini_load(void)
{
    char line[PATH_MAX + 80];
    int in_section = 0;
    FILE *f = fopen(s_ini, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && s_ini_n < N(s_ini_kv)) {
        char *eq, *end;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            in_section = !strncasecmp(line, "[launcher]", 10);
            continue;
        }
        if (!in_section || !(eq = strchr(line, '=')))
            continue;
        *eq = 0;
        for (end = eq; end > line && (end[-1] == ' ' || end[-1] == '\t'); end--)
            end[-1] = 0;
        snprintf(s_ini_kv[s_ini_n].key, sizeof s_ini_kv[0].key, "%s", line);
        snprintf(s_ini_kv[s_ini_n].val, sizeof s_ini_kv[0].val, "%s", eq + 1);
        s_ini_n++;
    }
    fclose(f);
}

static const char *ini_get(const char *key)
{
    for (int i = 0; i < s_ini_n; i++)
        if (!strcasecmp(s_ini_kv[i].key, key))
            return s_ini_kv[i].val;
    return NULL;
}

static int ini_int(const char *key, int def)
{
    const char *v = ini_get(key);
    return v && *v ? atoi(v) : def;
}

static void ini_set(const char *key, const char *val)
{
    int i;
    for (i = 0; i < s_ini_n; i++)
        if (!strcasecmp(s_ini_kv[i].key, key))
            break;
    if (i == s_ini_n) {
        if (s_ini_n == N(s_ini_kv))
            return;
        snprintf(s_ini_kv[s_ini_n++].key, sizeof s_ini_kv[0].key, "%s", key);
    }
    snprintf(s_ini_kv[i].val, sizeof s_ini_kv[0].val, "%s", val);
}

static void ini_set_int(const char *key, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    ini_set(key, b);
}

static void ini_save(void)
{
    FILE *f = fopen(s_ini, "w");
    if (!f)
        return;
    fprintf(f, "[launcher]\n");
    for (int i = 0; i < s_ini_n; i++)
        fprintf(f, "%s=%s\n", s_ini_kv[i].key, s_ini_kv[i].val);
    fclose(f);
}

/* ---- the form's state ---------------------------------------------------- */

static char s_path[PATH_MAX];
static int s_res, s_aspect, s_msaa, s_aniso, s_drawdist, s_shadows;
static int s_crt, s_hdtex, s_hudedges, s_skipintro, s_fastboot, s_debugmenu, s_moneycheat;
static int s_fullscreen, s_fps, s_mblur, s_cam, s_lang;
static int s_loglevel = 2;
static int s_game_ok;
static char s_status[512];

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The XBE certificate of an XBE in memory: title id and name. 0 = not one. */
static int xbe_cert(const unsigned char *x, size_t n, unsigned *title_id, char *name, size_t name_size)
{
    unsigned base, cert_va;
    const unsigned char *cert;
    int i;
    if (n < 0x180 || memcmp(x, "XBEH", 4))
        return 0;
    memcpy(&base, x + 0x104, 4);
    memcpy(&cert_va, x + 0x118, 4);
    if (cert_va < base || (size_t)(cert_va - base) + 0xA0 > n)
        return 0;
    cert = x + (cert_va - base);
    memcpy(title_id, cert + 8, 4);
    for (i = 0; i < 40 && (size_t)i + 1 < name_size; i++) {
        unsigned short c = (unsigned short)(cert[0xC + i * 2] | cert[0xD + i * 2] << 8);
        if (!c)
            break;
        name[i] = c < 128 ? (char)c : '?';
    }
    name[i] = 0;
    return 1;
}

/* Reads the XBE certificate: title id and name. 0 = not an XBE. */
static int read_xbe(const char *path, unsigned *title_id, char *name, size_t name_size)
{
    unsigned char hdr[0x180], cert[0xA0];
    unsigned base, cert_va;
    FILE *f = fopen(path, "rb");
    int i;
    if (!f)
        return 0;
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || memcmp(hdr, "XBEH", 4)) {
        fclose(f);
        return 0;
    }
    memcpy(&base, hdr + 0x104, 4);
    memcpy(&cert_va, hdr + 0x118, 4);
    if (cert_va < base || fseek(f, (long)(cert_va - base), SEEK_SET)
        || fread(cert, 1, sizeof cert, f) != sizeof cert) {
        fclose(f);
        return 0;
    }
    fclose(f);
    memcpy(title_id, cert + 8, 4);
    for (i = 0; i < 40 && (size_t)i + 1 < name_size; i++) {    /* UTF-16 title name */
        unsigned short c = (unsigned short)(cert[0xC + i * 2] | cert[0xD + i * 2] << 8);
        if (!c)
            break;
        name[i] = c < 128 ? (char)c : '?';
    }
    name[i] = 0;
    return 1;
}

/* Fills the status line; returns 1 when the folder can be played. Xbox file
 * names are case-insensitive and a rip can carry either spelling, so both
 * are tried. */
static int check_game(const char *dir, char *msg, size_t n)
{
    char xbe[PATH_MAX + 16], data[PATH_MAX + 8], name[48];
    unsigned id = 0;
    if (!*dir) {
        snprintf(msg, n, "Choose your SSX 3 disc image (.iso) or the folder of its files (the one with default.xbe).");
        return 0;
    }
    if (file_exists(dir)) {
        /* A disc image: XISO or a full redump, read in place by the game. */
        char why[160];
        xiso *x = xiso_open(dir, why, sizeof why);
        xiso_entry e;
        unsigned char *buf;
        size_t len = 0;
        int ok;
        if (!x) {
            snprintf(msg, n, "This file is %s.", why);
            return 0;
        }
        buf = (unsigned char *)xiso_read_all(x, "default.xbe", &len);
        ok = buf && xbe_cert(buf, len, &id, name, sizeof name);
        free(buf);
        if (!ok) {
            snprintf(msg, n, "This disc image has no readable default.xbe.");
        } else if (!xiso_find(x, "data", &e) || !e.is_dir) {
            snprintf(msg, n, "Found %s, but the disc image has no data folder.", name);
            ok = 0;
        } else if (id != TITLE_ID) {
            snprintf(msg, n, "This is %s (title %08X). This build needs SSX 3, PAL version (4541003C).",
                     *name ? name : "another game", id);
            ok = 0;
        } else {
            snprintf(msg, n, "Found %s (PAL) disc image, %d files. Ready to play.", name,
                     xiso_file_count(x));
        }
        xiso_close(x);
        return ok;
    }
    snprintf(xbe, sizeof xbe, "%s/default.xbe", dir);
    if (!file_exists(xbe))
        snprintf(xbe, sizeof xbe, "%s/DEFAULT.XBE", dir);
    if (!file_exists(xbe)) {
        snprintf(msg, n, "No default.xbe in this folder. Pick the folder that holds default.xbe and data/.");
        return 0;
    }
    if (!read_xbe(xbe, &id, name, sizeof name)) {
        snprintf(msg, n, "default.xbe could not be read (not an Xbox executable?).");
        return 0;
    }
    snprintf(data, sizeof data, "%s/data", dir);
    if (!dir_exists(data))
        snprintf(data, sizeof data, "%s/DATA", dir);
    if (!dir_exists(data)) {
        snprintf(msg, n, "Found %s, but the data/ folder next to it is missing.", name);
        return 0;
    }
    if (id != TITLE_ID) {
        snprintf(msg, n, "This is %s (title %08X). This build needs SSX 3, PAL version (4541003C).",
                 *name ? name : "another game", id);
        return 0;
    }
    snprintf(msg, n, "Found %s (PAL). Ready to play.", name);
    return 1;
}

static void update_status(void)
{
    s_game_ok = check_game(s_path, s_status, sizeof s_status);
}

static void load_settings(void)
{
    const char *dir = ini_get("game_dir");
    if (dir && *dir) {
        snprintf(s_path, sizeof s_path, "%s", dir);
    } else {
        /* First run: a game/ folder next to the launcher, or one level up
         * (build-linux/launcher inside the source tree). */
        static const char *cands[] = { "%s/game", "%s/../game" };
        for (int i = 0; i < N(cands); i++) {
            char cand[PATH_MAX], xbe[PATH_MAX + 16], real[PATH_MAX];
            snprintf(cand, sizeof cand, cands[i], s_dir);
            snprintf(xbe, sizeof xbe, "%s/default.xbe", cand);
            if (file_exists(xbe) && realpath(cand, real)) {
                snprintf(s_path, sizeof s_path, "%s", real);
                break;
            }
        }
    }
    s_res = ini_int("resolution", 0);
    s_aspect = ini_int("aspect", 0);
    s_msaa = ini_int("msaa", 2);
    s_aniso = ini_int("aniso", 3);
    s_drawdist = ini_int("draw_distance", 1);
    s_shadows = ini_int("shadows", 1);
    s_crt = ini_int("crt", 0);
    s_fullscreen = ini_int("fullscreen", 0);
    s_lang = ini_int("language", 0);
    if (s_lang < 0 || s_lang >= (int)N(k_lang)) s_lang = 0;
    s_fps = ini_int("fps_counter", 0);
    s_mblur = ini_int("motion_blur", 0);
    s_cam = ini_int("modern_camera", 0);
    s_hdtex = ini_int("hd_textures", 1);
    s_hudedges = ini_int("hud_edges", 1);
    s_skipintro = ini_int("skip_intro", 1);
    s_fastboot = ini_int("fast_boot", 1);
    s_debugmenu = ini_int("debug_menu", 0);
    s_moneycheat = ini_int("money_cheat", 0);
    s_loglevel = ini_int("log_level", 2);
    update_status();
}

static void save_settings(void)
{
    ini_set("game_dir", s_path);
    ini_set_int("resolution", s_res);
    ini_set_int("aspect", s_aspect);
    ini_set_int("msaa", s_msaa);
    ini_set_int("aniso", s_aniso);
    ini_set_int("draw_distance", s_drawdist);
    ini_set_int("shadows", s_shadows);
    ini_set_int("crt", s_crt);
    ini_set_int("fullscreen", s_fullscreen);
    ini_set_int("language", s_lang);
    ini_set_int("fps_counter", s_fps);
    ini_set_int("motion_blur", s_mblur);
    ini_set_int("modern_camera", s_cam);
    ini_set_int("hd_textures", s_hdtex);
    ini_set_int("hud_edges", s_hudedges);
    ini_set_int("skip_intro", s_skipintro);
    ini_set_int("fast_boot", s_fastboot);
    ini_set_int("debug_menu", s_debugmenu);
    ini_set_int("money_cheat", s_moneycheat);
    ini_set_int("log_level", s_loglevel);
    ini_save();
}

/* "Reset to defaults": every option as on a first start (the defaults of
 * load_settings); the game folder stays. */
static void reset_settings(void)
{
    s_res = 0;
    s_aspect = 0;
    s_msaa = 2;
    s_aniso = 3;
    s_drawdist = 1;
    s_shadows = 1;
    s_crt = 0;
    s_fullscreen = 0;
    s_lang = 0;
    s_fps = 0;
    s_mblur = 0;
    s_cam = 0;
    s_hdtex = 1;
    s_hudedges = 1;
    s_skipintro = 1;
    s_fastboot = 1;
    s_debugmenu = 0;
    s_moneycheat = 0;
    s_loglevel = 2;
    save_settings();
}

/* ---- helpers that run other programs ----------------------------------- */

/* Runs argv[0] from PATH, its stdout into out. Returns the exit status, or
 * -1 when it could not be started (not installed). */
static int run_capture(char *const argv[], char *out, size_t n)
{
    posix_spawn_file_actions_t fa;
    int fds[2], status = -1;
    size_t got = 0;
    ssize_t r;
    pid_t pid;

    out[0] = 0;
    if (pipe(fds))
        return -1;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    if (posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ)) {
        posix_spawn_file_actions_destroy(&fa);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    while (got + 1 < n && (r = read(fds[0], out + got, n - 1 - got)) > 0)
        got += (size_t)r;
    out[got] = 0;
    close(fds[0]);
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    out[strcspn(out, "\n")] = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void spawn_detached(char *const argv[])
{
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
        waitpid(pid, NULL, WNOHANG);
}

static void browse(void)
{
    char out[PATH_MAX], start[PATH_MAX + 2];
    int rc;

    snprintf(start, sizeof start, "%s/", *s_path ? s_path : getenv("HOME") ? getenv("HOME") : "/");
#if defined(__APPLE__)
    {
        char script[PATH_MAX + 256];
        snprintf(script, sizeof script,
                 "POSIX path of (choose folder with prompt \"Choose the SSX 3 game folder "
                 "(with default.xbe and data)\" default location (POSIX file \"%s\"))", start);
        char *osa[] = { "osascript", "-e", script, NULL };
        rc = run_capture(osa, out, sizeof out);
        {
            size_t l = strlen(out);
            while (l > 1 && out[l - 1] == '/')
                out[--l] = 0;
        }
    }
#else
    {
        char *zen[] = { "zenity", "--file-selection", "--directory",
                        "--title=Choose the SSX 3 game folder (with default.xbe and data)",
                        "--filename", start, NULL };
        rc = run_capture(zen, out, sizeof out);
    }
    if (rc < 0) {
        char *kd[] = { "kdialog", "--getexistingdirectory", start,
                       "--title", "Choose the SSX 3 game folder (with default.xbe and data)", NULL };
        rc = run_capture(kd, out, sizeof out);
    }
#endif
    if (rc < 0) {
        snprintf(s_status, sizeof s_status,
                 "No folder dialog found (install zenity or kdialog). Type the path in the box above.");
        s_game_ok = 0;
        return;
    }
    if (rc != 0 || !*out)
        return;                         /* cancelled */
    {
        /* Picked the disc's root with the game one level down? Use it. */
        char xbe[PATH_MAX + 16], sub[PATH_MAX + 24];
        snprintf(xbe, sizeof xbe, "%s/default.xbe", out);
        snprintf(sub, sizeof sub, "%s/game/default.xbe", out);
        if (!file_exists(xbe) && file_exists(sub))
            strncat(out, "/game", sizeof out - strlen(out) - 1);
    }
    snprintf(s_path, sizeof s_path, "%s", out);
    update_status();
}

/* Picks a disc image (.iso) with the system's file dialog. */
static void browse_image(void)
{
    char out[PATH_MAX], start[PATH_MAX + 2];
    int rc;
    struct stat st;

    snprintf(start, sizeof start, "%s", *s_path ? s_path : getenv("HOME") ? getenv("HOME") : "/");
    if (stat(start, &st) == 0 && S_ISREG(st.st_mode)) {
        char *slash = strrchr(start, '/');
        if (slash && slash != start)
            *slash = 0;
    }
#if defined(__APPLE__)
    {
        char script[PATH_MAX + 320];
        snprintf(script, sizeof script,
                 "POSIX path of (choose file with prompt \"Choose your SSX 3 disc image (.iso)\" "
                 "of type {\"public.iso-image\", \"iso\"} default location (POSIX file \"%s/\"))", start);
        char *osa[] = { "osascript", "-e", script, NULL };
        rc = run_capture(osa, out, sizeof out);
    }
#else
    {
        char fn[PATH_MAX + 4];
        snprintf(fn, sizeof fn, "%s/", start);
        char *zen[] = { "zenity", "--file-selection", "--title=Choose your SSX 3 disc image (.iso)",
                        "--file-filter=Xbox disc images | *.iso *.ISO *.xiso", "--filename", fn, NULL };
        rc = run_capture(zen, out, sizeof out);
        if (rc < 0) {
            char *kd[] = { "kdialog", "--getopenfilename", start, "*.iso *.ISO *.xiso", NULL };
            rc = run_capture(kd, out, sizeof out);
        }
    }
#endif
    if (rc < 0) {
        snprintf(s_status, sizeof s_status,
                 "No file dialog found (install zenity or kdialog). Type the path in the box above.");
        s_game_ok = 0;
        return;
    }
    if (rc != 0 || !*out)
        return;                         /* cancelled */
    snprintf(s_path, sizeof s_path, "%s", out);
    update_status();
}

#if defined(__APPLE__)
static char s_data[PATH_MAX];   /* ~/Library/Application Support/SSX3, or "" */

static int copy_file(const char *from, const char *to)
{
    char buf[8192];
    size_t n;
    FILE *in = fopen(from, "rb"), *out;
    if (!in)
        return 0;
    out = fopen(to, "wb");
    if (!out) { fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

static int data_dir_init(void)
{
    const char *home = getenv("HOME");
    char lib[PATH_MAX], ex_from[PATH_MAX + 64], ex_to[PATH_MAX + 32];
    if (!home || !*home)
        return 0;
    snprintf(lib, sizeof lib, "%s/Library/Application Support", home);
    snprintf(s_data, sizeof s_data, "%s/SSX3", lib);
    mkdir(lib, 0755);
    if (mkdir(s_data, 0755) != 0 && !dir_exists(s_data)) {
        s_data[0] = 0;
        return 0;
    }
    /* keys.example.ini beside keys.ini, as in the Windows release folder */
    snprintf(ex_from, sizeof ex_from, "%s/../Resources/keys.example.ini", s_dir);
    snprintf(ex_to, sizeof ex_to, "%s/keys.example.ini", s_data);
    if (!file_exists(ex_to))
        copy_file(ex_from, ex_to);
    return 1;
}
#endif

/* The system's language as one the game has (en/fr/de/es), English
 * otherwise. macOS: the first preferred language (an app started from the
 * Finder has no LANG); elsewhere LC_ALL / LC_MESSAGES / LANG. */
static const char *system_language(void)
{
    static const char *have[] = {"en", "fr", "de", "es"};
    char code[3] = {0};
    int i;
#if defined(__APPLE__)
    {
        CFArrayRef langs = CFLocaleCopyPreferredLanguages();
        if (langs && CFArrayGetCount(langs) > 0) {
            char buf[32];
            CFStringRef l = (CFStringRef)CFArrayGetValueAtIndex(langs, 0);
            if (CFStringGetCString(l, buf, sizeof buf, kCFStringEncodingUTF8)) {
                code[0] = (char)(buf[0] | 0x20);
                code[1] = (char)(buf[1] | 0x20);
            }
        }
        if (langs)
            CFRelease(langs);
    }
#else
    {
        const char *e = getenv("LC_ALL");
        if (!e || !*e) e = getenv("LC_MESSAGES");
        if (!e || !*e) e = getenv("LANG");
        if (e && e[0] && e[1]) {
            code[0] = (char)(e[0] | 0x20);
            code[1] = (char)(e[1] | 0x20);
        }
    }
#endif
    for (i = 0; i < 4; i++)
        if (!strcmp(code, have[i]))
            return have[i];
    return "en";
}

/* The game writes nothing but stdout/stderr; logs/ beside the game folder is
 * where the Windows game puts its log when it has no console. */
static void logs_dir(char *out, size_t n)
{
#if defined(__APPLE__)
    if (s_data[0]) {                     /* with keys.ini and the F12 marks */
        snprintf(out, n, "%s/logs", s_data);
        return;
    }
#endif
    char base[PATH_MAX], *slash;
    if (!*s_path || !realpath(s_path, base))
        snprintf(base, sizeof base, "%s/x", s_dir);
    if ((slash = strrchr(base, '/')) != NULL && slash != base)
        *slash = 0;
    snprintf(out, n, "%s/logs", base);
}

/* ---- controllers --------------------------------------------------------- */

static char s_pads[4][160];
/* Kept open while attached: the power level needs an open device, and
 * opening some pads every second resets their lights. */
static SDL_GameController *s_opened[16];

static SDL_GameController *opened(int device)
{
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    int i, free_slot = -1;
    for (i = 0; i < N(s_opened); i++) {
        if (s_opened[i] && !SDL_GameControllerGetAttached(s_opened[i])) {
            SDL_GameControllerClose(s_opened[i]);
            s_opened[i] = NULL;
        }
        if (s_opened[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_opened[i])) == id)
            return s_opened[i];
        if (!s_opened[i] && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return NULL;
    return s_opened[free_slot] = SDL_GameControllerOpen(device);
}

static void update_pads(void)
{
    int slot = 0;
    for (int i = 0; i < SDL_NumJoysticks() && slot < 4; i++) {
        SDL_GameController *c;
        const char *kind = "Controller", *power = "", *name;
        if (!SDL_IsGameController(i))
            continue;
        switch (SDL_GameControllerTypeForIndex(i)) {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:             kind = "Xbox controller"; break;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:                 kind = "PlayStation controller"; break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: kind = "Switch Pro controller"; break;
        default: break;
        }
        name = SDL_GameControllerNameForIndex(i);
        c = opened(i);
        if (c) {
            switch (SDL_JoystickCurrentPowerLevel(SDL_GameControllerGetJoystick(c))) {
            case SDL_JOYSTICK_POWER_WIRED:  power = ", wired"; break;
            case SDL_JOYSTICK_POWER_FULL:
            case SDL_JOYSTICK_POWER_MAX:    power = ", battery full"; break;
            case SDL_JOYSTICK_POWER_MEDIUM: power = ", battery medium"; break;
            case SDL_JOYSTICK_POWER_LOW:    power = ", battery low"; break;
            case SDL_JOYSTICK_POWER_EMPTY:  power = ", battery empty"; break;
            default: break;
            }
        }
        snprintf(s_pads[slot], sizeof s_pads[0], "Player %d:  %s connected%s%s%s%s", slot + 1, kind,
                 power, name ? " (" : "", name ? name : "", name ? ")" : "");
        slot++;
    }
    for (int i = slot; i < 4; i++) {
        if (i == 0)
            snprintf(s_pads[i], sizeof s_pads[0], "Player 1:  keyboard (no controller connected)");
        else
            snprintf(s_pads[i], sizeof s_pads[0], "Player %d:  -", i + 1);
    }
}

static int pad_count(void)
{
    int n = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        n += SDL_IsGameController(i) != 0;
    return n;
}

/* ---- launch -------------------------------------------------------------- */

static void env_set(const char *k, const char *v)
{
    if (v)
        setenv(k, v, 1);
    else
        unsetenv(k);
}

static const char *pick(const Choice *c, int n, int i)
{
    return i >= 0 && i < n ? c[i].value : NULL;
}

static SDL_Window *s_win;
/* The window class the launcher gave itself through the environment, which
 * the game must not inherit: its window would then be "ssx3-launcher". */
static int s_set_wmclass;

static void message(const char *text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SSX 3", text, s_win);
}

/* Replaces the launcher with the game, which inherits its settings through
 * the environment. Returns only on failure. */
static void play(void)
{
    char exe[PATH_MAX + 16], cwd[PATH_MAX + 16], buf[64];
    SDL_DisplayMode dm;
    int have_dm;

    snprintf(exe, sizeof exe, "%s/%s", s_dir, GAME_EXE);
    if (access(exe, X_OK)) {
        message(GAME_EXE " was not found next to the launcher.\n"
                "Keep the launcher in the same folder as " GAME_EXE ".");
        return;
    }
#if defined(__APPLE__)
    {
        /* A disc image is read-only: the saves (UDATA/TDATA, kept in the
         * game folder with an extracted rip) go to the data folder. The
         * first time, the saves of the folder played before are copied
         * over, so switching to the .iso keeps your progress. */
        const char *prev = ini_get("last_folder");
        char saves[PATH_MAX + 8], from[PATH_MAX + 8], to[PATH_MAX + 16];
        if (!prev || !*prev)
            prev = ini_get("game_dir");
        if (file_exists(s_path) && s_data[0]) {
            snprintf(saves, sizeof saves, "%s/saves", s_data);
            mkdir(saves, 0755);
            snprintf(to, sizeof to, "%s/UDATA", saves);
            if (!dir_exists(to) && prev && *prev && dir_exists(prev)) {
                static const char *trees[] = { "UDATA", "TDATA" };
                for (int i = 0; i < 2; i++) {
                    snprintf(from, sizeof from, "%s/%s", prev, trees[i]);
                    if (dir_exists(from)) {
                        char *cp[] = { "/bin/cp", "-R", from, saves, NULL };
                        pid_t pid;
                        if (posix_spawn(&pid, cp[0], NULL, NULL, cp, environ) == 0)
                            waitpid(pid, NULL, 0);
                    }
                }
            }
            env_set("RECOMP_SAVE_DIR", saves);
        } else if (dir_exists(s_path)) {
            ini_set("last_folder", s_path);
        }
    }
#endif
    save_settings();
    have_dm = SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(s_win), &dm) == 0 && dm.h > 0;

    env_set("RECOMP_GAME_DIR", s_path);
    env_set("RECOMP_KEYBOARD", "1");
    if (s_res > 0) {
        env_set("RECOMP_RES", pick(k_res, N(k_res), s_res));
    } else if (have_dm && dm.h >= 480) {
        snprintf(buf, sizeof buf, "%d", dm.h);
        env_set("RECOMP_RES", buf);
    } else {
        env_set("RECOMP_RES", NULL);
    }
    /* "auto", not the monitor's W:H: a fixed shape stops the renderer
     * following the window, and a game window that is not the monitor's
     * shape (a tiled one) then showed bars. */
    env_set("RECOMP_WIDESCREEN", s_aspect > 0 ? pick(k_aspect, N(k_aspect), s_aspect) : "auto");
    env_set("RECOMP_MSAA", pick(k_msaa, N(k_msaa), s_msaa));
    env_set("RECOMP_ANISO", pick(k_aniso, N(k_aniso), s_aniso));
    env_set("RECOMP_DRAW_DIST", pick(k_drawdist, N(k_drawdist), s_drawdist));
    env_set("RECOMP_SHADOW_RES", pick(k_shadows, N(k_shadows), s_shadows));
    env_set("RECOMP_CRT", s_crt ? "1" : NULL);
    env_set("RECOMP_FULLSCREEN", s_fullscreen ? "1" : NULL);
    env_set("RECOMP_LANGUAGE", s_lang > 0 ? k_lang[s_lang].value : system_language());
    env_set("RECOMP_FPS_OVERLAY", s_fps ? "1" : NULL);
    env_set("RECOMP_MBLUR", s_mblur ? "0.5" : NULL);
    env_set("RECOMP_CAM", s_cam ? "1" : NULL);
    env_set("RECOMP_HDTEX", s_hdtex ? NULL : "0");
    env_set("RECOMP_HUD_EDGES", s_hudedges ? NULL : "0");
    env_set("RECOMP_SKIP_INTRO", s_skipintro ? "1" : "0");
    env_set("RECOMP_FAST_BOOT", s_fastboot ? NULL : "0");
    env_set("RECOMP_DEBUG_MENU", s_debugmenu ? "1" : NULL);
    env_set("RECOMP_MONEY_CHEAT", s_moneycheat ? "1" : NULL);
    env_set("RECOMP_LOG_LEVEL", pick(k_loglevel, N(k_loglevel), s_loglevel));
    if (!getenv("RECOMP_USB_PADS")) {
        int pads = pad_count();
        snprintf(buf, sizeof buf, "%d", pads < 1 ? 1 : pads > 4 ? 4 : pads);
        env_set("RECOMP_USB_PADS", buf);
    }

    /* The working directory holds hd-textures/ and keys.ini: the launcher's
     * folder in a release, its parent in the source tree (build-linux/). */
    snprintf(cwd, sizeof cwd, "%s", s_dir);
    {
        char here[PATH_MAX + 16], up[PATH_MAX + 32];
        snprintf(here, sizeof here, "%s/hd-textures", s_dir);
        snprintf(up, sizeof up, "%s/../hd-textures", s_dir);
        if (!dir_exists(here) && dir_exists(up))
            snprintf(cwd, sizeof cwd, "%s/..", s_dir);
    }
#if defined(__APPLE__)
    /* SSX3.app: the data folder (keys.ini, logs/, F12 marks). The game finds
     * the HD textures in the bundle's Resources by itself. */
    if (s_data[0])
        snprintf(cwd, sizeof cwd, "%s", s_data);
#endif

    if (!isatty(STDERR_FILENO)) {
        char logs[PATH_MAX], path[PATH_MAX + 64];
        time_t t = time(NULL);
        struct tm tm;
        int fd;
        logs_dir(logs, sizeof logs);
        mkdir(logs, 0755);
        localtime_r(&t, &tm);
        snprintf(path, sizeof path, "%s/ssx3_%04d%02d%02d-%02d%02d%02d_stderr.txt", logs,
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        if ((fd = open("/dev/null", O_WRONLY)) >= 0) {
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
    }

    if (chdir(cwd)) {
        message("Could not enter the game's folder.");
        return;
    }
    SDL_DestroyWindow(s_win);
    s_win = NULL;
    TTF_Quit();
    SDL_Quit();
    if (s_set_wmclass) {
        unsetenv("SDL_VIDEO_WAYLAND_WMCLASS");
        unsetenv("SDL_VIDEO_X11_WMCLASS");
    }
    {
        char *argv[] = { exe, NULL };
        execv(exe, argv);
    }
    fprintf(stderr, "launcher: could not start %s: %s\n", exe, strerror(errno));
    exit(1);
}

static void open_logs(void)
{
    char logs[PATH_MAX];
    logs_dir(logs, sizeof logs);
    if (!dir_exists(logs)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SSX 3",
                                 "No logs yet: they appear after the game has been started "
                                 "from the launcher without a terminal.", s_win);
        return;
    }
    {
#if defined(__APPLE__)
        char *argv[] = { "open", logs, NULL };
#else
        char *argv[] = { "xdg-open", logs, NULL };
#endif
        spawn_detached(argv);
    }
}

/* ---- drawing ------------------------------------------------------------- */

static SDL_Renderer *s_ren;
static TTF_Font *s_font, *s_bold, *s_title;
static float s_scale = 1;

static const SDL_Color C_BG = {255, 255, 255, 255}, C_TEXT = {26, 26, 26, 255},
                       C_DIM = {102, 102, 102, 255}, C_BORDER = {138, 138, 138, 255},
                       C_HOVER = {229, 241, 251, 255}, C_ACCENT = {0, 103, 192, 255},
                       C_ACCENT_HI = {0, 84, 158, 255}, C_DISABLED = {204, 204, 204, 255},
                       C_OK = {0, 120, 40, 255}, C_ERR = {190, 40, 30, 255},
                       C_WHITE = {255, 255, 255, 255}, C_SHADOW = {0, 0, 0, 40};

static int S(int v) { return (int)(v * s_scale + 0.5f); }

static void color(SDL_Color c) { SDL_SetRenderDrawColor(s_ren, c.r, c.g, c.b, c.a); }

static void fill(int x, int y, int w, int h, SDL_Color c)
{
    SDL_Rect r = {S(x), S(y), S(w), S(h)};
    color(c);
    SDL_RenderFillRect(s_ren, &r);
}

static void frame(int x, int y, int w, int h, SDL_Color c)
{
    SDL_Rect r = {S(x), S(y), S(w), S(h)};
    color(c);
    SDL_RenderDrawRect(s_ren, &r);
}

/* Draws text with its top-left at (x, y), clipped to max_w logical pixels
 * (0 = none); wrap > 0 wraps at that width. Returns the height used. */
static int text(TTF_Font *f, const char *s, int x, int y, SDL_Color c, int max_w, int wrap)
{
    SDL_Surface *sf;
    SDL_Texture *tx;
    SDL_Rect src, dst;
    int h;
    if (!s || !*s)
        return TTF_FontHeight(f) / (s_scale > 0 ? s_scale : 1);
    sf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, s, c, (Uint32)S(wrap))
                  : TTF_RenderUTF8_Blended(f, s, c);
    if (!sf)
        return 0;
    tx = SDL_CreateTextureFromSurface(s_ren, sf);
    src.x = 0;
    src.y = 0;
    src.w = sf->w;
    src.h = sf->h;
    if (max_w > 0 && src.w > S(max_w)) {
        /* Too long for its box: keep the end, which is what tells two paths apart. */
        src.x = src.w - S(max_w);
        src.w = S(max_w);
    }
    dst.x = S(x);
    dst.y = S(y);
    dst.w = src.w;
    dst.h = src.h;
    SDL_RenderCopy(s_ren, tx, &src, &dst);
    h = (int)(sf->h / s_scale + 0.5f);
    SDL_DestroyTexture(tx);
    SDL_FreeSurface(sf);
    return h;
}

static int text_w(TTF_Font *f, const char *s)
{
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s, &w, &h);
    return (int)(w / s_scale + 0.5f);
}

static int font_h(TTF_Font *f) { return (int)(TTF_FontHeight(f) / s_scale + 0.5f); }

static void triangle_down(int cx, int cy, int half, SDL_Color c)
{
    color(c);
    for (int i = 0; i <= S(half); i++)
        SDL_RenderDrawLine(s_ren, S(cx) - S(half) + i, S(cy) - S(half) / 2 + i,
                           S(cx) + S(half) - i, S(cy) - S(half) / 2 + i);
}

static void check_mark(int x, int y, int size, SDL_Color c)
{
    /* A tick drawn three pixels thick at any scale. */
    int x0 = S(x + size * 22 / 100), y0 = S(y + size * 52 / 100);
    int x1 = S(x + size * 42 / 100), y1 = S(y + size * 72 / 100);
    int x2 = S(x + size * 78 / 100), y2 = S(y + size * 30 / 100);
    color(c);
    for (int d = -S(1); d <= S(1); d++) {
        SDL_RenderDrawLine(s_ren, x0, y0 + d, x1, y1 + d);
        SDL_RenderDrawLine(s_ren, x1, y1 + d, x2, y2 + d);
    }
}

/* ---- widgets: immediate mode --------------------------------------------- */

enum {
    W_NONE, W_PATH, W_RES, W_ASPECT, W_MSAA, W_ANISO, W_DRAWDIST, W_SHADOWS, W_LOGLEVEL, W_LANG
};

static int s_mx = -1, s_my = -1, s_click;       /* logical mouse, one click per frame */
static int s_focus = W_NONE;                    /* the text field, when typing */
static int s_open = W_NONE;                     /* the combo whose list is open */
static SDL_Rect s_open_box;
static const Choice *s_open_items;
static int s_open_n, *s_open_sel;

static int inside(int x, int y, int w, int h)
{
    return s_mx >= x && s_mx < x + w && s_my >= y && s_my < y + h;
}

static int button(int x, int y, int w, int h, const char *label, int primary, int enabled)
{
    int hot = enabled && s_open == W_NONE && inside(x, y, w, h);
    TTF_Font *f = primary ? s_bold : s_font;
    if (primary) {
        fill(x, y, w, h, !enabled ? C_DISABLED : hot ? C_ACCENT_HI : C_ACCENT);
    } else {
        fill(x, y, w, h, hot ? C_HOVER : C_BG);
        frame(x, y, w, h, enabled ? C_BORDER : C_DISABLED);
    }
    text(f, label, x + (w - text_w(f, label)) / 2, y + (h - font_h(f)) / 2,
         primary ? C_WHITE : enabled ? C_TEXT : C_DIM, 0, 0);
    return hot && s_click;
}

static void checkbox(int x, int y, int w, const char *label, int *val)
{
    int hot = s_open == W_NONE && inside(x, y, w, 22);
    if (*val) {
        fill(x, y + 3, 16, 16, hot ? C_ACCENT_HI : C_ACCENT);
        check_mark(x, y + 3, 16, C_WHITE);
    } else {
        fill(x, y + 3, 16, 16, hot ? C_HOVER : C_BG);
        frame(x, y + 3, 16, 16, C_BORDER);
    }
    text(s_font, label, x + 24, y + (22 - font_h(s_font)) / 2, C_TEXT, 0, 0);
    if (hot && s_click)
        *val = !*val;
}

static void combo(int id, int x, int y, int w, int h, const Choice *items, int n, int *sel)
{
    int hot = s_open == W_NONE && inside(x, y, w, h);
    const char *cur = *sel >= 0 && *sel < n ? items[*sel].label : "";
    fill(x, y, w, h, hot || s_open == id ? C_HOVER : C_BG);
    frame(x, y, w, h, s_open == id ? C_ACCENT : C_BORDER);
    text(s_font, cur, x + 8, y + (h - font_h(s_font)) / 2, C_TEXT, w - 34, 0);
    triangle_down(x + w - 14, y + h / 2, 4, C_DIM);
    if (hot && s_click) {
        s_open = id;
        s_open_box = (SDL_Rect){x, y, w, h};
        s_open_items = items;
        s_open_n = n;
        s_open_sel = sel;
        s_click = 0;
    }
}

/* The open combo's list, drawn last so it lies over the rows below it. */
static void combo_list(void)
{
    int x = s_open_box.x, w = s_open_box.w, row = s_open_box.h;
    int y = s_open_box.y + s_open_box.h + 1, h = row * s_open_n;
    if (s_open == W_NONE)
        return;
    fill(x + 3, y + 3, w, h, C_SHADOW);
    fill(x, y, w, h, C_BG);
    for (int i = 0; i < s_open_n; i++) {
        int ry = y + i * row, hot = s_mx >= x && s_mx < x + w && s_my >= ry && s_my < ry + row;
        if (hot)
            fill(x, ry, w, row, C_ACCENT);
        else if (i == *s_open_sel)
            fill(x, ry, w, row, C_HOVER);
        text(s_font, s_open_items[i].label, x + 8, ry + (row - font_h(s_font)) / 2,
             hot ? C_WHITE : C_TEXT, w - 16, 0);
    }
    frame(x, y, w, h, C_BORDER);
}

/* A click while a list is open goes to the list: a row selects, anywhere
 * else only closes it. */
static void combo_click(void)
{
    int x = s_open_box.x, w = s_open_box.w, row = s_open_box.h;
    int y = s_open_box.y + s_open_box.h + 1;
    if (s_open == W_NONE || !s_click)
        return;
    if (s_mx >= x && s_mx < x + w && s_my >= y && s_my < y + row * s_open_n)
        *s_open_sel = (s_my - y) / row;
    s_open = W_NONE;
    s_click = 0;
}

static void text_field(int id, int x, int y, int w, int h, char *buf)
{
    int hot = s_open == W_NONE && inside(x, y, w, h);
    fill(x, y, w, h, C_BG);
    frame(x, y, w, h, s_focus == id ? C_ACCENT : C_BORDER);
    if (s_focus == id)
        frame(x + 1, y + 1, w - 2, h - 2, C_ACCENT);
    text(s_font, buf, x + 6, y + (h - font_h(s_font)) / 2, C_TEXT, w - 14, 0);
    if (s_focus == id) {
        int cx = x + 6 + text_w(s_font, buf);
        if (cx > x + w - 8)
            cx = x + w - 8;
        fill(cx, y + 5, 1, h - 10, C_TEXT);
    }
    if (s_click) {
        if (hot && s_focus != id) {
            s_focus = id;
            SDL_StartTextInput();
        } else if (!hot && s_focus == id) {
            s_focus = W_NONE;
            SDL_StopTextInput();
        }
    }
}

static void heading(const char *s, int x, int y) { text(s_bold, s, x, y, C_TEXT, 0, 0); }

static void row_label(const char *s, int x, int y, int h)
{
    text(s_font, s, x, y + (h - font_h(s_font)) / 2, C_TEXT, 0, 0);
}

/* Lays out and draws the whole form; returns its height. Actions are taken
 * here as their widgets report clicks. */
static int draw_form(int *want_play)
{
    const int L = 20, CX = 160, CW = 300, RH = 26;
    static char auto_res[96];
    static Choice res[N(k_res)];
    int y = 14 - s_scroll, i, footer_y = s_win_h - FOOTER_H;
    int saved_click, saved_mx, saved_my;
    SDL_DisplayMode dm;

    combo_click();
    color(C_BG);
    SDL_RenderClear(s_ren);
    /* The pointer over the pinned footer must not reach the content
     * scrolled underneath it. */
    saved_click = s_click; saved_mx = s_mx; saved_my = s_my;
    if (s_my >= footer_y && s_open == W_NONE) {
        s_click = 0;
        s_mx = s_my = -1;
    }

    text(s_title, "SSX 3", L, y, C_TEXT, 0, 0);
#if defined(__APPLE__)
    text(s_font, "static recompilation for macOS   " SSX3_BUILD_COMMIT, L + text_w(s_title, "SSX 3") + 12,
#else
    text(s_font, "static recompilation for Linux   " SSX3_BUILD_COMMIT, L + text_w(s_title, "SSX 3") + 12,
#endif
         y + font_h(s_title) - font_h(s_font) - 4, C_DIM, 0, 0);
    y += font_h(s_title) + 14;

    heading("Game files", L, y);
    y += 24;
    text_field(W_PATH, L, y, WIN_W - 2 * L - 226, RH, s_path);
    if (button(WIN_W - L - 216, y, 110, RH, "Disc image...", 0, 1))
        browse_image();
    if (button(WIN_W - L - 96, y, 96, RH, "Folder...", 0, 1))
        browse();
    y += RH + 6;
    y += text(s_font, s_status, L, y, s_game_ok ? C_OK : C_ERR, 0, WIN_W - 2 * L);
    y += 14;

    heading("Display", L, y);
    y += 26;
    if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(s_win), &dm) == 0 && dm.h > 0)
        snprintf(auto_res, sizeof auto_res, "Auto (your monitor: %dp)", dm.h);
    else
        snprintf(auto_res, sizeof auto_res, "%s", k_res[0].label);
    memcpy(res, k_res, sizeof res);
    res[0].label = auto_res;
    row_label("Resolution", L, y, RH);
    combo(W_RES, CX, y, CW, RH, res, N(res), &s_res);
    y += RH + 6;
    row_label("Aspect ratio", L, y, RH);
    combo(W_ASPECT, CX, y, CW, RH, k_aspect, N(k_aspect), &s_aspect);
    y += RH + 6;
    row_label("Anti-aliasing", L, y, RH);
    combo(W_MSAA, CX, y, CW, RH, k_msaa, N(k_msaa), &s_msaa);
    y += RH + 6;
    row_label("Texture filtering", L, y, RH);
    combo(W_ANISO, CX, y, CW, RH, k_aniso, N(k_aniso), &s_aniso);
    y += RH + 6;
    row_label("Draw distance", L, y, RH);
    combo(W_DRAWDIST, CX, y, CW, RH, k_drawdist, N(k_drawdist), &s_drawdist);
    y += RH + 6;
    row_label("Shadow quality", L, y, RH);
    combo(W_SHADOWS, CX, y, CW, RH, k_shadows, N(k_shadows), &s_shadows);
    y += RH + 10;
    checkbox(L, y, 440, "CRT TV shader (needs a resolution above 480p)", &s_crt);
    y += 24;
    checkbox(L, y, 440, "HD textures (Bl4ckH4nd's HD texture pack)", &s_hdtex);
    y += 24;
    checkbox(L, y, 440, "HUD at the screen edges (widescreen)", &s_hudedges);
    y += 24;
    checkbox(L, y, 440, "Fullscreen (F11 toggles in game)", &s_fullscreen);
    y += 24;
    checkbox(L, y, 440, "FPS counter (F3 toggles in game)", &s_fps);
    y += 24;
    checkbox(L, y, 440, "Motion blur (riders stay sharp)", &s_mblur);
    y += 24;
    checkbox(L, y, 480, "Modern camera (smoother follow, speed FOV, shake)", &s_cam);
    y += 34;

    heading("Game", L, y);
    y += 26;
    row_label("Language", L, y, RH);
    combo(W_LANG, CX, y, CW, RH, k_lang, N(k_lang), &s_lang);
    y += RH + 8;
    checkbox(L, y, 440, "Skip the intro movies", &s_skipintro);
    y += 24;
    checkbox(L, y, 480, "Fast boot (shorter title and hard-disk-check waits)", &s_fastboot);
    y += 24;
    checkbox(L, y, 520,
             "Debug mode: EA's quick-start menu (press Start on the title screen)", &s_debugmenu);
    y += 24;
    checkbox(L, y, 520, "Testing: Home key gives player 1 200,000 money", &s_moneycheat);
    y += 28;
    row_label("Log detail", L, y, RH);
    combo(W_LOGLEVEL, CX, y, CW, RH, k_loglevel, N(k_loglevel), &s_loglevel);
    y += RH + 12;

    heading("Controllers", L, y);
    y += 24;
    for (i = 0; i < 4; i++, y += 21)
        text(s_font, s_pads[i], L, y, C_TEXT, WIN_W - 2 * L, 0);
    y += 2;
    y += text(s_font, "Plug in controllers before pressing Play; two or more enable Multi Play. "
                      "The keyboard always works as player 1 (keys.ini).",
              L, y, C_DIM, 0, WIN_W - 2 * L);
    y += 14;
    s_content_h = y + s_scroll;          /* the form's own height, unscrolled */

    /* Scroll position indicator when the form is taller than its area. */
    if (s_content_h > footer_y) {
        int track = footer_y - 8, bar = track * footer_y / s_content_h;
        int top = 4 + (track - bar) * s_scroll / (s_content_h - footer_y > 0 ? s_content_h - footer_y : 1);
        SDL_Rect r = {S(WIN_W - 6), S(top), S(3), S(bar)};
        color(C_DIM);
        SDL_RenderFillRect(s_ren, &r);
    }

    /* The pinned footer. */
    s_click = saved_click; s_mx = saved_mx; s_my = saved_my;
    {
        SDL_Rect r = {0, S(footer_y), S(WIN_W), S(FOOTER_H)};
        color(C_BG);
        SDL_RenderFillRect(s_ren, &r);
        r.h = S(1) > 0 ? S(1) : 1;
        color(C_DIM);
        SDL_RenderFillRect(s_ren, &r);
    }
    y = footer_y + (FOOTER_H - 32) / 2;
    if (button(L, y, 110, 32, "Open logs", 0, 1))
        open_logs();
    if (button(L + 120, y, 150, 32, "Reset to defaults", 0, 1))
        reset_settings();
    if (button(WIN_W - L - 130, y, 130, 32, "Play", 1, s_game_ok) && s_game_ok)
        *want_play = 1;

    combo_list();
    s_click = 0;
    return s_content_h + FOOTER_H;
}

/* ---- fonts --------------------------------------------------------------- */

#if defined(__APPLE__)
static TTF_Font *open_font(const char *pattern, int pt)
{
    /* Helvetica.ttc: face 0 regular, face 1 bold. Arial as a fallback. */
    int bold = strstr(pattern, "bold") != NULL, size = (int)(pt * s_scale + 0.5f);
    TTF_Font *f = TTF_OpenFontIndex("/System/Library/Fonts/Helvetica.ttc", size, bold ? 1 : 0);
    if (!f)
        f = TTF_OpenFont(bold ? "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
                              : "/System/Library/Fonts/Supplemental/Arial.ttf", size);
    if (!f)
        f = TTF_OpenFont("/System/Library/Fonts/Geneva.ttf", size);
    return f;
}
#else
static TTF_Font *open_font(const char *pattern, int pt)
{
    FcPattern *p = FcNameParse((const FcChar8 *)pattern), *m;
    FcResult r;
    TTF_Font *f = NULL;
    FcConfigSubstitute(NULL, p, FcMatchPattern);
    FcDefaultSubstitute(p);
    m = FcFontMatch(NULL, p, &r);
    if (m) {
        FcChar8 *file = NULL;
        if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch)
            f = TTF_OpenFont((const char *)file, (int)(pt * s_scale + 0.5f));
        FcPatternDestroy(m);
    }
    FcPatternDestroy(p);
    return f;
}
#endif

static int make_fonts(void)
{
    if (s_font) TTF_CloseFont(s_font);
    if (s_bold) TTF_CloseFont(s_bold);
    if (s_title) TTF_CloseFont(s_title);
    s_font = open_font("sans-serif", 14);
    s_bold = open_font("sans-serif:weight=bold", 14);
    s_title = open_font("sans-serif:weight=bold", 24);
    if (!s_bold) s_bold = s_font;
    if (!s_title) s_title = s_bold;
    return s_font != NULL;
}

/* Renderer pixels per window unit: 2 on a HiDPI output with scaling. */
static void update_scale(void)
{
    int ww, wh, rw, rh;
    float sc;
    SDL_GetWindowSize(s_win, &ww, &wh);
    SDL_GetRendererOutputSize(s_ren, &rw, &rh);
    sc = ww > 0 ? (float)rw / ww : 1;
    if (sc < 1)
        sc = 1;
    if (sc != s_scale || !s_font) {
        s_scale = sc;
        make_fonts();
    }
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    char *slash;
    int running = 1, height = 760;
    (void)argc;

#if defined(__APPLE__)
    {
        char exe[PATH_MAX];
        uint32_t sz = sizeof exe;
        if (_NSGetExecutablePath(exe, &sz) != 0 || !realpath(exe, s_dir))
            if (!realpath(argv[0], s_dir))
                snprintf(s_dir, sizeof s_dir, ".");
    }
    if (0)
#else
    if (!realpath("/proc/self/exe", s_dir) && !realpath(argv[0], s_dir))
#endif
        snprintf(s_dir, sizeof s_dir, ".");
    if ((slash = strrchr(s_dir, '/')) != NULL)
        *slash = 0;
    snprintf(s_ini, sizeof s_ini, "%s/launcher.ini", s_dir);
#if defined(__APPLE__)
    /* SSX3.app: settings, keys.ini, logs and F12 marks live in
     * ~/Library/Application Support/SSX3, not inside the (signed, replaced
     * on update) app bundle. */
    if (data_dir_init()) {
        char old_ini[PATH_MAX + 16];
        snprintf(old_ini, sizeof old_ini, "%s", s_ini);
        snprintf(s_ini, sizeof s_ini, "%s/launcher.ini", s_data);
        if (!file_exists(s_ini) && file_exists(old_ini))
            copy_file(old_ini, s_ini);     /* settings from an older build */
    }
#endif

    if (!getenv("SDL_VIDEO_WAYLAND_WMCLASS") && !getenv("SDL_VIDEO_X11_WMCLASS")) {
        setenv("SDL_VIDEO_WAYLAND_WMCLASS", "ssx3-launcher", 1);
        setenv("SDL_VIDEO_X11_WMCLASS", "ssx3-launcher", 1);
        s_set_wmclass = 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "launcher: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#if defined(__APPLE__)
    if (TTF_Init() != 0) {
#else
    if (TTF_Init() != 0 || !FcInit()) {
#endif
        fprintf(stderr, "launcher: font setup failed: %s\n", TTF_GetError());
        return 1;
    }
    s_win = SDL_CreateWindow("SSX 3 Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W, height,
                             SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    s_ren = s_win ? SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!s_ren && s_win)
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    if (!s_ren) {
        fprintf(stderr, "launcher: no window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_BLEND);
    SDL_StopTextInput();
    update_scale();
    if (!s_font) {
        fprintf(stderr, "launcher: no sans-serif font found (fontconfig)\n");
        return 1;
    }

    ini_load();
    load_settings();
    update_pads();
    {
        /* Size the window to the form (one layout pass while hidden), but
         * no taller than the display: the form scrolls inside it. */
        int dummy = 0;
        SDL_Rect usable;
        height = draw_form(&dummy);
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && height > usable.h - 40)
            height = usable.h - 40;
        if (height < WIN_MIN_H)
            height = WIN_MIN_H;
        s_win_h = height;
        SDL_SetWindowMinimumSize(s_win, WIN_MIN_W, WIN_MIN_H);
        SDL_SetWindowSize(s_win, WIN_W, height);
        SDL_SetWindowPosition(s_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(s_win);
    }

    while (running) {
        SDL_Event e;
        int want_play = 0, got = SDL_WaitEventTimeout(&e, 1000);
        if (!got)
            update_pads();              /* battery levels move without events */
        while (got) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE)
                    running = 0;
                else if (e.window.event == SDL_WINDOWEVENT_LEAVE)
                    s_mx = s_my = -1;
                else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GetWindowSize(s_win, &s_win_w, &s_win_h);
                    if (s_win_w < WIN_MIN_W) s_win_w = WIN_MIN_W;
                    s_open = W_NONE;    /* a combo list placed for the old size */
                }
                break;
            case SDL_MOUSEWHEEL:
                if (s_open == W_NONE) {
                    int dy = e.wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                    if (e.wheel.preciseY != 0.0f)
                        dy = (int)(e.wheel.preciseY * 3.0f + (e.wheel.preciseY > 0 ? 0.5f : -0.5f));
#endif
                    if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                        dy = -dy;
                    s_scroll -= dy * 12;
                }
                break;
            case SDL_MOUSEMOTION:
                s_mx = e.motion.x;
                s_my = e.motion.y;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    s_mx = e.button.x;
                    s_my = e.button.y;
                    s_click = 1;
                }
                break;
            case SDL_TEXTINPUT:
                if (s_focus == W_PATH && strlen(s_path) + strlen(e.text.text) < sizeof s_path) {
                    strcat(s_path, e.text.text);
                    update_status();
                }
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    if (s_open != W_NONE)
                        s_open = W_NONE;
                    else if (s_focus != W_NONE) {
                        s_focus = W_NONE;
                        SDL_StopTextInput();
                    } else
                        running = 0;
                } else if (s_focus == W_PATH && e.key.keysym.sym == SDLK_BACKSPACE) {
                    size_t n = strlen(s_path);
                    if (e.key.keysym.mod & KMOD_CTRL)
                        s_path[0] = 0;
                    else if (n)
                        s_path[n - 1] = 0;
                    update_status();
                } else if (s_focus == W_PATH && e.key.keysym.sym == SDLK_v
                           && (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
                    char *clip = SDL_GetClipboardText();
                    if (clip) {
                        clip[strcspn(clip, "\r\n")] = 0;
                        strncat(s_path, clip, sizeof s_path - strlen(s_path) - 1);
                        SDL_free(clip);
                        update_status();
                    }
                } else if ((e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)
                           && s_open == W_NONE && s_game_ok) {
                    want_play = 1;
                }
                break;
            case SDL_DROPFILE:        /* a disc image or a folder dropped on the window */
                if (e.drop.file) {
                    snprintf(s_path, sizeof s_path, "%s", e.drop.file);
                    SDL_free(e.drop.file);
                    update_status();
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                update_pads();
                break;
            default:
                break;
            }
            got = SDL_PollEvent(&e);
        }
        if (!running)
            break;
        update_scale();
        {
            int max_scroll;
            (void)draw_form(&want_play);
            max_scroll = s_content_h - (s_win_h - FOOTER_H);
            if (max_scroll < 0) max_scroll = 0;
            if (s_scroll > max_scroll || s_scroll < 0) {
                /* clamp, and redraw at the clamped position */
                s_scroll = s_scroll < 0 ? 0 : max_scroll;
                (void)draw_form(&want_play);
            }
            SDL_RenderPresent(s_ren);
        }
        if (want_play)
            play();                     /* returns only when the game could not start */
    }
    save_settings();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
