/*
 * 256-colour palette for one AVI file.
 *
 * GDI maps the movie frames to the palette by nearest colour without
 * dithering, so a fixed palette (6x6x6 cube, 3-3-2) bands badly on the
 * mostly sepia movies. Instead we decode frames spread over the file with
 * AVIFile, histogram them at 5 bits per channel and median-cut the histogram
 * into 254 colours. Index 0 stays black and 255 white, as DirectDraw expects.
 */
#include "shim.h"
#include <vfw.h>
#include <stdlib.h>

#define SAMPLE_FRAMES 48
#define BINS (32 * 32 * 32)
#define COLOURS 254

typedef struct {
    BYTE c[3]; /* r, g, b at 5 bits */
    DWORD count;
} bin_t;

typedef struct {
    int lo, hi; /* range in the bin array */
    DWORD pixels;
    int axis, spread;
} box_t;

static int sort_axis;

static int cmp_bin(const void *a, const void *b)
{
    return ((const bin_t *)a)->c[sort_axis] - ((const bin_t *)b)->c[sort_axis];
}

static void measure(box_t *box, const bin_t *bins)
{
    int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 };
    int i, a;

    box->pixels = 0;
    for (i = box->lo; i < box->hi; i++) {
        for (a = 0; a < 3; a++) {
            if (bins[i].c[a] < lo[a])
                lo[a] = bins[i].c[a];
            if (bins[i].c[a] > hi[a])
                hi[a] = bins[i].c[a];
        }
        box->pixels += bins[i].count;
    }
    box->axis = 0;
    for (a = 1; a < 3; a++)
        if (hi[a] - lo[a] > hi[box->axis] - lo[box->axis])
            box->axis = a;
    box->spread = hi[box->axis] - lo[box->axis];
}

/* Add one 24 bpp bottom-up DIB frame to the histogram. */
static void add_frame(DWORD *hist, const BITMAPINFOHEADER *bih)
{
    const BYTE *bits = (const BYTE *)bih + bih->biSize;
    int stride = (bih->biWidth * 3 + 3) & ~3;
    int x, y;

    for (y = 0; y < abs(bih->biHeight); y++) {
        const BYTE *p = bits + y * stride;
        for (x = 0; x < bih->biWidth; x++, p += 3)
            hist[(p[2] >> 3) << 10 | (p[1] >> 3) << 5 | p[0] >> 3]++;
    }
}

static BOOL sample(const char *path, DWORD *hist)
{
    PAVISTREAM stream = NULL;
    PGETFRAME frames = NULL;
    BITMAPINFOHEADER want = { 0 };
    LONG len, i;
    BOOL ok = FALSE;

    AVIFileInit();
    if (AVIStreamOpenFromFileA(&stream, path, streamtypeVIDEO, 0, OF_READ, NULL))
        goto out;
    want.biSize = sizeof(want);
    want.biPlanes = 1;
    want.biBitCount = 24;
    want.biCompression = BI_RGB;
    if (!(frames = AVIStreamGetFrameOpen(stream, &want)))
        goto out;
    len = AVIStreamLength(stream);
    for (i = 0; i < SAMPLE_FRAMES; i++) {
        const BITMAPINFOHEADER *bih = AVIStreamGetFrame(frames, AVIStreamStart(stream) + len * i / SAMPLE_FRAMES);
        if (bih && bih->biBitCount == 24 && bih->biCompression == BI_RGB) {
            add_frame(hist, bih);
            ok = TRUE;
        }
    }
out:
    if (frames)
        AVIStreamGetFrameClose(frames);
    if (stream)
        AVIStreamRelease(stream);
    AVIFileExit();
    return ok;
}

BOOL movie_palette_for(const char *path, PALETTEENTRY *out)
{
    DWORD *hist = calloc(BINS, sizeof(*hist));
    bin_t *bins = NULL;
    box_t boxes[COLOURS];
    int nbins = 0, nboxes = 1, i, b;
    BOOL ok = FALSE;

    if (!hist || !sample(path, hist))
        goto out;
    if (!(bins = malloc(BINS * sizeof(*bins))))
        goto out;
    for (i = 0; i < BINS; i++)
        if (hist[i]) {
            bins[nbins].c[0] = (BYTE)(i >> 10);
            bins[nbins].c[1] = (BYTE)(i >> 5 & 31);
            bins[nbins].c[2] = (BYTE)(i & 31);
            bins[nbins++].count = hist[i];
        }

    /* Split the box with the largest pixels x spread at its median. */
    boxes[0].lo = 0;
    boxes[0].hi = nbins;
    measure(&boxes[0], bins);
    while (nboxes < COLOURS) {
        int best = -1, mid;
        DWORD half, sum = 0;
        for (b = 0; b < nboxes; b++)
            if (boxes[b].spread > 0 && (best < 0 || (double)boxes[b].pixels * boxes[b].spread > (double)boxes[best].pixels * boxes[best].spread))
                best = b;
        if (best < 0)
            break;
        sort_axis = boxes[best].axis;
        qsort(bins + boxes[best].lo, boxes[best].hi - boxes[best].lo, sizeof(*bins), cmp_bin);
        half = boxes[best].pixels / 2;
        for (mid = boxes[best].lo; mid < boxes[best].hi - 1; mid++) {
            sum += bins[mid].count;
            if (sum >= half)
                break;
        }
        mid++; /* first bin of the upper half; both halves are non-empty */
        boxes[nboxes].lo = mid;
        boxes[nboxes].hi = boxes[best].hi;
        boxes[best].hi = mid;
        measure(&boxes[best], bins);
        measure(&boxes[nboxes++], bins);
    }

    out[0].peRed = out[0].peGreen = out[0].peBlue = 0;
    out[255].peRed = out[255].peGreen = out[255].peBlue = 255;
    for (i = 0; i < 256; i++)
        out[i].peFlags = 0;
    for (b = 0; b < COLOURS; b++) {
        double sum[3] = { 0, 0, 0 };
        int a;
        if (b >= nboxes) {
            out[b + 1] = out[0];
            continue;
        }
        for (i = boxes[b].lo; i < boxes[b].hi; i++)
            for (a = 0; a < 3; a++)
                sum[a] += bins[i].c[a] * (double)bins[i].count;
        /* Bin centre: 5-bit value v covers 8v..8v+7. */
        out[b + 1].peRed = (BYTE)(sum[0] / boxes[b].pixels * 8 + 4);
        out[b + 1].peGreen = (BYTE)(sum[1] / boxes[b].pixels * 8 + 4);
        out[b + 1].peBlue = (BYTE)(sum[2] / boxes[b].pixels * 8 + 4);
    }
    shim_log("rotk4-shim: movie: %s: %d colours from %d bins\n", path, nboxes, nbins);
    ok = TRUE;
out:
    free(bins);
    free(hist);
    return ok;
}
