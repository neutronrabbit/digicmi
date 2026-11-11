/* digicmi: the CMI machine's two SRC pages (machine-pages 1.4, cm_ui_v33).
 *
 * Page 1 "CMI":  A TUNE, B 8BIT, C ENG, D SAMP / E-G blank, H LEV.
 * Page 2 "LOOP": a full-screen waveform of the sample (or the spectral engine's placeholder) with the eight
 *                loop values as text, each above or below its encoder:
 *                A STRT, B LEN, C LOOP, D LFIN / E LLEN, F LMOD, G DIR, H PLAY.
 * TUNE, PLAY, SAMP, STRT, LEN, LOOP and LEV are the sound's own slots 17-24 (ONESHOT's meaning: CMI renders as
 * ONESHOT). The rest are machine-pages' extra values, kept by the firmware in slots 46-51 (p-locks, LFOs,
 * copy/paste and the project save):
 *   LLEN  loop length: 0 = to the end (the stock loop), else LOOP + LLEN/128 of the sample
 *   LMOD  HOLD (loops on after release) or SUS (loops while held, then plays on to the end)
 *   DIR   FWD, or ALT (forward, then backward: ping-pong)
 *   LFIN  fine loop start: one LOOP step divided into 128
 *   8BIT  OFF/ON: the sample played at 8 bits (cmi_loop.c, digicmi_render)
 *   ENG   SMPL, or SPEC: the spectral synth, not built yet (its voices are silent)
 * Extras need SLICE's parameter bank (params 3). Extra 14 (slot 52) is on no page: Digi Poly's kitstore marks
 * every sound there. BR is on no page (its slot still exists; stock BR crushes rate and bits together). */
#include "digitakt-mk1/core3.h"

#define EX_LLEN 8
#define EX_LMOD 9
#define EX_DIR  10
#define EX_LFIN 11
#define EX_8BIT 12
#define EX_ENG  13
#define P_BR    0x86                            /* BR's parameter id: the look of a plain 0..127 knob */
#define B       CM_SRC_BLANK

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

static int32_t fmt_8bit(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 64 ? "OFF" : (ctx ? "ON: 8-BIT" : "ON"));
    return 1;
}

static int32_t fmt_eng(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 64 ? (ctx ? "SAMPLE" : "SMPL") : (ctx ? "SPECTRAL (NOT BUILT)" : "SPEC"));
    return 1;
}

/* ---- page 2: the full-screen view ------------------------------------------------------------------ *
 * LLEN's knob exists only on page 2, so its graphic's callback says the page is being drawn and takes the
 * values it shows (a held step's locks included); ev_draw, after the frame is composed, then paints the
 * view over everything under the title bar. The encoders still turn the page's eight values as usual. */
#ifdef OS154
#define SMP_TAB  0x4031a3a0u
#else
#define SMP_TAB  0x403193a0u
#endif
#define FULL     0x7800                         /* 120.0: STRT, LEN and LOOP's scale */
#define SCREEN_TOP 54                           /* y = 0 is the bottom row; the title bar is above 54 */
#define WCOLS    128
#define WMID     34                             /* the wave: 17-51; the loop flags at 52-53; ticks at 15-16 */
#define WHALF    17
#define WBOT     (WMID - WHALF)
#define WTOP     (WMID + WHALF + 2)
#define TXT_A    8                              /* text rows: A-D above E-H, a 32-pixel column each */
#define TXT_E    1
#define WPROBES  64                             /* samples read a column: a long sample stays cheap */

static struct { int on, slot, play, strt, len, g, lfin, llen, lmod, dir, eng; } shown;
static signed char lo[WCOLS], hi[WCOLS];
static const short *peaks_pcm;
static uint32_t peaks_len;

static int clampi(int x, int a, int b)
{
    return x < a ? a : x > b ? b : x;
}

static int32_t llen_draw(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    static const int8_t ks[] = {1, 3, 4, 5, 6, EX_LMOD, EX_DIR, EX_LFIN, EX_ENG};
    int32_t v[sizeof ks];
    unsigned i;
    (void)bmp; (void)x; (void)y; (void)flag;
    for (i = 0; i < sizeof ks; i++)
        if ((v[i] = cm_src_shown(ks[i])) == CM_NO_VALUE)
            return 0;
    shown.play = (v[0] >> 8) & 255;
    shown.slot = (v[1] >> 8) & 255;
    shown.strt = clampi(v[2], 0, FULL);
    shown.len = clampi(v[3], 0, FULL);
    shown.g = clampi(v[4], 0, 0x7802);
    shown.lmod = (v[5] >> 8) & 127;
    shown.dir = (v[6] >> 8) & 127;
    shown.lfin = (v[7] >> 8) & 127;
    shown.eng = (v[8] >> 8) & 127;
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
    for (c = 0; c < WCOLS; c++) {                /* quiet samples still fill the view */
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
    int y0 = WMID + lo[c], y1 = WMID + hi[c];
    if (y0 > WBOT)
        fw_fillrect(bmp, c, WBOT, c, y0 - 1, 1);
    fw_fillrect(bmp, c, y0, c, y1, 0);
    if (y1 < WTOP)
        fw_fillrect(bmp, c, y1 + 1, c, WTOP, 1);
}

static void text(void *bmp, int col, int y, const char *s)
{
    fw_textf(bmp, fw_font5, 2 + 32 * col, y, -1, "%s", s);
}

static void loop_text(void *bmp)
{
    static const char *const plays[4] = {"REV", "REV.L", "FWD.L", "FWD"};
    char b[12];
    put_u(put_s(b, "ST"), (unsigned)(shown.strt >> 8));
    text(bmp, 0, TXT_A, b);
    put_u(put_s(b, "LN"), (unsigned)(shown.len >> 8));
    text(bmp, 1, TXT_A, b);
    put_u(put_s(b, "LP"), (unsigned)(shown.g >> 8));
    text(bmp, 2, TXT_A, b);
    put_u(put_s(b, "LF"), (unsigned)shown.lfin);
    text(bmp, 3, TXT_A, b);
    if (shown.llen)
        put_u(put_s(b, "LL"), (unsigned)shown.llen);
    else
        put_s(b, "LL END");
    text(bmp, 0, TXT_E, b);
    text(bmp, 1, TXT_E, shown.lmod < 64 ? "HOLD" : "SUS");
    text(bmp, 2, TXT_E, shown.dir < 64 ? "FWD" : "ALT");
    text(bmp, 3, TXT_E, shown.play < 4 ? plays[shown.play] : "?");
}

void digicmi_wave_draw(void *bmp, void *ctrl)
{
    const volatile uint32_t *e;
    const short *pcm;
    uint32_t len;
    int c, a, b, looped, le, endv, loopv;
    (void)ctrl;
    if (!shown.on)
        return;
    shown.on = 0;
    fw_fillrect(bmp, 0, 0, WCOLS - 1, SCREEN_TOP, 0);   /* the page's knobs and labels go */
    loop_text(bmp);
    if (shown.eng >= 64) {                       /* the spectral engine: a placeholder until it is built */
        fw_textf(bmp, fw_font5, 34, 37, -1, "SPECTRAL ENGINE");
        fw_textf(bmp, fw_font5, 38, 27, -1, "NOT BUILT YET");
        return;
    }
    e = (const volatile uint32_t *)(SMP_TAB + 16 * (uint32_t)shown.slot);
    pcm = (const short *)e[0];
    len = e[2];
    if ((uint32_t)pcm < 0x40000000u || len <= 248u) {    /* no sample: the slots' silent dummy */
        fw_fillrect(bmp, 0, WMID, WCOLS - 1, WMID, 1);
        return;
    }
    if (pcm != peaks_pcm || len != peaks_len) {
        make_peaks(pcm, len);
        peaks_pcm = pcm;
        peaks_len = len;
    }
    endv = clampi(shown.strt + shown.len, 0, FULL);
    loopv = clampi((shown.g ? clampi(shown.g - 1, 0, FULL) : shown.strt) + 2 * shown.lfin, 0, endv);
    looped = shown.play == 2;                    /* FWD.L: the only PLAY the loop controls act on */
    le = shown.llen ? loopv + shown.llen * (FULL / 128) : endv;
    if (le > endv)
        le = endv;
    a = col_of(loopv);
    b = col_of(le);
    for (c = 0; c < WCOLS; c++)                  /* solid inside the loop, dotted outside it */
        if (!looped || (c >= a && c <= b) || !(c & 1))
            fw_fillrect(bmp, c, WMID + lo[c], c, WMID + hi[c], 1);
    c = col_of(shown.strt);                      /* STRT and END: ticks under the wave */
    fw_fillrect(bmp, c, WBOT - 2, c, WBOT - 1, 1);
    c = col_of(endv);
    fw_fillrect(bmp, c, WBOT - 2, c, WBOT - 1, 1);
    if (looped) {                                /* a line and an inward flag at each end of the loop */
        marker(bmp, a);
        marker(bmp, b);
        fw_fillrect(bmp, a, WTOP - 1, clampi(a + 3, 0, WCOLS - 1), WTOP, 1);
        fw_fillrect(bmp, clampi(b - 3, 0, WCOLS - 1), WTOP - 1, b, WTOP, 1);
    }
}

#define EXTRA(sn, ln, d, f, c) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .cc = c, .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = f }

static const struct cm_src_page pages[2] = {
    {"CMI", {0, EX_8BIT, EX_ENG, 3, B, B, B, 7}, 0, {0, 0, 0}},
    {"CMI LOOP", {4, 5, 6, EX_LFIN, EX_LLEN, EX_LMOD, EX_DIR, 1}, 0, {0, 0, 0}},
};

const struct cm_ui32 digicmi_page = {
    /* With an extras page the knobs are the params machine's (SLICE's: E SLICE, F LEN, G GRID), so E-G carry
     * ONESHOT's look, name, range and default (the firmware's range table, 0x401aa0a4 on 1.54, 52 bytes an id:
     * ONESHOT 0x6c-0x73, SLICE 0x84-0x8b); A-D and H are the same in both. */
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
        EXTRA("8BIT", "8-Bit Sample", 0, fmt_8bit, 28),
        EXTRA("ENG", "Engine", 0, fmt_eng, 29),
        /* 14: on no page, but machine-pages refuses the whole page unless all seven are valid. It is slot 52,
         * kitstore's marker, so its default is the marker itself (KS_MARK, 0x4b53) and a switch keeps it. */
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00,
          .def = 0x4b53 },
    },
    .page_count = 2, .pages = pages,
};
