/*
 * Integer window scale (x1, x2 or x3 of the game size, 640x480 or 800x592),
 * chosen from the menu "화면" that the shim puts first on the game's menu bar and
 * kept in rotk4.ini [window] scale (default: the largest that fits). A scale
 * whose window would not fit the monitor's work area is greyed out and the
 * effective scale is the largest one that fits, so the stored choice comes
 * back when the game size or the monitor allows it again.
 *
 * ddraw.ini sets resizable=false for SAN4W95, so the border cannot be dragged.
 * cnc-ddraw draws the scaled picture, scales the mouse (adjmouse) and keeps
 * its own window size in its config; it only takes a new size from the user
 * dragging the border. We play that part: WM_ENTERSIZEMOVE, WM_SIZING with
 * the new window rect, WM_EXITSIZEMOVE, after which cnc-ddraw sets the window
 * to that size and restarts rendering. Applied after each exclusive
 * SetCooperativeLevel (the game's 800x600 choice makes a new DirectDraw
 * object) and on a menu choice; skipped while cnc-ddraw is full screen
 * (Alt+Enter, no caption).
 *
 * cnc-ddraw patches the imports of every module in the game folder, ours
 * included (GetProcAddress too), so GetWindowRect here returns its fake
 * 0,0-based game rect; GetWindowInfo is not hooked and gives the real one.
 * Our window procedure is installed through its SetWindowLongA hook, i.e.
 * between cnc-ddraw's procedure and the game's; cnc-ddraw keeps it there when
 * it re-subclasses the window for a new DirectDraw object.
 *
 * The same menu has "마우스 가두기" (cnc-ddraw's cursor lock in windowed
 * mode). It toggles devmode in ddraw.ini [SAN4W95] and takes effect on the
 * next start: cnc-ddraw reads devmode once at load into a global it does not
 * export, and its window procedure, which runs before ours, drops mouse
 * input while the cursor is not locked unless devmode is set. The patch's
 * ddraw.ini ships devmode=true, so the lock starts off.
 */
#include "shim.h"

#define ID_SCALE1 0x7e51 /* 0x7e51..0x7e53, far from the game's 40001/40005 */
#define MAX_SCALE 3
#define ID_MOUSELOCK 0x7e5f

static HWND main_wnd;
static WNDPROC next_proc;
static int pref = MAX_SCALE; /* the player's choice */
static BOOL menu_added;
static BOOL lock_next; /* cursor lock after the next start (devmode=false) */

static char shim_ini[MAX_PATH], ddraw_ini[MAX_PATH]; /* rotk4.ini, ddraw.ini */

/* devmode as cnc-ddraw sees it: the game section wins over [ddraw]. */
static BOOL devmode_set(void)
{
    char v[16];

    if (!GetPrivateProfileStringA("SAN4W95", "devmode", "", v, sizeof(v), ddraw_ini))
        GetPrivateProfileStringA("ddraw", "devmode", "false", v, sizeof(v), ddraw_ini);
    return v[0] == 't' || v[0] == 'T' || v[0] == '1' || v[0] == 'y' || v[0] == 'Y';
}

/* Window rect for client size cw x ch with the window's current style. */
static void frame_for(int cw, int ch, RECT *r)
{
    SetRect(r, 0, 0, cw, ch);
    AdjustWindowRectEx(r, GetWindowLongA(main_wnd, GWL_STYLE), GetMenu(main_wnd) != NULL,
                       GetWindowLongA(main_wnd, GWL_EXSTYLE));
}

static BOOL work_area(RECT *work)
{
    MONITORINFO mi = { .cbSize = sizeof(mi) };

    if (!GetMonitorInfoA(MonitorFromWindow(main_wnd, MONITOR_DEFAULTTONEAREST), &mi))
        return FALSE;
    *work = mi.rcWork;
    return TRUE;
}

static BOOL fits(int s)
{
    RECT r, work;
    int w, h;

    if (s == 1 || !work_area(&work))
        return s == 1;
    game_size(&w, &h);
    frame_for(w * s, h * s, &r);
    return r.right - r.left <= work.right - work.left && r.bottom - r.top <= work.bottom - work.top;
}

static int effective(void)
{
    int s = pref;

    while (s > 1 && !fits(s))
        s--;
    return s;
}

static void update_menu(void)
{
    HMENU m = GetMenu(main_wnd);
    int s;

    if (!m)
        return;
    for (s = 1; s <= MAX_SCALE; s++)
        EnableMenuItem(m, ID_SCALE1 + s - 1, MF_BYCOMMAND | (fits(s) ? MF_ENABLED : MF_GRAYED));
    s = effective();
    CheckMenuRadioItem(m, ID_SCALE1, ID_SCALE1 + MAX_SCALE - 1, ID_SCALE1 + s - 1, MF_BYCOMMAND);
    CheckMenuItem(m, ID_MOUSELOCK, MF_BYCOMMAND | (lock_next ? MF_CHECKED : MF_UNCHECKED));
}

static void apply(void)
{
    WINDOWINFO wi = { .cbSize = sizeof(wi) };
    RECT frame, work, r;
    int w, h, s = effective(), dx = 0, dy = 0;

    if (!(GetWindowLongA(main_wnd, GWL_STYLE) & WS_CAPTION) || IsIconic(main_wnd)
        || !GetWindowInfo(main_wnd, &wi))
        return;
    game_size(&w, &h);
    frame_for(w * s, h * s, &frame);
    /* Keep the top-left corner, then pull the window back on screen. */
    SetRect(&r, wi.rcWindow.left, wi.rcWindow.top, wi.rcWindow.left + frame.right - frame.left,
            wi.rcWindow.top + frame.bottom - frame.top);
    if (EqualRect(&r, &wi.rcWindow))
        return;
    if (work_area(&work)) {
        if (r.right > work.right)
            dx = work.right - r.right;
        if (r.left + dx < work.left)
            dx = work.left - r.left;
        if (r.bottom > work.bottom)
            dy = work.bottom - r.bottom;
        if (r.top + dy < work.top)
            dy = work.top - r.top;
        OffsetRect(&r, dx, dy);
    }
    SendMessageA(main_wnd, WM_ENTERSIZEMOVE, 0, 0);
    SendMessageA(main_wnd, WM_SIZING, WMSZ_BOTTOMRIGHT, (LPARAM)&r);
    SendMessageA(main_wnd, WM_EXITSIZEMOVE, 0, 0);
    shim_log("rotk4-shim: scale x%d (chosen x%d), game %dx%d, window %ldx%ld at %ld,%ld\n", s, pref,
             w, h, r.right - r.left, r.bottom - r.top, r.left, r.top);
}

static LRESULT CALLBACK scale_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && LOWORD(wp) >= ID_SCALE1 && LOWORD(wp) < ID_SCALE1 + MAX_SCALE) {
        int s = LOWORD(wp) - ID_SCALE1 + 1;
        if (fits(s)) {
            char v[2] = { (char)('0' + s), 0 };
            pref = s;
            WritePrivateProfileStringA("window", "scale", v, shim_ini);
            apply();
        }
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == ID_MOUSELOCK) {
        lock_next = !lock_next;
        WritePrivateProfileStringA("SAN4W95", "devmode", lock_next ? "false" : "true", ddraw_ini);
        shim_log("rotk4-shim: devmode=%s from the next start\n", lock_next ? "false" : "true");
        return 0;
    }
    if (msg == WM_INITMENU)
        update_menu();
    return CallWindowProcA(next_proc, wnd, msg, wp, lp);
}

void scale_attach(HWND wnd)
{
    static BOOL loaded;

    if (!loaded) {
        char dir[MAX_PATH];
        game_dir(dir, sizeof(dir));
        wsprintfA(shim_ini, "%srotk4.ini", dir);
        wsprintfA(ddraw_ini, "%sddraw.ini", dir);
        pref = GetPrivateProfileIntA("window", "scale", MAX_SCALE, shim_ini);
        if (pref < 1 || pref > MAX_SCALE)
            pref = MAX_SCALE;
        lock_next = !devmode_set();
        loaded = TRUE;
    }
    main_wnd = wnd;
    if ((WNDPROC)GetWindowLongA(wnd, GWL_WNDPROC) != scale_proc)
        next_proc = (WNDPROC)SetWindowLongA(wnd, GWL_WNDPROC, (LONG)scale_proc);
    if (!menu_added) {
        HMENU bar = GetMenu(wnd), view = CreatePopupMenu();
        if (bar && view) {
            AppendMenuW(view, MF_STRING, ID_SCALE1, L"x&1");
            AppendMenuW(view, MF_STRING, ID_SCALE1 + 1, L"x&2");
            AppendMenuW(view, MF_STRING, ID_SCALE1 + 2, L"x&3");
            AppendMenuW(view, MF_SEPARATOR, 0, NULL);
            AppendMenuW(view, MF_STRING, ID_MOUSELOCK, L"마우스 가두기 - 다음 실행부터(&M)");
            /* Leftmost, so the game's "도움말" stays last. */
            InsertMenuW(bar, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)view, L"화면(&V)");
            DrawMenuBar(wnd);
            menu_added = TRUE;
        }
    }
    apply();
    update_menu();
}
