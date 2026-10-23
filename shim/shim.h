#ifndef ROTK4_SHIM_H
#define ROTK4_SHIM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

typedef MCIERROR(WINAPI *mciSendCommandA_t)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);

void shim_log(const char *fmt, ...);

/* Replace one import of the main EXE; returns the previous target or NULL. */
void *iat_hook(const char *dll, const char *func, void *replacement);

/* Directory of the game EXE, with trailing backslash. */
void game_dir(char *buf, DWORD size);

void cdaudio_init(void);
void cdaudio_rehook(void);
void ddraw_rehook(void); /* GetSystemMetrics hook, see ddraw.c */
void game_size(int *w, int *h); /* the game's window size, 640x480 or 800x592 */
void winpos_check(void); /* reset saved cnc-ddraw window placement, see winpos.c */
void scale_attach(HWND wnd); /* menu and integer window scale, see scale.c */

/* One MP3 file of 44.1 kHz stereo, addressed in CD frames (mp3.c). */
typedef struct mp3 mp3_t;
mp3_t *mp3_open(const char *path, DWORD *frames); /* NULL if missing or not 44.1 kHz stereo */
BOOL mp3_seek(mp3_t *m, DWORD frame);
DWORD mp3_read(mp3_t *m, BYTE *buf, DWORD frames); /* bytes of 16-bit PCM; short at the end */
void mp3_close(mp3_t *m);

/* Load entries into the game's DirectDraw palette, or put the game's colours
 * back (NULL). Returns FALSE if the game has no palette yet. */
BOOL ddraw_movie_palette(const PALETTEENTRY *entries);

/* Median-cut palette (0 black, 255 white) from frames of an AVI file. */
BOOL movie_palette_for(const char *path, PALETTEENTRY *out);

/* Called around every MCI command that goes to the real winmm. */
void movie_mci_before(mciSendCommandA_t send, MCIDEVICEID id, UINT msg);
void movie_mci_after(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms, MCIERROR err);

#endif
