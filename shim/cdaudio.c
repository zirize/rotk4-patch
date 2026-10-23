/*
 * MCI "cdaudio" emulation backed by a BIN/CUE image.
 *
 * The game identifies its CD by the lengths of audio tracks 2..12 (checked
 * against a min/max table at 0x5374e0/0x537510) and plays background music
 * with MCI_PLAY FROM/TO in TMSF, relying on MM_MCINOTIFY to loop. Only the
 * cdaudio device is emulated; every other MCI device (waveaudio for sound
 * effects, avivideo for movies) goes to the real winmm, passing through the
 * movie fixes in movie.c.
 *
 * Commands the game uses: OPEN (type "cdaudio"), CLOSE, SET TIME FORMAT
 * (MSF, TMSF), STATUS (NUMBER_OF_TRACKS, MODE, MEDIA_PRESENT, LENGTH+TRACK),
 * PLAY (FROM|TO|NOTIFY), STOP.
 *
 * Audio tracks in the BIN are raw 44.1 kHz 16-bit stereo PCM, 2352 bytes per
 * frame (1/75 s), so they are streamed to waveOut without decoding. A track
 * with a TrackNN.mp3 beside the cue (tools/cd2mp3.sh) is decoded from that
 * file instead; with MP3s for every audio track the BIN can be left out, and
 * the end of the last track comes from its MP3. The cue stays the TOC.
 */
#include "shim.h"
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CD_DEVICE_ID 0x4344 /* "CD"; real winmm hands out small IDs */
#define FRAME_BYTES 2352
#define FRAMES_PER_SEC 75
#define MAX_TRACKS 99
#define NUM_BUFFERS 4
#define BUFFER_FRAMES 15 /* 0.2 s per buffer */

typedef MCIDEVICEID(WINAPI *mciGetDeviceIDA_t)(LPCSTR);

/* Where the streaming thread reads from: the BIN, or one track's MP3. */
struct source {
    HANDLE bin;
    mp3_t *mp3;
    int mp3_track; /* track `mp3` belongs to, 0 if none */
    DWORD mp3_next; /* absolute frame the decoder is at */
};

static mciSendCommandA_t real_mciSendCommandA;
static mciGetDeviceIDA_t real_mciGetDeviceIDA;
static BOOL have_disc; /* a cue image was found; otherwise cdaudio goes to the real drive */

static struct {
    char dir[MAX_PATH]; /* of the cue, with trailing backslash */
    char bin_path[MAX_PATH];
    int track_count;
    DWORD start[MAX_TRACKS + 2]; /* frame offsets; start[n + 1] is the end of track n */
    BOOL audio[MAX_TRACKS + 1];
    BOOL mp3[MAX_TRACKS + 1];
} disc;

static CRITICAL_SECTION lock;
static BOOL is_open;
static DWORD time_format = MCI_FORMAT_MSF;
static DWORD position; /* current frame, for STATUS POSITION and PLAY without FROM */

/* Playback state, shared with the streaming thread under `lock`. */
static HANDLE play_thread;
static volatile BOOL stop_requested;
static BOOL playing;
static HWND notify_hwnd;
static BOOL notify_pending;

/* ---- cue parsing ---- */

static void mp3_path(int track, char *buf)
{
    wsprintfA(buf, "%sTrack%02d.mp3", disc.dir, track);
}

static BOOL parse_cue(const char *cue_path)
{
    FILE *f = fopen(cue_path, "r");
    char line[512], *dir = disc.dir, path[MAX_PATH], *slash;
    int track = 0, t;
    DWORD frames = 0;
    BOOL track_audio = FALSE;
    HANDLE h;
    LARGE_INTEGER size;

    if (!f)
        return FALSE;
    memset(&disc, 0, sizeof(disc));
    lstrcpynA(dir, cue_path, sizeof(disc.dir));
    slash = strrchr(dir, '\\');
    if (slash)
        slash[1] = '\0';
    else
        dir[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *p = line, name[MAX_PATH], type[32];
        int m, s, fr, n;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!strncmp(p, "FILE ", 5)) {
            char *q = strchr(p, '"'), *e = q ? strchr(q + 1, '"') : NULL;
            if (!q || !e || disc.bin_path[0])
                continue; /* only single-file images are supported */
            *e = '\0';
            lstrcpynA(name, q + 1, sizeof(name));
            if (name[0] && name[1] == ':')
                lstrcpynA(disc.bin_path, name, sizeof(disc.bin_path));
            else
                wsprintfA(disc.bin_path, "%s%s", dir, name);
        } else if (sscanf(p, "TRACK %d %31s", &n, type) == 2) {
            track = n;
            track_audio = !strcmp(type, "AUDIO");
        } else if (sscanf(p, "INDEX 01 %d:%d:%d", &m, &s, &fr) == 3 && track >= 1 && track <= MAX_TRACKS) {
            disc.start[track] = (m * 60 + s) * FRAMES_PER_SEC + fr;
            disc.audio[track] = track_audio;
            if (track > disc.track_count)
                disc.track_count = track;
        }
    }
    fclose(f);

    if (!disc.track_count || !disc.bin_path[0])
        return FALSE;
    for (t = 1; t <= disc.track_count; t++) {
        mp3_t *m;
        if (!disc.audio[t])
            continue;
        mp3_path(t, path);
        if ((m = mp3_open(path, t == disc.track_count ? &frames : NULL))) {
            disc.mp3[t] = TRUE;
            mp3_close(m);
        }
    }
    h = CreateFileA(disc.bin_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(h, &size);
        CloseHandle(h);
        disc.start[disc.track_count + 1] = (DWORD)(size.QuadPart / FRAME_BYTES);
    } else if (disc.mp3[disc.track_count]) {
        disc.start[disc.track_count + 1] = disc.start[disc.track_count] + frames;
    } else {
        return FALSE;
    }
    return TRUE;
}

static BOOL find_disc(void)
{
    static const char *candidates[] = { "CD\\Audio.cue", "Audio.cue", "cd\\audio.cue" };
    char dir[MAX_PATH], path[MAX_PATH], ini[MAX_PATH], custom[MAX_PATH];
    size_t i;

    game_dir(dir, sizeof(dir));
    wsprintfA(ini, "%srotk4.ini", dir);
    GetPrivateProfileStringA("cdaudio", "cue", "", custom, sizeof(custom), ini);
    if (custom[0]) {
        if (custom[1] == ':' || custom[0] == '\\')
            lstrcpynA(path, custom, sizeof(path));
        else
            wsprintfA(path, "%s%s", dir, custom);
        if (parse_cue(path))
            return TRUE;
        shim_log("rotk4-shim: cdaudio: cannot use cue %s\n", path);
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        wsprintfA(path, "%s%s", dir, candidates[i]);
        if (parse_cue(path))
            return TRUE;
    }
    return FALSE;
}

/* ---- time formats ---- */

static DWORD track_length(int t)
{
    return disc.start[t + 1] - disc.start[t];
}

static DWORD frames_to_msf(DWORD frames)
{
    DWORD sec = frames / FRAMES_PER_SEC;
    return MCI_MAKE_MSF(sec / 60, sec % 60, frames % FRAMES_PER_SEC);
}

/* Absolute frame from a FROM/TO value in the current time format. */
static DWORD to_frames(DWORD value)
{
    switch (time_format) {
    case MCI_FORMAT_TMSF: {
        int t = MCI_TMSF_TRACK(value);
        if (t < 1 || t > disc.track_count)
            t = 1;
        return disc.start[t]
            + (MCI_TMSF_MINUTE(value) * 60 + MCI_TMSF_SECOND(value)) * FRAMES_PER_SEC
            + MCI_TMSF_FRAME(value);
    }
    case MCI_FORMAT_MILLISECONDS:
        return value * FRAMES_PER_SEC / 1000;
    default: /* MSF */
        return (MCI_MSF_MINUTE(value) * 60 + MCI_MSF_SECOND(value)) * FRAMES_PER_SEC + MCI_MSF_FRAME(value);
    }
}

static int track_at(DWORD frame)
{
    int t;
    for (t = disc.track_count; t > 1 && disc.start[t] > frame; t--)
        ;
    return t;
}

static DWORD from_frames(DWORD frames)
{
    int t;
    switch (time_format) {
    case MCI_FORMAT_TMSF:
        t = track_at(frames);
        frames -= disc.start[t];
        return MCI_MAKE_TMSF(t, frames / FRAMES_PER_SEC / 60, frames / FRAMES_PER_SEC % 60, frames % FRAMES_PER_SEC);
    case MCI_FORMAT_MILLISECONDS:
        return frames * 1000 / FRAMES_PER_SEC;
    default:
        return frames_to_msf(frames);
    }
}

/* ---- playback ---- */

static void post_notify(UINT code)
{
    if (notify_pending && notify_hwnd)
        PostMessageA(notify_hwnd, MM_MCINOTIFY, code, CD_DEVICE_ID);
    notify_pending = FALSE;
}

/*
 * Reads up to `frames` frames at `next`, stopping at the end of its track.
 * Returns bytes read, 0 if there is nothing to play there.
 */
static DWORD read_frames(struct source *src, DWORD next, DWORD frames, BYTE *buf)
{
    int t = track_at(next);
    DWORD got = 0;

    if (frames > disc.start[t + 1] - next)
        frames = disc.start[t + 1] - next;
    if (disc.mp3[t]) {
        if (src->mp3_track != t) {
            char path[MAX_PATH];
            mp3_close(src->mp3);
            mp3_path(t, path);
            src->mp3 = mp3_open(path, NULL);
            src->mp3_track = t;
            src->mp3_next = disc.start[t];
        }
        if (src->mp3 && src->mp3_next != next)
            mp3_seek(src->mp3, next - disc.start[t]);
        if (src->mp3)
            got = mp3_read(src->mp3, buf, frames);
        src->mp3_next = next + frames;
        /* An MP3 a few frames short of the TOC ends in silence, keeping the timing. */
        memset(buf + got, 0, frames * FRAME_BYTES - got);
        return frames * FRAME_BYTES;
    }
    if (src->bin != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER off;
        off.QuadPart = (LONGLONG)next * FRAME_BYTES;
        SetFilePointerEx(src->bin, off, NULL, FILE_BEGIN);
        ReadFile(src->bin, buf, frames * FRAME_BYTES, &got, NULL);
    }
    return got;
}

struct play_range {
    DWORD from, to;
};

static DWORD WINAPI play_proc(LPVOID arg)
{
    struct play_range range = *(struct play_range *)arg;
    WAVEFORMATEX fmt = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    WAVEHDR hdr[NUM_BUFFERS];
    static BYTE data[NUM_BUFFERS][BUFFER_FRAMES * FRAME_BYTES];
    HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
    struct source src = { INVALID_HANDLE_VALUE, NULL, 0, 0 };
    HWAVEOUT wo;
    DWORD next = range.from;
    BOOL finished = FALSE;
    int i, queued = 0;

    free(arg);
    if (waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        shim_log("rotk4-shim: cdaudio: cannot start playback\n");
        CloseHandle(done);
        EnterCriticalSection(&lock);
        playing = FALSE;
        post_notify(MCI_NOTIFY_FAILURE);
        LeaveCriticalSection(&lock);
        return 0;
    }

    src.bin = CreateFileA(disc.bin_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    memset(hdr, 0, sizeof(hdr));
    for (;;) {
        /* Refill every buffer the device has finished with. */
        for (i = 0; i < NUM_BUFFERS && !stop_requested; i++) {
            DWORD frames, got;
            if (hdr[i].dwFlags & WHDR_INQUEUE)
                continue;
            if (hdr[i].dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
                queued--;
            }
            if (next >= range.to)
                continue;
            frames = range.to - next;
            if (frames > BUFFER_FRAMES)
                frames = BUFFER_FRAMES;
            got = read_frames(&src, next, frames, data[i]);
            if (!got) {
                next = range.to;
                continue;
            }
            next += got / FRAME_BYTES;
            position = next;
            memset(&hdr[i], 0, sizeof(WAVEHDR));
            hdr[i].lpData = (LPSTR)data[i];
            hdr[i].dwBufferLength = got;
            waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
            waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR));
            queued++;
        }
        if (stop_requested)
            break;
        if (!queued && next >= range.to) {
            finished = TRUE;
            break;
        }
        WaitForSingleObject(done, 100);
    }

    waveOutReset(wo);
    for (i = 0; i < NUM_BUFFERS; i++)
        if (hdr[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(wo);
    if (src.bin != INVALID_HANDLE_VALUE)
        CloseHandle(src.bin);
    mp3_close(src.mp3);
    CloseHandle(done);

    EnterCriticalSection(&lock);
    playing = FALSE;
    if (finished)
        post_notify(MCI_NOTIFY_SUCCESSFUL);
    LeaveCriticalSection(&lock);
    return 0;
}

/* Stops playback; `code` is sent to a pending notify (ABORTED or SUPERSEDED). */
static void stop_playback(UINT code)
{
    HANDLE t;
    EnterCriticalSection(&lock);
    t = play_thread;
    play_thread = NULL;
    stop_requested = TRUE;
    LeaveCriticalSection(&lock);
    if (t) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    EnterCriticalSection(&lock);
    stop_requested = FALSE;
    playing = FALSE;
    post_notify(code);
    LeaveCriticalSection(&lock);
}

static MCIERROR cd_play(DWORD flags, MCI_PLAY_PARMS *parms)
{
    struct play_range *range;
    DWORD from = position, to = disc.start[disc.track_count + 1];

    if (parms && (flags & MCI_FROM))
        from = to_frames(parms->dwFrom);
    if (parms && (flags & MCI_TO))
        to = to_frames(parms->dwTo);
    if (from >= to || to > disc.start[disc.track_count + 1])
        return MCIERR_OUTOFRANGE;

    stop_playback(MCI_NOTIFY_SUPERSEDED);
    range = malloc(sizeof(*range));
    range->from = from;
    range->to = to;

    EnterCriticalSection(&lock);
    position = from;
    playing = TRUE;
    notify_pending = parms && (flags & MCI_NOTIFY);
    notify_hwnd = notify_pending ? (HWND)parms->dwCallback : NULL;
    play_thread = CreateThread(NULL, 0, play_proc, range, 0, NULL);
    LeaveCriticalSection(&lock);
    return 0;
}

static MCIERROR cd_status(DWORD flags, MCI_STATUS_PARMS *parms)
{
    if (!parms || !(flags & MCI_STATUS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    switch (parms->dwItem) {
    case MCI_STATUS_NUMBER_OF_TRACKS:
        parms->dwReturn = disc.track_count;
        break;
    case MCI_STATUS_MODE:
        parms->dwReturn = playing ? MCI_MODE_PLAY : MCI_MODE_STOP;
        break;
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY:
        parms->dwReturn = TRUE;
        break;
    case MCI_STATUS_TIME_FORMAT:
        parms->dwReturn = time_format;
        break;
    case MCI_STATUS_LENGTH:
        if (flags & MCI_TRACK) {
            if (parms->dwTrack < 1 || parms->dwTrack > (DWORD)disc.track_count)
                return MCIERR_OUTOFRANGE;
            /* Track lengths are reported in MSF even in TMSF format. */
            parms->dwReturn = time_format == MCI_FORMAT_MILLISECONDS
                ? track_length(parms->dwTrack) * 1000 / FRAMES_PER_SEC
                : frames_to_msf(track_length(parms->dwTrack));
        } else {
            parms->dwReturn = time_format == MCI_FORMAT_MILLISECONDS
                ? disc.start[disc.track_count + 1] * 1000 / FRAMES_PER_SEC
                : frames_to_msf(disc.start[disc.track_count + 1]);
        }
        break;
    case MCI_STATUS_POSITION:
        if (flags & MCI_TRACK) {
            if (parms->dwTrack < 1 || parms->dwTrack > (DWORD)disc.track_count)
                return MCIERR_OUTOFRANGE;
            parms->dwReturn = from_frames(disc.start[parms->dwTrack]);
        } else {
            parms->dwReturn = from_frames(position);
        }
        break;
    case MCI_STATUS_CURRENT_TRACK:
        parms->dwReturn = track_at(position);
        break;
    case MCI_CDA_STATUS_TYPE_TRACK:
        if (!(flags & MCI_TRACK) || parms->dwTrack < 1 || parms->dwTrack > (DWORD)disc.track_count)
            return MCIERR_OUTOFRANGE;
        parms->dwReturn = disc.audio[parms->dwTrack] ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER;
        break;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}

static MCIERROR cd_command(UINT msg, DWORD flags, DWORD_PTR parms)
{
    MCI_GENERIC_PARMS *generic = (MCI_GENERIC_PARMS *)parms;
    MCIERROR err = 0;

    switch (msg) {
    case MCI_CLOSE:
        stop_playback(MCI_NOTIFY_ABORTED);
        is_open = FALSE;
        break;
    case MCI_SET:
        if (flags & MCI_SET_TIME_FORMAT)
            time_format = ((MCI_SET_PARMS *)parms)->dwTimeFormat;
        break;
    case MCI_STATUS:
        err = cd_status(flags, (MCI_STATUS_PARMS *)parms);
        break;
    case MCI_PLAY:
        return cd_play(flags, (MCI_PLAY_PARMS *)parms); /* notifies on its own */
    case MCI_STOP:
    case MCI_PAUSE:
        stop_playback(MCI_NOTIFY_ABORTED);
        break;
    case MCI_SEEK:
        stop_playback(MCI_NOTIFY_ABORTED);
        if (flags & MCI_SEEK_TO_START)
            position = disc.start[1];
        else if (flags & MCI_SEEK_TO_END)
            position = disc.start[disc.track_count + 1];
        else if (flags & MCI_TO)
            position = to_frames(((MCI_SEEK_PARMS *)parms)->dwTo);
        break;
    default:
        shim_log("rotk4-shim: cdaudio: unhandled MCI message 0x%x flags 0x%lx\n", msg, flags);
        err = MCIERR_UNSUPPORTED_FUNCTION;
    }
    /* Synchronous commands complete at once; send NOTIFY_SUCCESSFUL if asked. */
    if (!err && (flags & MCI_NOTIFY) && generic)
        PostMessageA((HWND)generic->dwCallback, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, CD_DEVICE_ID);
    return err;
}

static BOOL is_cdaudio_open(DWORD flags, MCI_OPEN_PARMSA *parms)
{
    if (!parms || !(flags & MCI_OPEN_TYPE))
        return FALSE;
    if (flags & MCI_OPEN_TYPE_ID)
        return LOWORD((DWORD_PTR)parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO;
    return parms->lpstrDeviceType && !lstrcmpiA(parms->lpstrDeviceType, "cdaudio");
}

static MCIERROR hook_mciSendCommandA_inner(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms);

static MCIERROR WINAPI hook_mciSendCommandA(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms)
{
    MCIERROR err = hook_mciSendCommandA_inner(id, msg, flags, parms);
    /* cnc-ddraw re-runs its hooks when an MCI call loads msvfw32 (first avivideo open). */
    cdaudio_rehook();
    if (msg == MCI_STATUS && parms)
        shim_log("rotk4-shim: mci(0x%x, STATUS item %lu track %lu, 0x%lx) = %lu -> 0x%lx\n", id,
            ((MCI_STATUS_PARMS *)parms)->dwItem, ((MCI_STATUS_PARMS *)parms)->dwTrack, (DWORD)flags, err,
            ((MCI_STATUS_PARMS *)parms)->dwReturn);
    else
        shim_log("rotk4-shim: mci(0x%x, 0x%x, 0x%lx) = %lu\n", id, msg, (DWORD)flags, err);
    return err;
}

static MCIERROR hook_mciSendCommandA_inner(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms)
{
    MCIERROR err;

    if (have_disc && msg == MCI_OPEN && is_cdaudio_open(flags, (MCI_OPEN_PARMSA *)parms)) {
        MCI_OPEN_PARMSA *open = (MCI_OPEN_PARMSA *)parms;
        is_open = TRUE;
        open->wDeviceID = CD_DEVICE_ID;
        if (flags & MCI_NOTIFY)
            PostMessageA((HWND)open->dwCallback, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, CD_DEVICE_ID);
        return 0;
    }
    if (have_disc && id == CD_DEVICE_ID)
        return is_open ? cd_command(msg, flags, parms) : MCIERR_INVALID_DEVICE_ID;
    movie_mci_before(real_mciSendCommandA, id, msg);
    err = real_mciSendCommandA(id, msg, flags, parms);
    movie_mci_after(id, msg, flags, parms, err);
    return err;
}

static MCIDEVICEID WINAPI hook_mciGetDeviceIDA(LPCSTR name)
{
    if (have_disc && is_open && name && !lstrcmpiA(name, "cdaudio"))
        return CD_DEVICE_ID;
    return real_mciGetDeviceIDA(name);
}

static BOOL active;

/*
 * cnc-ddraw hooks mciSendCommandA itself (for MCIAVI movie windows) during
 * DirectDraw setup, overwriting our IAT entry. Called again after each
 * DirectDraw call that may do that; whatever was in the IAT becomes the next
 * link in the chain.
 */
void cdaudio_rehook(void)
{
    void *old;

    ddraw_rehook();
    if (!active)
        return;
    old = iat_hook("winmm.dll", "mciSendCommandA", (void *)hook_mciSendCommandA);
    if (old && old != (void *)hook_mciSendCommandA)
        real_mciSendCommandA = (mciSendCommandA_t)old;
    old = iat_hook("winmm.dll", "mciGetDeviceIDA", (void *)hook_mciGetDeviceIDA);
    if (old && old != (void *)hook_mciGetDeviceIDA)
        real_mciGetDeviceIDA = (mciGetDeviceIDA_t)old;
}

void cdaudio_init(void)
{
    int t;

    /* The hook also carries the movie fixes, so it is installed even without a disc image. */
    real_mciSendCommandA = (mciSendCommandA_t)GetProcAddress(LoadLibraryA("winmm.dll"), "mciSendCommandA");
    real_mciGetDeviceIDA = (mciGetDeviceIDA_t)GetProcAddress(LoadLibraryA("winmm.dll"), "mciGetDeviceIDA");
    active = TRUE;
    cdaudio_rehook();

    if (!find_disc()) {
        shim_log("rotk4-shim: cdaudio: no cue image found, using the real CD drive\n");
        return;
    }
    InitializeCriticalSection(&lock);
    position = disc.start[1];
    have_disc = TRUE;

    shim_log("rotk4-shim: cdaudio: %s, %d tracks\n", disc.dir, disc.track_count);
    for (t = 1; t <= disc.track_count; t++)
        shim_log("rotk4-shim: cdaudio:   track %d %s start %lu length %lu%s\n", t,
            disc.audio[t] ? "audio" : "data", disc.start[t], track_length(t), disc.mp3[t] ? " (mp3)" : "");
}
