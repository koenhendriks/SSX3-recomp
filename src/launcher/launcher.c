/*
 * launcher.exe -- pick the game files and settings, then start the game.
 *
 * The onboarding path: unpack the release, run launcher.exe, point it at
 * the folder of your SSX 3 rip (default.xbe + data\), choose display
 * options, press Play. The choices are kept in launcher.ini next to the
 * launcher and passed to ssx3_recomp.exe as its RECOMP_* settings (the
 * game reads nothing else), so everything here can also be set by hand.
 *
 * Plain Win32 (no runtime to install). Controllers are listed from XInput,
 * refreshed every second; player N is XInput slot N-1, which is how the
 * game assigns them.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define GAME_EXE   "ssx3_recomp.exe"
#define TITLE_ID   0x4541003Cu      /* SSX 3, PAL: what this build was made from */

enum {
    ID_PATH = 100, ID_BROWSE, ID_STATUS,
    ID_RES, ID_ASPECT, ID_MSAA, ID_ANISO, ID_DRAWDIST, ID_SHADOWS, ID_AUDIO, ID_PACING, ID_PACENOTE, ID_HUDEDGES, ID_FULLSCREEN, ID_CRT,
    ID_MBLUR, ID_CAM, ID_FPS, ID_HDTEX,
    ID_SKIPINTRO, ID_FASTBOOT, ID_DEBUGMENU, ID_MONEY, ID_LOGLEVEL,
    ID_PAD0, ID_PAD1, ID_PAD2, ID_PAD3, ID_KEYS,
    ID_LOGS, ID_PLAY,
    ID_TIMER = 1
};

static const struct { const char *label, *value; } k_res[] = {
    {"Auto (your monitor's height)", ""},
    {"480p (original, sharp pixels)", "480"},
    {"720p", "720"},
    {"1080p", "1080"},
    {"1440p", "1440"},
    {"2160p (4K)", "2160"},
};
static const struct { const char *label, *value; } k_aspect[] = {
    {"Auto (follows the window)", "auto"},
    {"16:9", "16:9"},
    {"21:9", "21:9"},
    {"32:9", "32:9"},
    {"4:3 (original)", "0"},
};
static const struct { const char *label, *value; } k_aniso[] = {
    {"Trilinear", "0"},
    {"4x anisotropic", "4"},
    {"8x anisotropic", "8"},
    {"16x anisotropic (sharpest)", "16"},
};
static const struct { const char *label, *value; } k_drawdist[] = {
    {"Original", "1"},
    {"Far (1.5x)", "1.5"},
    {"Maximum (2x)", "2"},
};
static const struct { const char *label, *value; } k_shadows[] = {
    {"Original (128x128)", "1"},
    {"High (512x512)", "4"},
    {"Ultra (1024x1024)", "8"},
};
static const struct { const char *label, *value; } k_audio[] = {
    {"Stereo", "2"},
    {"5.1 surround", "6"},
};
static const struct { const char *label, *value; } k_loglevel[] = {
    {"Errors only", "error"},
    {"Warnings and errors", "warn"},
    {"Normal (what you did, and problems)", "info"},
    {"Detailed (for bug reports)", "debug"},
    {"Everything (large logs)", "trace"},
};
/* Frame pacing (RECOMP_PRESENT_TIMING): auto paces each frame to the
 * display (even on 60/120/240 Hz and under G-Sync/FreeSync); "variable
 * refresh" always spaces frames 1/60 s apart and never locks to the
 * display -- for a VRR display run at 60 Hz, which auto cannot tell from a
 * fixed 60 Hz one; legacy shows each frame as soon as it is ready. */
static const struct { const char *label, *value; } k_pacing[] = {
    {"Auto: paced to the display (recommended)", "auto"},
    {"Variable refresh display (G-Sync/FreeSync)", "timed"},
    {"Legacy: show frames when ready", "legacy"},
};
static const struct { const char *label, *value; } k_msaa[] = {
    {"Off", "0"},
    {"2x MSAA", "2"},
    {"4x MSAA (smooth edges)", "4"},
    {"8x MSAA", "8"},
};

static HINSTANCE s_inst;
static HWND s_wnd;
static HFONT s_font, s_bold, s_title;
static int s_dpi = 96;
static char s_dir[MAX_PATH];        /* the launcher's folder */
static char s_ini[MAX_PATH];
static int s_game_ok;
static HBRUSH s_bg;

static int S(int v) { return MulDiv(v, s_dpi, 96); }

/* ---- game folder check -------------------------------------------------- */

static int file_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
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

/* Fills the status line; returns 1 when the folder can be played. */
static int check_game(const char *dir, char *msg, size_t n)
{
    char xbe[MAX_PATH], data[MAX_PATH], name[48];
    unsigned id = 0;
    if (!*dir) {
        snprintf(msg, n, "Choose the folder of your SSX 3 game files (the one with default.xbe).");
        return 0;
    }
    snprintf(xbe, sizeof xbe, "%s\\default.xbe", dir);
    snprintf(data, sizeof data, "%s\\data", dir);
    if (!file_exists(xbe)) {
        snprintf(msg, n, "No default.xbe in this folder. Pick the folder that holds default.xbe and data\\.");
        return 0;
    }
    if (!read_xbe(xbe, &id, name, sizeof name)) {
        snprintf(msg, n, "default.xbe could not be read (not an Xbox executable?).");
        return 0;
    }
    if (!dir_exists(data)) {
        snprintf(msg, n, "Found %s, but the data\\ folder next to it is missing.", name);
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
    char dir[MAX_PATH], msg[256];
    GetDlgItemTextA(s_wnd, ID_PATH, dir, sizeof dir);
    s_game_ok = check_game(dir, msg, sizeof msg);
    SetDlgItemTextA(s_wnd, ID_STATUS, msg);
    EnableWindow(GetDlgItem(s_wnd, ID_PLAY), s_game_ok);
    InvalidateRect(GetDlgItem(s_wnd, ID_STATUS), NULL, TRUE);
}

static void browse(void)
{
    IFileOpenDialog *dlg = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dlg)))
        return;
    {
        DWORD opt = 0;
        IFileOpenDialog_GetOptions(dlg, &opt);
        IFileOpenDialog_SetOptions(dlg, opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        IFileOpenDialog_SetTitle(dlg, L"Choose the SSX 3 game folder (with default.xbe and data)");
    }
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, s_wnd))) {
        IShellItem *it = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &it))) {
            PWSTR p = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p))) {
                char dir[MAX_PATH], xbe[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, p, -1, dir, sizeof dir, NULL, NULL);
                CoTaskMemFree(p);
                /* Picked the disc's root with the game one level down? Use it. */
                snprintf(xbe, sizeof xbe, "%s\\default.xbe", dir);
                if (!file_exists(xbe)) {
                    char sub[MAX_PATH];
                    snprintf(sub, sizeof sub, "%s\\game\\default.xbe", dir);
                    if (file_exists(sub))
                        strncat(dir, "\\game", sizeof dir - strlen(dir) - 1);
                }
                SetDlgItemTextA(s_wnd, ID_PATH, dir);
                update_status();
            }
            IShellItem_Release(it);
        }
    }
    IFileOpenDialog_Release(dlg);
}

/* ---- controllers --------------------------------------------------------- */

static void update_pads(void)
{
    DWORD i;
    for (i = 0; i < 4; i++) {
        XINPUT_STATE xs;
        char t[160];
        if (XInputGetState(i, &xs) == ERROR_SUCCESS) {
            XINPUT_CAPABILITIES cap;
            XINPUT_BATTERY_INFORMATION bat;
            const char *kind = "Controller", *power = "";
            if (XInputGetCapabilities(i, 0, &cap) == ERROR_SUCCESS) {
                switch (cap.SubType) {
                case XINPUT_DEVSUBTYPE_WHEEL:        kind = "Wheel"; break;
                case XINPUT_DEVSUBTYPE_ARCADE_STICK: kind = "Arcade stick"; break;
                case XINPUT_DEVSUBTYPE_FLIGHT_STICK: kind = "Flight stick"; break;
                case XINPUT_DEVSUBTYPE_DANCE_PAD:    kind = "Dance pad"; break;
                case XINPUT_DEVSUBTYPE_GUITAR:
                case XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE:
                case XINPUT_DEVSUBTYPE_GUITAR_BASS:  kind = "Guitar"; break;
                case XINPUT_DEVSUBTYPE_DRUM_KIT:     kind = "Drum kit"; break;
                default: break;
                }
            }
            if (XInputGetBatteryInformation(i, BATTERY_DEVTYPE_GAMEPAD, &bat) == ERROR_SUCCESS) {
                if (bat.BatteryType == BATTERY_TYPE_WIRED)
                    power = ", wired";
                else if (bat.BatteryType != BATTERY_TYPE_DISCONNECTED && bat.BatteryType != BATTERY_TYPE_UNKNOWN)
                    power = bat.BatteryLevel == BATTERY_LEVEL_FULL ? ", battery full"
                          : bat.BatteryLevel == BATTERY_LEVEL_MEDIUM ? ", battery medium"
                          : bat.BatteryLevel == BATTERY_LEVEL_LOW ? ", battery low" : ", battery empty";
            }
            snprintf(t, sizeof t, "Player %lu:  %s connected%s", (unsigned long)i + 1, kind, power);
        } else if (i == 0) {
            snprintf(t, sizeof t, "Player 1:  keyboard (no controller connected)");
        } else {
            snprintf(t, sizeof t, "Player %lu:  -", (unsigned long)i + 1);
        }
        SetDlgItemTextA(s_wnd, ID_PAD0 + i, t);
    }
}

/* ---- settings ------------------------------------------------------------ */

static int ini_int(const char *key, int def)
{
    return (int)GetPrivateProfileIntA("launcher", key, def, s_ini);
}

static void ini_set_int(const char *key, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    WritePrivateProfileStringA("launcher", key, b, s_ini);
}

static void load_settings(void)
{
    char dir[MAX_PATH] = "";
    GetPrivateProfileStringA("launcher", "game_dir", "", dir, sizeof dir, s_ini);
    if (!*dir) {
        /* First run: a game\ folder next to the launcher, if there is one. */
        char cand[MAX_PATH];
        snprintf(cand, sizeof cand, "%s\\game\\default.xbe", s_dir);
        if (file_exists(cand))
            snprintf(dir, sizeof dir, "%s\\game", s_dir);
    }
    SetDlgItemTextA(s_wnd, ID_PATH, dir);
    SendDlgItemMessageA(s_wnd, ID_RES, CB_SETCURSEL, ini_int("resolution", 0), 0);
    SendDlgItemMessageA(s_wnd, ID_ASPECT, CB_SETCURSEL, ini_int("aspect", 0), 0);
    SendDlgItemMessageA(s_wnd, ID_MSAA, CB_SETCURSEL, ini_int("msaa", 2), 0);
    SendDlgItemMessageA(s_wnd, ID_PACING, CB_SETCURSEL, ini_int("pacing", 0), 0);
    SendDlgItemMessageA(s_wnd, ID_ANISO, CB_SETCURSEL, ini_int("aniso", 3), 0);
    SendDlgItemMessageA(s_wnd, ID_DRAWDIST, CB_SETCURSEL, ini_int("draw_distance", 1), 0);
    SendDlgItemMessageA(s_wnd, ID_SHADOWS, CB_SETCURSEL, ini_int("shadows", 1), 0);
    SendDlgItemMessageA(s_wnd, ID_AUDIO, CB_SETCURSEL, ini_int("audio_output", 0), 0);
    CheckDlgButton(s_wnd, ID_FULLSCREEN, ini_int("fullscreen", 0));
    CheckDlgButton(s_wnd, ID_CRT, ini_int("crt", 0));
    CheckDlgButton(s_wnd, ID_MBLUR, ini_int("motion_blur", 0));
    CheckDlgButton(s_wnd, ID_CAM, ini_int("modern_camera", 0));
    CheckDlgButton(s_wnd, ID_FPS, ini_int("fps_counter", 0));
    CheckDlgButton(s_wnd, ID_HDTEX, ini_int("hd_textures", 1));
    CheckDlgButton(s_wnd, ID_HUDEDGES, ini_int("hud_edges", 1));
    CheckDlgButton(s_wnd, ID_SKIPINTRO, ini_int("skip_intro", 1));
    CheckDlgButton(s_wnd, ID_FASTBOOT, ini_int("fast_boot", 1));
    CheckDlgButton(s_wnd, ID_DEBUGMENU, ini_int("debug_menu", 0));
    CheckDlgButton(s_wnd, ID_MONEY, ini_int("money_cheat", 0));
    SendDlgItemMessageA(s_wnd, ID_LOGLEVEL, CB_SETCURSEL, ini_int("log_level", 2), 0);
    update_status();
}

static void save_settings(void)
{
    char dir[MAX_PATH];
    GetDlgItemTextA(s_wnd, ID_PATH, dir, sizeof dir);
    WritePrivateProfileStringA("launcher", "game_dir", dir, s_ini);
    ini_set_int("resolution", (int)SendDlgItemMessageA(s_wnd, ID_RES, CB_GETCURSEL, 0, 0));
    ini_set_int("aspect", (int)SendDlgItemMessageA(s_wnd, ID_ASPECT, CB_GETCURSEL, 0, 0));
    ini_set_int("msaa", (int)SendDlgItemMessageA(s_wnd, ID_MSAA, CB_GETCURSEL, 0, 0));
    ini_set_int("pacing", (int)SendDlgItemMessageA(s_wnd, ID_PACING, CB_GETCURSEL, 0, 0));
    ini_set_int("aniso", (int)SendDlgItemMessageA(s_wnd, ID_ANISO, CB_GETCURSEL, 0, 0));
    ini_set_int("draw_distance", (int)SendDlgItemMessageA(s_wnd, ID_DRAWDIST, CB_GETCURSEL, 0, 0));
    ini_set_int("shadows", (int)SendDlgItemMessageA(s_wnd, ID_SHADOWS, CB_GETCURSEL, 0, 0));
    ini_set_int("audio_output", (int)SendDlgItemMessageA(s_wnd, ID_AUDIO, CB_GETCURSEL, 0, 0));
    ini_set_int("fullscreen", IsDlgButtonChecked(s_wnd, ID_FULLSCREEN) == BST_CHECKED);
    ini_set_int("crt", IsDlgButtonChecked(s_wnd, ID_CRT) == BST_CHECKED);
    ini_set_int("motion_blur", IsDlgButtonChecked(s_wnd, ID_MBLUR) == BST_CHECKED);
    ini_set_int("modern_camera", IsDlgButtonChecked(s_wnd, ID_CAM) == BST_CHECKED);
    ini_set_int("fps_counter", IsDlgButtonChecked(s_wnd, ID_FPS) == BST_CHECKED);
    ini_set_int("hd_textures", IsDlgButtonChecked(s_wnd, ID_HDTEX) == BST_CHECKED);
    ini_set_int("hud_edges", IsDlgButtonChecked(s_wnd, ID_HUDEDGES) == BST_CHECKED);
    ini_set_int("skip_intro", IsDlgButtonChecked(s_wnd, ID_SKIPINTRO) == BST_CHECKED);
    ini_set_int("fast_boot", IsDlgButtonChecked(s_wnd, ID_FASTBOOT) == BST_CHECKED);
    ini_set_int("debug_menu", IsDlgButtonChecked(s_wnd, ID_DEBUGMENU) == BST_CHECKED);
    ini_set_int("money_cheat", IsDlgButtonChecked(s_wnd, ID_MONEY) == BST_CHECKED);
    ini_set_int("log_level", (int)SendDlgItemMessageA(s_wnd, ID_LOGLEVEL, CB_GETCURSEL, 0, 0));
}

/* ---- launch -------------------------------------------------------------- */

static void play(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], cmd[MAX_PATH + 2];
    int res = (int)SendDlgItemMessageA(s_wnd, ID_RES, CB_GETCURSEL, 0, 0);
    int asp = (int)SendDlgItemMessageA(s_wnd, ID_ASPECT, CB_GETCURSEL, 0, 0);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    snprintf(exe, sizeof exe, "%s\\%s", s_dir, GAME_EXE);
    if (!file_exists(exe)) {
        MessageBoxA(s_wnd, GAME_EXE " was not found next to the launcher.\n"
                    "Keep launcher.exe in the same folder as " GAME_EXE ".",
                    "SSX 3", MB_ICONERROR);
        return;
    }
    save_settings();
    GetDlgItemTextA(s_wnd, ID_PATH, dir, sizeof dir);
    /* The game's settings, inherited through the environment. */
    SetEnvironmentVariableA("RECOMP_GAME_DIR", dir);
    SetEnvironmentVariableA("RECOMP_KEYBOARD", "1");
    SetEnvironmentVariableA("RECOMP_RES", res > 0 && res < (int)(sizeof k_res / sizeof k_res[0])
                                          ? k_res[res].value : NULL);
    SetEnvironmentVariableA("RECOMP_WIDESCREEN", asp >= 0 && asp < (int)(sizeof k_aspect / sizeof k_aspect[0])
                                                 ? k_aspect[asp].value : "auto");
    {
        int pc = (int)SendDlgItemMessageA(s_wnd, ID_PACING, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_PRESENT_TIMING", pc >= 0 && pc < (int)(sizeof k_pacing / sizeof k_pacing[0])
                                                         ? k_pacing[pc].value : "auto");
        int aa = (int)SendDlgItemMessageA(s_wnd, ID_MSAA, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_MSAA", aa >= 0 && aa < (int)(sizeof k_msaa / sizeof k_msaa[0])
                                               ? k_msaa[aa].value : NULL);
        aa = (int)SendDlgItemMessageA(s_wnd, ID_ANISO, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_ANISO", aa >= 0 && aa < (int)(sizeof k_aniso / sizeof k_aniso[0])
                                                ? k_aniso[aa].value : NULL);
        aa = (int)SendDlgItemMessageA(s_wnd, ID_DRAWDIST, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_DRAW_DIST", aa >= 0 && aa < (int)(sizeof k_drawdist / sizeof k_drawdist[0])
                                                    ? k_drawdist[aa].value : NULL);
        aa = (int)SendDlgItemMessageA(s_wnd, ID_SHADOWS, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_SHADOW_RES", aa >= 0 && aa < (int)(sizeof k_shadows / sizeof k_shadows[0])
                                                     ? k_shadows[aa].value : NULL);
        aa = (int)SendDlgItemMessageA(s_wnd, ID_AUDIO, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_AUDIO_CHANNELS", aa >= 0 && aa < (int)(sizeof k_audio / sizeof k_audio[0])
                                                         ? k_audio[aa].value : NULL);
    }
    SetEnvironmentVariableA("RECOMP_FULLSCREEN", IsDlgButtonChecked(s_wnd, ID_FULLSCREEN) ? "1" : NULL);
    SetEnvironmentVariableA("RECOMP_CRT", IsDlgButtonChecked(s_wnd, ID_CRT) ? "1" : NULL);
    SetEnvironmentVariableA("RECOMP_MBLUR", IsDlgButtonChecked(s_wnd, ID_MBLUR) ? "0.5" : NULL);
    SetEnvironmentVariableA("RECOMP_CAM", IsDlgButtonChecked(s_wnd, ID_CAM) ? "1" : NULL);
    SetEnvironmentVariableA("RECOMP_FPS_OVERLAY", IsDlgButtonChecked(s_wnd, ID_FPS) ? "1" : NULL);
    SetEnvironmentVariableA("RECOMP_HDTEX", IsDlgButtonChecked(s_wnd, ID_HDTEX) ? NULL : "0");
    SetEnvironmentVariableA("RECOMP_HUD_EDGES", IsDlgButtonChecked(s_wnd, ID_HUDEDGES) ? NULL : "0");
    SetEnvironmentVariableA("RECOMP_SKIP_INTRO", IsDlgButtonChecked(s_wnd, ID_SKIPINTRO) ? "1" : "0");
    SetEnvironmentVariableA("RECOMP_FAST_BOOT", IsDlgButtonChecked(s_wnd, ID_FASTBOOT) ? NULL : "0");
    SetEnvironmentVariableA("RECOMP_DEBUG_MENU", IsDlgButtonChecked(s_wnd, ID_DEBUGMENU) ? "1" : NULL);
    SetEnvironmentVariableA("RECOMP_MONEY_CHEAT", IsDlgButtonChecked(s_wnd, ID_MONEY) ? "1" : NULL);
    {
        int ll = (int)SendDlgItemMessageA(s_wnd, ID_LOGLEVEL, CB_GETCURSEL, 0, 0);
        SetEnvironmentVariableA("RECOMP_LOG_LEVEL", ll >= 0 && ll < (int)(sizeof k_loglevel / sizeof k_loglevel[0])
                                                    ? k_loglevel[ll].value : NULL);
    }

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    snprintf(cmd, sizeof cmd, "\"%s\"", exe);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, s_dir, &si, &pi)) {
        char m[256];
        snprintf(m, sizeof m, "Could not start " GAME_EXE " (error %lu).", GetLastError());
        MessageBoxA(s_wnd, m, "SSX 3", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DestroyWindow(s_wnd);
}

static void open_logs(void)
{
    /* The game writes its log to logs\ beside the game folder when it is
     * not started from a console (main.c). */
    char dir[MAX_PATH], logs[MAX_PATH], *slash;
    GetDlgItemTextA(s_wnd, ID_PATH, dir, sizeof dir);
    GetFullPathNameA(*dir ? dir : s_dir, sizeof logs, logs, NULL);
    if (*dir && (slash = strrchr(logs, '\\')) != NULL)
        *slash = 0;
    strncat(logs, "\\logs", sizeof logs - strlen(logs) - 1);
    if (!dir_exists(logs)) {
        MessageBoxA(s_wnd, "No logs yet: they appear after the game has run once.", "SSX 3",
                    MB_ICONINFORMATION);
        return;
    }
    {
        /* the folder with this session's file selected (logs\latest.txt) */
        char lp[MAX_PATH + 16], name[MAX_PATH], arg[2 * MAX_PATH];
        FILE *f;
        size_t n = 0;
        snprintf(lp, sizeof lp, "%s\\latest.txt", logs);
        if ((f = fopen(lp, "r")) != NULL) {
            if (fgets(name, sizeof name, f))
                n = strcspn(name, "\r\n");
            fclose(f);
        }
        if (n) {
            name[n] = 0;
            snprintf(arg, sizeof arg, "/select,\"%s\\%s\"", logs, name);
            if ((INT_PTR)ShellExecuteA(s_wnd, "open", "explorer.exe", arg, NULL, SW_SHOWNORMAL) > 32)
                return;
        }
    }
    ShellExecuteA(s_wnd, "open", logs, NULL, NULL, SW_SHOWNORMAL);
}

/* ---- window -------------------------------------------------------------- */

static HWND add(const char *cls, const char *text, DWORD style, int id, int x, int y, int w, int h, HFONT f)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                             s_wnd, (HMENU)(INT_PTR)id, s_inst, NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static void make_fonts(void)
{
    NONCLIENTMETRICSA m;
    LOGFONTA lf;
    m.cbSize = sizeof m;
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    lf = m.lfMessageFont;
    lf.lfHeight = -MulDiv(9, s_dpi, 72);
    s_font = CreateFontIndirectA(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    s_bold = CreateFontIndirectA(&lf);
    lf.lfHeight = -MulDiv(16, s_dpi, 72);
    lf.lfWeight = FW_BOLD;
    s_title = CreateFontIndirectA(&lf);
}

/* What the display the launcher is on means for 60 FPS: even on a multiple
 * of 60 Hz; uneven on other rates unless variable refresh is on (the
 * launcher cannot see whether it is). */
static void pacing_note(void)
{
    DEVMODEA dm;
    MONITORINFOEXA mi;
    char text[200];
    int hz = 0, mult;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (GetMonitorInfoA(MonitorFromWindow(s_wnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi)
        && EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        hz = (int)dm.dmDisplayFrequency;
    mult = hz > 0 ? (hz + 30) / 60 : 0;
    if (hz <= 0)
        text[0] = 0;
    else if (mult >= 1 && abs(hz - mult * 60) <= hz / 100 + 1)
        snprintf(text, sizeof text, "This display runs at %d Hz: 60 FPS is shown evenly.%s", hz,
                 hz <= 61 ? " With G-Sync/FreeSync on, pick Variable refresh display." : "");
    else
        snprintf(text, sizeof text, "This display runs at %d Hz: 60 FPS is shown unevenly unless "
                 "G-Sync/FreeSync is on. Otherwise use 60 or 120 Hz.", hz);
    SetDlgItemTextA(s_wnd, ID_PACENOTE, text);
}

static void build(void)
{
    int y = 14, i;
    HWND cb;
    char auto_res[96];
    add("STATIC", "SSX 3", 0, -1, 20, y, 300, 30, s_title);
    add("STATIC", "static recompilation for Windows", 0, -1, 104, y + 11, 300, 18, s_font);
    y += 44;

    add("STATIC", "Game files", 0, -1, 20, y, 400, 18, s_bold);
    y += 22;
    add("EDIT", "", WS_BORDER | ES_AUTOHSCROLL, ID_PATH, 20, y, 382, 24, s_font);
    add("BUTTON", "Browse...", BS_PUSHBUTTON | WS_TABSTOP, ID_BROWSE, 410, y - 1, 90, 26, s_font);
    y += 30;
    add("STATIC", "", 0, ID_STATUS, 20, y, 480, 34, s_font);
    y += 40;

    add("STATIC", "Display", 0, -1, 20, y, 400, 18, s_bold);
    y += 24;
    add("STATIC", "Resolution", 0, -1, 20, y + 4, 110, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_RES, 140, y, 260, 200, s_font);
    snprintf(auto_res, sizeof auto_res, "Auto (your monitor: %dp)", GetSystemMetrics(SM_CYSCREEN));
    for (i = 0; i < (int)(sizeof k_res / sizeof k_res[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)(i ? k_res[i].label : auto_res));
    y += 30;
    add("STATIC", "Aspect ratio", 0, -1, 20, y + 4, 110, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_ASPECT, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_aspect / sizeof k_aspect[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_aspect[i].label);
    y += 30;
    add("STATIC", "Anti-aliasing", 0, -1, 20, y + 4, 110, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_MSAA, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_msaa / sizeof k_msaa[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_msaa[i].label);
    y += 30;
    add("STATIC", "Texture filtering", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_ANISO, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_aniso / sizeof k_aniso[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_aniso[i].label);
    y += 30;
    add("STATIC", "Draw distance", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_DRAWDIST, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_drawdist / sizeof k_drawdist[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_drawdist[i].label);
    y += 30;
    add("STATIC", "Shadow quality", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_SHADOWS, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_shadows / sizeof k_shadows[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_shadows[i].label);
    y += 30;
    add("STATIC", "Audio output", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_AUDIO, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_audio / sizeof k_audio[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_audio[i].label);
    y += 30;
    add("STATIC", "Frame pacing", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_PACING, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_pacing / sizeof k_pacing[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_pacing[i].label);
    SendMessageA(cb, CB_SETCURSEL, 0, 0);
    y += 28;
    add("STATIC", "", 0, ID_PACENOTE, 140, y, 360, 34, s_font);
    pacing_note();
    y += 40;
    add("BUTTON", "Fullscreen (F11 toggles in game)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FULLSCREEN, 20, y, 300, 22, s_font);
    y += 24;
    add("BUTTON", "CRT TV shader (needs a resolution above 480p)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CRT, 20, y, 400, 22, s_font);
    y += 24;
    add("BUTTON", "Motion blur (riders stay sharp)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_MBLUR, 20, y, 300, 22, s_font);
    y += 24;
    add("BUTTON", "Modern camera (smoother follow, speed FOV, shake)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CAM, 20, y, 400, 22, s_font);
    y += 24;
    add("BUTTON", "FPS counter (F3 toggles in game)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FPS, 20, y, 300, 22, s_font);
    y += 24;
    add("BUTTON", "HD textures (Bl4ckH4nd's HD texture pack)",
        BS_AUTOCHECKBOX | WS_TABSTOP, ID_HDTEX, 20, y, 480, 22, s_font);
    y += 24;
    add("BUTTON", "HUD at the screen edges (widescreen)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_HUDEDGES,
        20, y, 480, 22, s_font);
    y += 34;

    add("STATIC", "Game", 0, -1, 20, y, 400, 18, s_bold);
    y += 24;
    add("BUTTON", "Skip the intro movies", BS_AUTOCHECKBOX | WS_TABSTOP, ID_SKIPINTRO, 20, y, 300, 22, s_font);
    y += 24;
    add("BUTTON", "Fast boot (shorter title and hard-disk-check waits)", BS_AUTOCHECKBOX | WS_TABSTOP,
        ID_FASTBOOT, 20, y, 480, 22, s_font);
    y += 24;
    add("BUTTON", "Debug mode: EA's quick-start menu (press Start on the title screen)",
        BS_AUTOCHECKBOX | WS_TABSTOP, ID_DEBUGMENU, 20, y, 480, 22, s_font);
    y += 24;
    add("BUTTON", "Testing: Home key gives player 1 200,000 money", BS_AUTOCHECKBOX | WS_TABSTOP,
        ID_MONEY, 20, y, 480, 22, s_font);
    y += 28;
    add("STATIC", "Log detail", 0, -1, 20, y + 4, 115, 20, s_font);
    cb = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_LOGLEVEL, 140, y, 260, 200, s_font);
    for (i = 0; i < (int)(sizeof k_loglevel / sizeof k_loglevel[0]); i++)
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)k_loglevel[i].label);
    SendMessageA(cb, CB_SETCURSEL, 2, 0);
    y += 38;

    add("STATIC", "Controllers", 0, -1, 20, y, 400, 18, s_bold);
    y += 22;
    for (i = 0; i < 4; i++, y += 21)
        add("STATIC", "", 0, ID_PAD0 + i, 20, y, 480, 20, s_font);
    add("STATIC", "Plug in controllers before pressing Play; two or more enable Multi Play. "
        "The keyboard always works as player 1 (keys.ini).", 0, ID_KEYS, 20, y + 2, 480, 34, s_font);
    y += 46;

    add("BUTTON", "Open logs", BS_PUSHBUTTON | WS_TABSTOP, ID_LOGS, 20, y, 100, 30, s_font);
    add("BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_PLAY, 380, y, 120, 30, s_bold);
    y += 46;
    {
        RECT r = {0, 0, S(520), S(y)};
        AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
        SetWindowPos(s_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_BROWSE: browse(); return 0;
        case ID_PATH:
            if (HIWORD(w) == EN_CHANGE)
                update_status();
            return 0;
        case ID_PLAY: if (s_game_ok) play(); return 0;
        case ID_LOGS: open_logs(); return 0;
        }
        break;
    case WM_TIMER:
        update_pads();
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkMode(dc, TRANSPARENT);
        if ((HWND)l == GetDlgItem(h, ID_STATUS))
            SetTextColor(dc, s_game_ok ? RGB(0, 120, 40) : RGB(190, 40, 30));
        return (LRESULT)s_bg;
    }
    case WM_CLOSE:
        save_settings();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSA wc;
    MSG msg;
    char *slash;
    HDC dc;
    (void)prev; (void)cmdline;

    s_inst = inst;
    {
        /* Sharp text on high-DPI screens. */
        typedef BOOL (WINAPI *sdpi_t)(HANDLE);
        sdpi_t f = (sdpi_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (!f || !f((HANDLE)-4))       /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            SetProcessDPIAware();
    }
    dc = GetDC(NULL);
    s_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControls();

    GetModuleFileNameA(NULL, s_dir, sizeof s_dir);
    if ((slash = strrchr(s_dir, '\\')) != NULL)
        *slash = 0;
    snprintf(s_ini, sizeof s_ini, "%s\\launcher.ini", s_dir);

    s_bg = GetSysColorBrush(COLOR_WINDOW);
    make_fonts();
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = s_bg;
    wc.lpszClassName = "SSX3Launcher";
    RegisterClassA(&wc);
    s_wnd = CreateWindowExA(0, wc.lpszClassName, "SSX 3 Launcher",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, S(520), S(600), NULL, NULL, inst, NULL);
    if (!s_wnd)
        return 1;
    build();
    load_settings();
    update_pads();
    SetTimer(s_wnd, ID_TIMER, 1000, NULL);
    {
        /* centre on the screen */
        RECT r, wa;
        GetWindowRect(s_wnd, &r);
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(s_wnd, NULL, wa.left + ((wa.right - wa.left) - (r.right - r.left)) / 2,
                     wa.top + ((wa.bottom - wa.top) - (r.bottom - r.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(s_wnd, show);
    SetFocus(GetDlgItem(s_wnd, s_game_ok ? ID_PLAY : ID_BROWSE));
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    CoUninitialize();
    return 0;
}
