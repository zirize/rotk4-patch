/*
 * Window placement reset.
 *
 * cnc-ddraw saves the window position and size on exit (savesettings) and
 * restores them on the next start, even when the game folder has moved to
 * another PC or the monitor changed: the window can then open off screen or
 * larger than the desktop. Before cnc-ddraw loads (it reads ddraw.ini in its
 * DllMain) we check the saved values against the desktop and, if they no
 * longer fit, the desktop resolution changed since the last start, or Shift
 * is held at startup, put back a centred window at the largest integer
 * multiple of 640x480 that fits the work area; scale.c then fits it to the
 * chosen scale once the game sets its mode.
 *
 * cnc-ddraw is DPI aware and stores physical pixels; this DLL runs before it
 * and may see scaled metrics, so everything is converted by the ratio of the
 * real mode width to SM_CXSCREEN.
 */
#include "shim.h"
#include <stdlib.h>

#define CENTER (-32000)

static int ini_int(const char *sec, const char *key, int def, const char *ini)
{
    char buf[32];

    if (!GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini) || !buf[0])
        return def;
    return atoi(buf);
}

static void ini_put(const char *sec, const char *key, int val, const char *ini)
{
    char buf[16];

    wsprintfA(buf, "%d", val);
    WritePrivateProfileStringA(sec, key, buf, ini);
}

void winpos_check(void)
{
    char dir[MAX_PATH], ini[MAX_PATH], shim_ini[MAX_PATH], desk[32], last[32];
    const char *sec = "ddraw";
    DEVMODEA dm = { 0 };
    RECT work;
    int save, w, h, x, y, fw, fh, scale_num, cx, avail_w, avail_h, k;
    BOOL reset;

    game_dir(dir, sizeof(dir));
    wsprintfA(ini, "%sddraw.ini", dir);
    wsprintfA(shim_ini, "%srotk4.ini", dir);
    save = ini_int("ddraw", "savesettings", 0, ini);
    if (save == 2)
        sec = "SAN4W95"; /* game section, the EXE base name */

    dm.dmSize = sizeof(dm);
    cx = GetSystemMetrics(SM_CXSCREEN);
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) || !cx
        || !SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0))
        return;
    scale_num = (int)dm.dmPelsWidth; /* physical = logical * scale_num / cx */
#define PHYS(v) MulDiv((v), scale_num, cx)
    /* Frame around the client area: borders, caption and menu. */
    fw = PHYS(2 * GetSystemMetrics(SM_CXSIZEFRAME));
    fh = PHYS(2 * GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION)
              + GetSystemMetrics(SM_CYMENU));
    avail_w = PHYS(work.right - work.left) - fw;
    avail_h = PHYS(work.bottom - work.top) - fh;

    w = ini_int(sec, "width", 0, ini);
    h = ini_int(sec, "height", 0, ini);
    x = ini_int(sec, "posX", CENTER, ini);
    y = ini_int(sec, "posY", CENTER, ini);

    wsprintfA(desk, "%lux%lu", dm.dmPelsWidth, dm.dmPelsHeight);
    GetPrivateProfileStringA("window", "desktop", "", last, sizeof(last), shim_ini);
    reset = (GetAsyncKeyState(VK_SHIFT) & 0x8000) || lstrcmpA(desk, last)
            || w > avail_w || h > avail_h
            || (x != CENTER && (x < PHYS(work.left) || x + w + fw > PHYS(work.right)))
            || (y != CENTER && (y < PHYS(work.top) || y + h + fh > PHYS(work.bottom)));
#undef PHYS
    WritePrivateProfileStringA("window", "desktop", desk, shim_ini);
    if (!reset)
        return;

    /* Largest integer k * 640x480 (up to 3, as scale.c), at least 1. */
    k = min(min(avail_w / 640, avail_h / 480), 3);
    if (k < 1)
        k = 1;
    ini_put(sec, "width", 640 * k, ini);
    ini_put(sec, "height", 480 * k, ini);
    ini_put(sec, "posX", CENTER, ini);
    ini_put(sec, "posY", CENTER, ini);
    shim_log("rotk4-shim: window reset to %dx%d centred (desktop %s, was %dx%d at %d,%d)\n",
             640 * k, 480 * k, desk, w, h, x, y);
}
