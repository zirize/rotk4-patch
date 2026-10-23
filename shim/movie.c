/*
 * Movie (MCI avivideo) fixes.
 *
 * The game plays its AVIs (Cinepak, 24 bpp, 320x240) through MCIAVI in a
 * WS_CHILD window of its main window, sized by the game with MoveWindow. It
 * never touches the palette for a movie: on Win95 in 256 colours MCIAVI
 * realised its own palette, which replaced the hardware palette under the
 * game's DirectDraw palette until the game re-applied it after the movie.
 *
 * cnc-ddraw (fixchilds) redraws the MCIAVI window into the 8 bpp primary
 * surface, so GDI maps the frames to the game's palette of the moment and the
 * colours come out wrong. While a movie plays we load a palette made from
 * that AVI (palette.c) into the game's DirectDraw palette and put the game's
 * colours back when the device closes.
 *
 * Before PLAY the movie window is also resized to fill the screen and the
 * picture stretched over it (see fit_to_screen).
 */
#include "shim.h"
#include <mmsystem.h>
#include <digitalv.h>

#define NO_DEVICE ((MCIDEVICEID)-1)

static MCIDEVICEID avi_id = NO_DEVICE;
static BOOL palette_swapped;
static char avi_path[MAX_PATH];

static BOOL is_avi_open(DWORD flags, const MCI_OPEN_PARMSA *parms)
{
    const char *name;
    int len;

    if (!parms)
        return FALSE;
    if ((flags & MCI_OPEN_TYPE) && !(flags & MCI_OPEN_TYPE_ID) && parms->lpstrDeviceType
        && !lstrcmpiA(parms->lpstrDeviceType, "avivideo"))
        return TRUE;
    if (!(flags & MCI_OPEN_ELEMENT) || !(name = parms->lpstrElementName))
        return FALSE;
    len = lstrlenA(name);
    return len > 4 && !lstrcmpiA(name + len - 4, ".avi");
}

/*
 * Fill the game screen (640x480, or 800x592 in the 800x600 mode). The game
 * sizes the movie window to the desktop height minus caption and menu
 * (0x490e3d), so on the 1024x768 desktops it was meant for the opening fills
 * 640x480 at 2x; with the screen we report it would come out at a different
 * size. Then stretch the picture over the window, as Win95 MCIAVI did.
 */
static void fit_to_screen(mciSendCommandA_t send, MCIDEVICEID id)
{
    MCI_DGV_STATUS_PARMSA status = { 0 };
    MCI_DGV_PUT_PARMS put = { 0 };
    MCIERROR err;
    int w, h;

    status.dwItem = MCI_DGV_STATUS_HWND;
    if (send(id, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status))
        return;
    game_size(&w, &h);
    MoveWindow((HWND)status.dwReturn, 0, 0, w, h, TRUE);
    /* MCI rectangles are left, top, width, height. */
    put.rc.right = w;
    put.rc.bottom = h;
    err = send(id, MCI_PUT, MCI_DGV_PUT_DESTINATION | MCI_DGV_RECT, (DWORD_PTR)&put);
    shim_log("rotk4-shim: movie: fit to screen = %lu\n", err);
}

/* Fallback if the AVI cannot be decoded: 6x6x6 cube (0..215) and a grey ramp. */
static void cube_palette(PALETTEENTRY *pal)
{
    int i;

    for (i = 0; i < 216; i++) {
        pal[i].peRed = (BYTE)(i / 36 * 51);
        pal[i].peGreen = (BYTE)(i / 6 % 6 * 51);
        pal[i].peBlue = (BYTE)(i % 6 * 51);
        pal[i].peFlags = 0;
    }
    for (i = 216; i < 256; i++) {
        pal[i].peRed = pal[i].peGreen = pal[i].peBlue = (BYTE)((i - 215) * 255 / 40);
        pal[i].peFlags = 0;
    }
}

void movie_mci_before(mciSendCommandA_t send, MCIDEVICEID id, UINT msg)
{
    if (id == NO_DEVICE || id != avi_id || msg != MCI_PLAY)
        return;
    fit_to_screen(send, id);
    if (!palette_swapped) {
        PALETTEENTRY pal[256];
        if (!avi_path[0] || !movie_palette_for(avi_path, pal))
            cube_palette(pal);
        palette_swapped = ddraw_movie_palette(pal);
    }
}

void movie_mci_after(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms, MCIERROR err)
{
    if (msg == MCI_OPEN && !err && is_avi_open(flags, (MCI_OPEN_PARMSA *)parms)) {
        const MCI_OPEN_PARMSA *open = (const MCI_OPEN_PARMSA *)parms;
        avi_id = open->wDeviceID;
        avi_path[0] = 0;
        if ((flags & MCI_OPEN_ELEMENT) && open->lpstrElementName)
            lstrcpynA(avi_path, open->lpstrElementName, sizeof(avi_path));
    } else if (msg == MCI_CLOSE && id == avi_id) {
        avi_id = NO_DEVICE;
        if (palette_swapped)
            shim_log("rotk4-shim: movie: game palette restored = %d\n", ddraw_movie_palette(NULL));
        palette_swapped = FALSE;
    }
}
