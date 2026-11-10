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
        EXTRA("LLEN", "Loop Length", 0, fmt_llen, 24),
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
