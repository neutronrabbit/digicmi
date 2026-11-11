/* digicmi: the CMI machine's two SRC pages (machine-pages 1.11, cm_ui_v33 with CM_SRC_WAVE).
 *
 * Page 1 "CMI", knobs:   A TUNE, B 8BIT, C ENG, D SAMP / E-G blank, H LEV.
 * Page 2, SRC again:     the stock waveform view ONESHOT has (the firmware draws the sample and its STRT and
 *                        LOOP marks), with ONESHOT's layout on the encoders and as its labels:
 *                        A PLAY, B LLEN, C LMOD, D LFIN / E STRT, F LEN, G LOOP, H LEV. SRC again: page 1.
 * PLAY: REV, REV.L, FWD.L, FWD, and P (ping-pong between the loop points; cmi_loop.c). TUNE, PLAY, SAMP,
 * STRT, LEN, LOOP and LEV are the sound's slots 17-24 (ONESHOT's meaning: CMI renders as ONESHOT). The rest
 * are machine-pages' extra values in slots 46-51 (p-locks, LFOs, copy/paste, the project save):
 *   LLEN  loop length: 0 = to the end (the stock loop), else LOOP + LLEN/128 of the sample
 *   LMOD  HOLD (loops on after release) or SUS (loops while held, then plays on to the end, or the start)
 *   LFIN  fine loop start: one LOOP step divided into 128
 *   8BIT  OFF/ON: the sample played at 8 bits (cmi_loop.c, digicmi_render)
 *   ENG   SMPL, or SPEC: the spectral synth, not built yet (its voices are silent)
 * Extra 10 (slot 48, 0.3-0.6's DIR) and 14 (slot 52, Digi Poly kitstore's marker) are on no page. BR is on
 * no page (stock BR crushes rate and bits together). Extras need SLICE's parameter bank (params 3). */
#include "digitakt-mk1/core3.h"

#define EX_LLEN 8
#define EX_LMOD 9
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

#define EXTRA(sn, ln, d, f, c) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .cc = c, .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = f }

static const struct cm_src_page pages[2] = {
    {"CMI", {0, EX_8BIT, EX_ENG, 3, B, B, B, 7}, 0, {0, 0, 0}},
    {"CMI LOOP", {1, EX_LLEN, EX_LMOD, EX_LFIN, 4, 5, 6, 7}, 0, {0, 0, 0}},   /* the stock waveform view */
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
        EXTRA("LLEN", "Loop Length", 0, fmt_llen, 24),
        EXTRA("LMOD", "Loop Mode", 0, fmt_lmod, 25),
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00 },  /* 0.6's DIR */
        EXTRA("LFIN", "Loop Start Fine", 0, fmt_num, 27),
        EXTRA("8BIT", "8-Bit Sample", 0, fmt_8bit, 28),
        EXTRA("ENG", "Engine", 0, fmt_eng, 29),
        /* 14: on no page, but machine-pages refuses the whole page unless all seven are valid. It is slot 52,
         * kitstore's marker, so its default is the marker itself (KS_MARK, 0x4b53) and a switch keeps it. */
        { .name = "-", .lname = "Unused", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, .max = 0x7f00,
          .def = 0x4b53 },
    },
    .page_count = 2, .reserved = {CM_SRC_WAVE, 0, 0}, .pages = pages,
};
