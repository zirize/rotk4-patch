/*
 * ddraw.dll shim for SAN4W95.EXE (Romance of the Three Kingdoms IV, Korean Win95).
 *
 * Sits in front of cnc-ddraw (renamed to cnc-ddraw.dll) and restores two things
 * the game took for granted from a Win95 desktop running in 256 colours:
 *
 *  1. The game never calls SetDisplayMode; it assumes the desktop already is
 *     8 bpp. On a 32 bpp desktop the primary surface is 32 bpp and
 *     SetPalette fails with DDERR_INVALIDPIXELFORMAT, so the game quits.
 *     We set an 8 bpp mode of the game's own size (640x480, or 800x592 after
 *     the player picks 800x600) right after an exclusive SetCooperativeLevel.
 *
 *  2. Win95 DirectDraw showed and activated the window on entering exclusive
 *     mode. The game creates its window hidden and only proceeds once
 *     WM_ACTIVATEAPP has set its "active" flag (0x543e94 bit 0). We show and
 *     activate the window ourselves.
 *
 *  3. The game sizes its window from the desktop: 640 x (SM_CYSCREEN -
 *     SM_CYCAPTION - SM_CYMENU), rounded down to 16 lines if under 480
 *     (0x490e3d, 0x401648). On a 640x480 screen that is 640x400, while
 *     cnc-ddraw scales the mouse for 640x480, so clicks drift vertically.
 *     We report a screen tall enough for that (see shim_GetSystemMetrics),
 *     and at least 800 wide so the game offers its 640x480 / 800x600 choice
 *     after the opening (0x4583c0); 800 mode wants 592 lines.
 *
 *  4. The game draws into the primary surface at its client area's screen
 *     position, taken from WM_MOVE (0x492999, used at 0x493453). cnc-ddraw
 *     maps the primary to the client area and swallows later WM_MOVEs, so
 *     the game keeps the position from window creation (below the caption
 *     and menu) and draws that far down. We tell it the client is at 0,0.
 *
 * After that it fits the window to the chosen integer scale (scale.c).
 * It also remembers the game's DirectDraw palette so movie.c can swap colours
 * while a movie plays.
 */
#include "shim.h"
#include <string.h>

/* IDirectDraw (v1) vtable slots used here. */
enum {
    DD_CREATEPALETTE = 5,
    DD_SETCOOPERATIVELEVEL = 20,
    DD_SETDISPLAYMODE = 21,
    DD_VTBL_SIZE = 23
};

#define DDSCL_FULLSCREEN 0x00000001
#define DDSCL_EXCLUSIVE 0x00000010
#define DDPCAPS_8BIT 0x00000004

typedef HRESULT(WINAPI *DirectDrawCreate_t)(GUID *, void **, void *);
typedef HRESULT(WINAPI *SetCooperativeLevel_t)(void *, HWND, DWORD);
typedef HRESULT(WINAPI *SetDisplayMode_t)(void *, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI *CreatePalette_t)(void *, DWORD, PALETTEENTRY *, void **, void *);
/* IDirectDrawPalette slots 4 and 6. */
typedef HRESULT(WINAPI *PalEntries_t)(void *, DWORD, DWORD, DWORD, PALETTEENTRY *);
enum { DDP_GETENTRIES = 4, DDP_SETENTRIES = 6 };

static HMODULE real_dll;
static DirectDrawCreate_t real_DirectDrawCreate;
static void *real_vtbl[DD_VTBL_SIZE];
static void *shim_vtbl[DD_VTBL_SIZE];

static WNDPROC game_wndproc;

/* Set by DirectDrawCreate: the game creates a new DirectDraw object after the
 * opening, and cnc-ddraw starts each one in the desktop's 32 bpp mode. */
static BOOL mode_pending;

typedef int(WINAPI *GetSystemMetrics_t)(int);
static GetSystemMetrics_t real_GetSystemMetrics;

static void *game_palette; /* last 256-entry palette the game created */
static PALETTEENTRY saved_entries[256];

/*
 * The game treats WM_DISPLAYCHANGE to a size other than the one it recorded
 * at startup as "resolution changed": it minimises itself and pauses
 * (0x543e94 bits 1 and 2). Our own SetDisplayMode must not trigger that.
 */
static LRESULT CALLBACK swallow_displaychange(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DISPLAYCHANGE)
        return 0;
    return CallWindowProcA(game_wndproc, hwnd, msg, wp, lp);
}

/* Window size the game is using: 0x520bb0 / 0x5229dc, set by 0x490cd0 before it
 * (re)creates DirectDraw. Falls back to 640x480 if they look wrong. */
void game_size(int *w, int *h)
{
    *w = *(volatile int *)0x520bb0;
    *h = *(volatile int *)0x5229dc;
    if (*w < 640 || *w > 800 || *h < 400 || *h > 600) {
        *w = 640;
        *h = 480;
    }
}

static HRESULT WINAPI shim_SetCooperativeLevel(void *self, HWND hwnd, DWORD flags)
{
    static BOOL busy;
    HRESULT hr;

    /* Before cnc-ddraw subclasses the window and stops passing WM_MOVE on. */
    if (hwnd && !busy)
        SendMessageA(hwnd, WM_MOVE, 0, MAKELPARAM(0, 0));
    hr = ((SetCooperativeLevel_t)real_vtbl[DD_SETCOOPERATIVELEVEL])(self, hwnd, flags);
    cdaudio_rehook();
    shim_log("rotk4-shim: SetCooperativeLevel(%p, 0x%lx) = 0x%lx\n", hwnd, flags, hr);
    /* Activating the window below re-enters here from the game's WM_ACTIVATEAPP handler. */
    if (busy || FAILED(hr) || (flags & (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE)) != (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE))
        return hr;
    busy = TRUE;

    if (mode_pending) {
        HRESULT mode;
        int w, h;
        game_size(&w, &h);
        if (hwnd)
            game_wndproc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)swallow_displaychange);
        mode = ((SetDisplayMode_t)real_vtbl[DD_SETDISPLAYMODE])(self, w, h, 8);
        if (hwnd)
            SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)game_wndproc);
        mode_pending = FAILED(mode);
        shim_log("rotk4-shim: SetDisplayMode(%d, %d, 8) = 0x%lx\n", w, h, mode);
    }

    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);
        scale_attach(hwnd);
    }
    shim_log("rotk4-shim: active=%p\n", GetActiveWindow());

    busy = FALSE;
    return hr;
}

/* Desktops were larger than the game window: report at least 800 wide and room
 * for 592 client lines (the 800x600 choice; 640 mode still caps at 480). */
static int WINAPI shim_GetSystemMetrics(int index)
{
    int value = real_GetSystemMetrics(index);
    int need = 0;

    if (index == SM_CXSCREEN)
        need = 800;
    else if (index == SM_CYSCREEN)
        need = 592 + real_GetSystemMetrics(SM_CYCAPTION) + real_GetSystemMetrics(SM_CYMENU);
    if (value < need) {
        static BOOL logged[2];
        if (!logged[index == SM_CYSCREEN])
            shim_log("rotk4-shim: GetSystemMetrics(%d) %d -> %d\n", index, value, need);
        logged[index == SM_CYSCREEN] = TRUE;
        value = need;
    }
    return value;
}

/* Called with the MCI re-hook: cnc-ddraw may put its own hook back in the IAT. */
void ddraw_rehook(void)
{
    void *old = iat_hook("user32.dll", "GetSystemMetrics", (void *)shim_GetSystemMetrics);
    if (old && old != (void *)shim_GetSystemMetrics)
        real_GetSystemMetrics = (GetSystemMetrics_t)old;
}

static HRESULT WINAPI shim_CreatePalette(void *self, DWORD flags, PALETTEENTRY *entries, void **pal, void *outer)
{
    HRESULT hr = ((CreatePalette_t)real_vtbl[DD_CREATEPALETTE])(self, flags, entries, pal, outer);
    if (SUCCEEDED(hr) && pal && *pal && (flags & DDPCAPS_8BIT))
        game_palette = *pal;
    return hr;
}

static HRESULT pal_call(int slot, PALETTEENTRY *entries)
{
    void **vtbl = *(void ***)game_palette;
    return ((PalEntries_t)vtbl[slot])(game_palette, 0, 0, 256, entries);
}

/* Load `entries` into the game's palette, saving its colours; NULL puts them back. */
BOOL ddraw_movie_palette(const PALETTEENTRY *entries)
{
    if (!game_palette)
        return FALSE;
    if (!entries)
        return SUCCEEDED(pal_call(DDP_SETENTRIES, saved_entries));
    if (FAILED(pal_call(DDP_GETENTRIES, saved_entries)))
        return FALSE;
    return SUCCEEDED(pal_call(DDP_SETENTRIES, (PALETTEENTRY *)entries));
}

/* Swap the object's vtable pointer for our copy with our methods in it. */
static void hook_ddraw(void *dd)
{
    void ***obj = (void ***)dd;
    if (!shim_vtbl[0]) {
        memcpy(real_vtbl, *obj, sizeof(real_vtbl));
        memcpy(shim_vtbl, *obj, sizeof(shim_vtbl));
        shim_vtbl[DD_SETCOOPERATIVELEVEL] = (void *)shim_SetCooperativeLevel;
        shim_vtbl[DD_CREATEPALETTE] = (void *)shim_CreatePalette;
    }
    *obj = shim_vtbl;
}

static BOOL load_real(void)
{
    char path[MAX_PATH];

    if (real_DirectDrawCreate)
        return TRUE;
    /* Load from the game directory, next to this DLL. */
    game_dir(path, sizeof(path));
    lstrcatA(path, "cnc-ddraw.dll");
    real_dll = LoadLibraryA(path);
    if (!real_dll) {
        shim_log("rotk4-shim: cannot load %s (%lu)\n", path, GetLastError());
        return FALSE;
    }
    real_DirectDrawCreate = (DirectDrawCreate_t)GetProcAddress(real_dll, "DirectDrawCreate");
    return real_DirectDrawCreate != NULL;
}

HRESULT WINAPI DirectDrawCreate(GUID *guid, void **dd, void *outer)
{
    HRESULT hr;

    if (!load_real())
        return E_FAIL;
    hr = real_DirectDrawCreate(guid, dd, outer);
    shim_log("rotk4-shim: DirectDrawCreate = 0x%lx\n", hr);
    if (SUCCEEDED(hr) && dd && *dd) {
        hook_ddraw(*dd);
        mode_pending = TRUE;
    }
    cdaudio_rehook();
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        /*
         * Load cnc-ddraw now rather than at DirectDrawCreate: the game checks
         * GetDeviceCaps for a 256-colour desktop before it ever touches
         * DirectDraw, and cnc-ddraw's fake_mode hooks must already be in place.
         * Fix the saved window placement first: cnc-ddraw reads it on load.
         */
        winpos_check();
        load_real();
        ddraw_rehook();
        cdaudio_init();
    }
    return TRUE;
}
