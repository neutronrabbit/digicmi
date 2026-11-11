/* digicmi: the CMI machine's two SRC pages (machine-pages 1.4+, cm_ui_v33).
 *
 * Page 1 "CMI":       A TUNE, B RETRO (switch), C ENG (switch), D SAMP / E SPRD, F-G blank, H LEV.
 * Page 2 "CMI LOOP":  drawn here in the Digitakt II's waveform-page style (digicmi_wave_draw): value boxes
 *                     for A PLAY, (B empty), C LMOD, D LFIN along the top, the sample's waveform with dotted
 *                     STRT, END, loop-start and loop-end marks, and E STRT, F LEN, G LOOP, H LEV (a slider)
 *                     along the bottom. The page's SMP box and LEV fader stay on the left.
 * PLAY: REV, REV.L, FWD.L, FWD, and P (ping-pong between the loop points; cmi_loop.c). TUNE, PLAY, SAMP,
 * STRT, LEN, LOOP and LEV are the sound's slots 17-24 (ONESHOT's meaning: CMI renders as ONESHOT). The rest
 * are machine-pages' extra values in slots 46-51 (p-locks, LFOs, copy/paste, the project save):
 *   (LLEN, extra 8 / slot 46: on no page since 0.13; the loop runs from LOOP to END)
 *   LMOD  HOLD (loops on after release) or SUS (loops while held, then plays on to the end, or the start)
 *   LFIN  fine loop start: one LOOP step divided into 128
 *   RETRO a switch: the sample as a CMI Series I voice plays it: 8 bits at 24 kHz, its clock following the
 *         note's pitch (cmi_loop.c, digicmi_render)
 *   ENG   a switch: SMPL, or SPEC (the spectral synth, not built yet: its voices are silent)
 * The two switches are 0 (off) or 127 (on): a turn either way flips them at once (poly.c, digipoly_setparam,
 * snaps the value the knob sends; machine-pages wants every extra 0-127). Extra 10 (slot 48, once DIR) and 14 (slot
 * 52, Digi Poly kitstore's marker) are on no page; BR is on no page (stock BR crushes rate and bits). */
#include "digitakt-mk1/core3.h"

#define EX_LLEN 8
#define EX_LMOD 9
#define EX_LFIN 11
#define EX_8BIT 12
#define EX_ENG  13
#define EX_SPRD 10
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

static int32_t fmt_lmod(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 64 ? (ctx ? "HOLD (LOOP ON)" : "HOLD") : (ctx ? "SUSTAIN" : "SUS"));
    return 1;
}

/* Every pop-up text (ctx 1) must fit machine-pages' 16-byte buffer: 15 characters at most. A longer one
 * overwrites its memory (0.8-0.10's "SPECTRAL (NOT BUILT)", 20, froze the unit when ENG was turned on). */
static int32_t fmt_sprd(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    v = (v >> 8) & 127;
    if (!v)
        put_s(buf, ctx ? "OFF (CENTRED)" : "OFF");
    else
        put_u(buf, (unsigned)v);
    return 1;
}

static int32_t fmt_num(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    put_u(buf, (unsigned)((v >> 8) & 127));
    return 1;
}

static int32_t fmt_retro(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 96 ? "OFF" : (ctx ? "8-BIT 24KHZ" : "ON"));
    return 1;
}

static int32_t fmt_eng(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 96 ? (ctx ? "SAMPLE" : "SMPL") : (ctx ? "SPECTRAL" : "SPEC"));   /* <= 15: pop[16] */
    return 1;
}

/* STRT, LEN and LOOP as ONESHOT shows them, 0.00-120.00 (the borrowed SLICE bank would format GRID's) */
static int32_t fmt_pos(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    unsigned f = ((unsigned)v & 255u) * 100u / 256u;
    char *b;
    (void)ctx; (void)m;
    b = put_u(buf, (unsigned)(v >> 8) & 255u);
    *b++ = '.';
    *b++ = (char)('0' + f / 10);
    *b++ = (char)('0' + f % 10);
    *b = 0;
    return 1;
}

static int32_t fmt_play(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    static const char *const s[5] = {"REV", "REV.L", "FWD.L", "FWD", "P"};
    static const char *const l[5] = {"REVERSE", "REVERSE LOOP", "FORWARD LOOP", "FORWARD", "PING-PONG LOOP"};
    (void)m;
    v = (v >> 8) & 255;
    put_s(buf, v < 5 ? (ctx ? l[v] : s[v]) : "?");
    return 1;
}

static int page2_age = 1000;                    /* UI ticks since page 2 was last drawn (1000: not on show) */

/* ---- the switches ----------------------------------------------------------------------------- */
#define SW_OFF   0                               /* poly.c's digipoly_setparam snaps a turn to 0 or 127 */
#define SW_ON    0x7f00

/* A toggle drawn in the knob's 17 x 17 place (x + 1 .. x + 17, y .. y + 16; y = 0 is the bottom row). */
static int32_t switch_draw(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int on = ((value >> 8) & 127) >= 96, x0 = x + 2, x1 = x + 16, y0 = y + 5, y1 = y + 11;
    (void)flag;
    page2_age = 1000;                                           /* page 1 is on show, not page 2 */
    fw_fillrect(bmp, x0 + 1, y0, x1 - 1, y0, 1);                /* a rounded outline */
    fw_fillrect(bmp, x0 + 1, y1, x1 - 1, y1, 1);
    fw_fillrect(bmp, x0, y0 + 1, x0, y1 - 1, 1);
    fw_fillrect(bmp, x1, y0 + 1, x1, y1 - 1, 1);
    if (on) {                                                   /* on: filled, the knob to the right */
        fw_fillrect(bmp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
        fw_fillrect(bmp, x1 - 5, y0 + 2, x1 - 2, y1 - 2, 0);
    } else                                                      /* off: the knob to the left */
        fw_fillrect(bmp, x0 + 2, y0 + 2, x0 + 5, y1 - 2, 1);
    return 1;
}

/* ---- page 2: the Digitakt II-style waveform page ------------------------------------------------ *
 * LMOD's knob exists only on page 2, so its graphic's callback says the page is being drawn and takes the
 * values it shows (a held step's locks included); ev_draw, after the frame is composed, paints the page
 * over the knobs (x 22 on; the SMP box and LEV fader on the left stay). The encoders turn the values. */
#ifdef OS154
#define SMP_TAB  0x4031a3a0u
#else
#define SMP_TAB  0x403193a0u
#endif
#define FULL     0x7800                         /* 120.0: STRT, LEN and LOOP's scale */
#define PX0      22                             /* the page's own drawing starts right of the LEV fader */
#define WX0      24                             /* the waveform: 102 columns */
#define WCOLS    102
#define WMID     25                             /* y = 0 is the bottom row: wave 11-39, labels 0-7 */
#define WHALF    14
#define BOX_Y0   44                             /* the top row's value boxes: y 44-52 */
#define BOX_Y1   52
#define WPROBES  64
static const int8_t col_x[4] = {31, 57, 83, 109};   /* the encoders' columns (the knobs' centres) */

static struct { int on, slot, play, strt, len, g, lfin, lmod; } shown;
extern int digicmi_voice_pos(int v, int *pos, uint32_t *len);   /* cmi_loop.c: a sounding CMI voice's playhead */
static signed char lo[WCOLS], hi[WCOLS];
static const short *peaks_pcm;
static uint32_t peaks_len;

static int clampi(int x, int a, int b)
{
    return x < a ? a : x > b ? b : x;
}

static int32_t lmod_draw(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    static const int8_t ks[] = {1, 3, 4, 5, 6, EX_LFIN};
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
    shown.lfin = (v[5] >> 8) & 127;
    shown.lmod = (value >> 8) & 127;
    shown.on = 1;
    page2_age = 0;
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
    for (c = 0; c < WCOLS; c++) {
        lo[c] = (signed char)(lo[c] * WHALF / top);
        hi[c] = (signed char)(hi[c] * WHALF / top);
    }
}

static int col_of(int val)
{
    return val * (WCOLS - 1) / FULL;
}

/* A dotted mark the waveform's height, every other pixel, shown against the wave (inverted over it). */
static void dotted(void *bmp, int c)
{
    int y, x = WX0 + c;
    for (y = WMID - WHALF - 1; y <= WMID + WHALF + 1; y += 2) {
        int in = y >= WMID + lo[c] && y <= WMID + hi[c];
        fw_fillrect(bmp, x, y, x, y, !in);
    }
}

/* A value box in the top row, centred on encoder column k: outlined, the text inside. */
static void vbox(void *bmp, int k, const char *s, int dotted_frame)
{
    int cx = col_x[k], x0 = cx - 11, x1 = cx + 11, n = 0, x;
    while (s[n])
        n++;
    if (dotted_frame) {
        for (x = x0 + 1; x < x1; x += 2) {
            fw_fillrect(bmp, x, BOX_Y0, x, BOX_Y0, 1);
            fw_fillrect(bmp, x, BOX_Y1, x, BOX_Y1, 1);
        }
    } else {
        fw_fillrect(bmp, x0 + 1, BOX_Y0, x1 - 1, BOX_Y0, 1);
        fw_fillrect(bmp, x0 + 1, BOX_Y1, x1 - 1, BOX_Y1, 1);
        fw_fillrect(bmp, x0, BOX_Y0 + 1, x0, BOX_Y1 - 1, 1);
        fw_fillrect(bmp, x1, BOX_Y0 + 1, x1, BOX_Y1 - 1, 1);
    }
    fw_textf(bmp, fw_font5, cx - 2 * n + 1, BOX_Y0 + 2, -1, "%s", s);
}

/* UI tick: while page 2 is what the SRC page last drew and a CMI note sounds (or just stopped), ask for
 * a frame each tick so the playheads move. */
void digicmi_page_tick(void *ctrl)
{
    static int was;
    int v, pos, any = 0;
    uint32_t vl;
    if (page2_age < 1000)
        page2_age++;
    for (v = 0; v < 8; v++)
        any |= digicmi_voice_pos(v, &pos, &vl);
    if (page2_age < 1000 && (any || was) && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;
    was = any;
}

void digicmi_wave_draw(void *bmp, void *ctrl)
{
    static const char *const plays[5] = {"REV", "REV.L", "FWD.L", "FWD", "P"};
    const volatile uint32_t *e;
    const short *pcm;
    uint32_t len;
    char b[12];
    int c, endv, loopv, le, looped;
    (void)ctrl;
    if (!shown.on)
        return;
    shown.on = 0;
    fw_fillrect(bmp, PX0, 8, 127, 54, 0);        /* the page's knobs go; its bottom labels stay */

    vbox(bmp, 0, shown.play < 5 ? plays[shown.play] : "?", 0);
    vbox(bmp, 1, "", 1);                         /* B: an empty place, as the Digitakt II draws one */
    vbox(bmp, 2, shown.lmod < 64 ? "HOLD" : "SUS", 0);
    put_u(put_s(b, "F"), (unsigned)shown.lfin);
    vbox(bmp, 3, b, !shown.lfin);


    e = (const volatile uint32_t *)(SMP_TAB + 16 * (uint32_t)shown.slot);
    pcm = (const short *)e[0];
    len = e[2];
    if ((uint32_t)pcm < 0x40000000u || len <= 248u) {    /* no sample: the slots' silent dummy */
        fw_fillrect(bmp, WX0, WMID, WX0 + WCOLS - 1, WMID, 1);
        for (c = 0; c < WCOLS; c++)
            lo[c] = hi[c] = 0;
        peaks_pcm = 0;
    } else {
        if (pcm != peaks_pcm || len != peaks_len) {
            make_peaks(pcm, len);
            peaks_pcm = pcm;
            peaks_len = len;
        }
        for (c = 0; c < WCOLS; c++)
            fw_fillrect(bmp, WX0 + c, WMID + lo[c], WX0 + c, WMID + hi[c], 1);
    }
    endv = clampi(shown.strt + shown.len, 0, FULL);
    loopv = clampi((shown.g ? clampi(shown.g - 1, 0, FULL) : shown.strt) + 2 * shown.lfin, 0, endv);
    le = endv;                                   /* the loop runs to END */
    for (c = 0; c < 8; c++) {                    /* the playheads: an arrow under the wave for each note */
        int pos;
        uint32_t vl;
        if (digicmi_voice_pos(c, &pos, &vl) && vl > 256 && pos >= 0 && (uint32_t)pos < vl) {
            int x = WX0 + (int)(((uint32_t)pos >> 8) * (WCOLS - 1) / (vl >> 8));
            fw_fillrect(bmp, x, 10, x, 10, 1);
            fw_fillrect(bmp, clampi(x - 1, PX0, 127), 9, clampi(x + 1, PX0, 127), 9, 1);
            fw_fillrect(bmp, clampi(x - 2, PX0, 127), 8, clampi(x + 2, PX0, 127), 8, 1);
        }
    }
    looped = shown.play == 1 || shown.play == 2 || shown.play == 4;
    dotted(bmp, col_of(shown.strt));             /* STRT and END */
    dotted(bmp, col_of(endv));
    if (looped) {                                /* the loop: dotted, with a short bar on top of each end */
        int a = col_of(loopv), z = col_of(le);
        dotted(bmp, a);
        dotted(bmp, z);
        fw_fillrect(bmp, WX0 + a, WMID + WHALF + 2, clampi(WX0 + a + 2, 0, 127), WMID + WHALF + 2, 1);
        fw_fillrect(bmp, clampi(WX0 + z - 2, 0, 127), WMID + WHALF + 2, WX0 + z, WMID + WHALF + 2, 1);
    }
}

#define EXTRA(sn, ln, d, f, c) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .cc = c, .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = f }

static const struct cm_src_page pages[2] = {
    {"CMI", {0, EX_8BIT, EX_ENG, 3, EX_SPRD, B, B, 7}, 0, {0, 0, 0}},
    {"CMI LOOP", {1, B, EX_LMOD, EX_LFIN, 4, 5, 6, B}, 0, {0, 0, 0}},   /* drawn by digicmi_wave_draw */
};

const struct cm_ui32 digicmi_page = {
    /* With an extras page the knobs are the params machine's (SLICE's: E SLICE, F LEN, G GRID), so E-G carry
     * ONESHOT's look, name, range and default (the firmware's range table, 0x401aa0a4 on 1.54, 52 bytes an id:
     * ONESHOT 0x6c-0x73, SLICE 0x84-0x8b); A-D and H are the same in both. */
    .base = { .abi = cm_ui_v33, .page_from = 0,
        .knob = {
            [1] = { .name = "PLAY", .lname = "Play Mode", .look = 0x6d, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x400, .def = 0x300, .fmt = fmt_play },   /* ONESHOT's 0-3, and 4: P */
            [4] = { .name = "STRT", .lname = "Sample Start", .look = 0x70, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7800, .def = 0, .fmt = fmt_pos },
            [5] = { .name = "LEN", .lname = "Sample Length", .look = 0x71, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7800, .def = 0x7800, .fmt = fmt_pos },
            [6] = { .name = "LOOP", .lname = "Loop Position", .look = 0x72, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 0x7802, .def = 0, .fmt = fmt_pos },
        } },
    .extra = {
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00 },  /* 0.3-0.12's LLEN */
        { .name = "LMOD", .lname = "Loop Mode", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 25, .min = 0, .max = 127 << 8, .def = 0, .fmt = fmt_lmod, .draw = lmod_draw },
        EXTRA("SPRD", "Stereo Spread", 0, fmt_sprd, 26),     /* slot 48 (0.3-0.6's DIR): cmi_loop.c */
        EXTRA("LFIN", "Loop Start Fine", 0, fmt_num, 27),
        { .name = "RETRO", .lname = "Retro", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 28, .min = 0, .max = 127 << 8, .def = SW_OFF, .fmt = fmt_retro, .draw = switch_draw },
        { .name = "ENG", .lname = "Engine", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 29, .min = 0, .max = 127 << 8, .def = SW_OFF, .fmt = fmt_eng, .draw = switch_draw },
        /* 14: on no page, but machine-pages refuses the whole page unless all seven are valid. It is slot 52,
         * kitstore's marker, so its default is the marker itself (KS_MARK, 0x4b53) and a switch keeps it. */
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00,
          .def = 0x4b53 },
    },
    .page_count = 2, .pages = pages,
};
