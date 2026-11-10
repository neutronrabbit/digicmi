/* digicmi: the CMI machine's two SRC pages (machine-pages 1.4, cm_ui_v33).
 *
 * Page 1 is ONESHOT's page as it is: TUNE, PLAY, BR, SAMP / STRT, LEN, LOOP, LEV.
 * Page 2 holds the CMI-style loop controls, machine-pages' extra values 8-11, which the firmware keeps in
 * the sound's slots 46-49 (so they take p-locks, the LFOs, copy/paste and the project save):
 *   LLEN  loop length: 0 = to the end (the stock loop), else LOOP + LLEN/128 of the sample
 *   LMOD  loop mode:   HOLD (loops on after release, as the stock and the CMI's default) or
 *                      SUS (loops while the key is held, then plays on to the end: the CMI's SUSTAIN)
 *   DIR   loop direction: FWD, or ALT (forward, then backward: ping-pong)
 *   LFIN  fine loop start: one LOOP step divided into 128
 * Extras need SLICE's parameter bank (params 3); the machine still plays as ONESHOT (render 0) on ONESHOT's
 * page (page_from 0). Extra 14 (slot 52) is never used: Digi Poly's kitstore marks every sound there. */
#include "digitakt-mk1/core3.h"

#define EX_LLEN 8
#define EX_LMOD 9
#define EX_DIR  10
#define EX_LFIN 11
#define P_BR    0x86                            /* BR's parameter id: the look of a plain 0..127 knob */

static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

static char *put_u(char *b, unsigned v)
{
    char t[10];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) && n < 10);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

static int32_t fmt_llen(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    v = (v >> 8) & 127;
    if (v)
        put_u(buf, (unsigned)v);
    else
        put_s(buf, ctx ? "TO END" : "END");
    return 1;
}

static int32_t fmt_lmod(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 64 ? (ctx ? "HOLD (LOOP ON)" : "HOLD") : (ctx ? "SUSTAIN" : "SUS"));
    return 1;
}

static int32_t fmt_dir(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 64 ? (ctx ? "FORWARD" : "FWD") : (ctx ? "ALTERNATE" : "ALT"));
    return 1;
}

static int32_t fmt_num(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    put_u(buf, (unsigned)((v >> 8) & 127));
    return 1;
}

/* ---- the loop page's waveform: the sample, STRT/END ticks and the loop's two markers ----------------- *
 * Painted over the page's empty lower half. LLEN's knob exists only on this page, so its graphic's callback
 * says the page is being drawn and takes the values the page shows (a held step's locks included); ev_draw,
 * which comes after the frame is composed, then paints. Returning 0 leaves the knob its stock graphic. */
#ifdef OS154
#define SMP_TAB  0x4031a3a0u
#else
#define SMP_TAB  0x403193a0u
#endif
#define FULL     0x7800                         /* 120.0: STRT, LEN and LOOP's scale */
#define WX0      24                             /* the strip: x to the right of the LEV fader */
#define WCOLS    102
#define WY0      2                              /* y = 0 is the screen's bottom row; 0-1 the STRT/END ticks */
#define WMID     12
#define WHALF    10
#define WTOP     (WMID + WHALF + 2)             /* the loop markers' flags: the two rows above the wave */
#define WPROBES  64                             /* samples read a column: a long sample stays cheap */

static struct { int on, slot, play, strt, endv, loopv, llen; } shown;
static signed char lo[WCOLS], hi[WCOLS];
static const short *peaks_pcm;
static uint32_t peaks_len;

static int clampi(int x, int a, int b)
{
    return x < a ? a : x > b ? b : x;
}

static int32_t llen_draw(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int play = cm_src_shown(1), strt = cm_src_shown(4), len = cm_src_shown(5), g = cm_src_shown(6);
    int samp = cm_src_shown(3), lfin = cm_src_shown(EX_LFIN);
    (void)bmp; (void)x; (void)y; (void)flag;
    if (play == CM_NO_VALUE || strt == CM_NO_VALUE || len == CM_NO_VALUE || g == CM_NO_VALUE ||
        samp == CM_NO_VALUE || lfin == CM_NO_VALUE)
        return 0;
    shown.play = (play >> 8) & 255;
    shown.slot = (samp >> 8) & 255;
    shown.strt = clampi(strt, 0, FULL);
    shown.endv = clampi(strt + len, 0, FULL);
    shown.loopv = clampi((g ? clampi(g - 1, 0, FULL) : shown.strt) + 2 * ((lfin >> 8) & 127), 0, shown.endv);
    shown.llen = (value >> 8) & 127;
    shown.on = 1;
    return 0;
}

static void make_peaks(const short *pcm, uint32_t len)
{
    int c, p, top = 1;
    for (c = 0; c < WCOLS; c++) {
        uint32_t a = (len / WCOLS) * (uint32_t)c, n = len / WCOLS;
        uint32_t step = n / WPROBES ? n / WPROBES : 1;
        int mn = 0, mx = 0;
        for (p = 0; p < WPROBES && (uint32_t)p * step < n; p++) {
            int v = pcm[a + (uint32_t)p * step];
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
        }
        lo[c] = (signed char)(mn >> 8);
        hi[c] = (signed char)(mx >> 8);
        if (-lo[c] > top)
            top = -lo[c];
        if (hi[c] > top)
            top = hi[c];
    }
    for (c = 0; c < WCOLS; c++) {                /* quiet samples still fill the strip */
        lo[c] = (signed char)(lo[c] * WHALF / top);
        hi[c] = (signed char)(hi[c] * WHALF / top);
    }
}

static int col_of(int val)
{
    return val * (WCOLS - 1) / FULL;
}

/* A loop marker: on where the wave isn't and off where it is, so it shows over a loud sample too. */
static void marker(void *bmp, int c)
{
    int x = WX0 + c, y0 = WMID + lo[c], y1 = WMID + hi[c];
    if (y0 > WY0)
        fw_fillrect(bmp, x, WY0, x, y0 - 1, 1);
    fw_fillrect(bmp, x, y0, x, y1, 0);
    if (y1 < WTOP)
        fw_fillrect(bmp, x, y1 + 1, x, WTOP, 1);
}

void digicmi_wave_draw(void *bmp, void *ctrl)
{
    const volatile uint32_t *e;
    const short *pcm;
    uint32_t len;
    int c, a, b, looped, le;
    (void)ctrl;
    if (!shown.on)
        return;
    shown.on = 0;
    fw_fillrect(bmp, WX0, 0, WX0 + WCOLS - 1, WTOP, 0);   /* the blank knobs' boxes go */
    e = (const volatile uint32_t *)(SMP_TAB + 16 * (uint32_t)shown.slot);
    pcm = (const short *)e[0];
    len = e[2];
    if ((uint32_t)pcm < 0x40000000u || len <= 248u) {    /* no sample: the slots' silent dummy */
        fw_fillrect(bmp, WX0, WMID, WX0 + WCOLS - 1, WMID, 1);
        return;
    }
    if (pcm != peaks_pcm || len != peaks_len) {
        make_peaks(pcm, len);
        peaks_pcm = pcm;
        peaks_len = len;
    }
    looped = shown.play == 2;                    /* FWD.L: the only PLAY the loop controls act on */
    le = shown.llen ? shown.loopv + shown.llen * (FULL / 128) : shown.endv;
    if (le > shown.endv)
        le = shown.endv;
    a = col_of(shown.loopv);
    b = col_of(le);
    for (c = 0; c < WCOLS; c++)                  /* solid inside the loop, dotted outside it */
        if (!looped || (c >= a && c <= b) || !(c & 1))
            fw_fillrect(bmp, WX0 + c, WMID + lo[c], WX0 + c, WMID + hi[c], 1);

    c = col_of(shown.strt);                      /* STRT and END: ticks along the bottom */
    fw_fillrect(bmp, WX0 + c, 0, WX0 + c, 1, 1);
    c = col_of(shown.endv);
    fw_fillrect(bmp, WX0 + c, 0, WX0 + c, 1, 1);

    if (looped) {                                /* a line and an inward flag at each end of the loop */
        marker(bmp, a);
        marker(bmp, b);
        fw_fillrect(bmp, WX0 + a, WTOP - 1, clampi(WX0 + a + 3, 0, WX0 + WCOLS - 1), WTOP, 1);
        fw_fillrect(bmp, clampi(WX0 + b - 3, WX0, WX0 + WCOLS - 1), WTOP - 1, WX0 + b, WTOP, 1);
    }
}

#define EXTRA(sn, ln, d, f, c) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .cc = c, .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = f }

static const struct cm_src_page pages[2] = {
    {"CMI", {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0}},
    {"CMI LOOP", {EX_LLEN, EX_LMOD, EX_DIR, EX_LFIN, CM_SRC_BLANK, CM_SRC_BLANK, CM_SRC_BLANK, CM_SRC_BLANK},
     0, {0, 0, 0}},
};

const struct cm_ui32 digicmi_page = {
    /* ONESHOT's page. With an extras page the knobs are the params machine's (SLICE's: E SLICE, F LEN, G GRID),
     * so E-G take ONESHOT's look, name, range and default (the firmware's range table, 0x401aa0a4 on 1.54,
     * 52 bytes an id: ONESHOT 0x6c-0x73, SLICE 0x84-0x8b); A-D and H are the same in both. The render, ONESHOT,
     * reads slot 21 as STRT, 22 LEN, 23 LOOP. */
    .base = { .abi = cm_ui_v33, .page_from = 0,
        .knob = {
            [4] = { .name = "STRT", .lname = "Sample Start", .look = 0x70, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7800, .def = 0 },
            [5] = { .name = "LEN", .lname = "Sample Length", .look = 0x71, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7800, .def = 0x7800 },
            [6] = { .name = "LOOP", .lname = "Loop Position", .look = 0x72, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7802, .def = 0 },
        } },
    .extra = {
        { .name = "LLEN", .lname = "Loop Length", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 24, .min = 0, .max = 127 << 8, .def = 0, .fmt = fmt_llen, .draw = llen_draw },
        EXTRA("LMOD", "Loop Mode", 0, fmt_lmod, 25),
        EXTRA("DIR", "Loop Direction", 0, fmt_dir, 26),
        EXTRA("LFIN", "Loop Start Fine", 0, fmt_num, 27),
        /* 12-14: on no page, but machine-pages refuses the whole page unless all seven are valid (a look,
         * CM_RANGE | CM_DEFAULT, 0..0x7f00). A switch writes each one's default into its slot; 14 is slot 52,
         * kitstore's marker, so its default is the marker itself (KS_MARK, 0x4b53) and the switch keeps it. */
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00 },
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00 },
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00,
          .def = 0x4b53 },
    },
    .page_count = 2, .pages = pages,
};
