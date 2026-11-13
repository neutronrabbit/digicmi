/* digicmi: the CMI machine's two SRC pages (machine-pages 1.4+, cm_ui_v33).
 *
 * Page 1 "CMI":       A TUNE, B RETRO (a dial: off, 16/44.1, 8/32, 8/24, 8/16, 8/8), C ENG (switch), D SAMP /
 *                     E SPRD, F-G blank, H LEV.
 * Page 2 "CMI LOOP":  drawn here in the Digitakt II's waveform-page style (digicmi_wave_draw): value boxes
 *                     along the top, the sample's waveform with dotted STRT, END and loop marks, labels along
 *                     the bottom. ENG = SMPL: A PLAY, (B empty), C LMOD, (D empty) / E STRT, F LEN, G LOOP, H LEV.
 *                     ENG = TIME: A PLAY, B BLND, C SMEAR, D TIME / E STRT, F LEN, G LOOP, H LEV, with the 16
 *                     waves' places between STRT and END. The page's SMP box and LEV fader stay on the left.
 * PLAY: REV, REV.L, FWD.L, FWD, and P (ping-pong between the loop points; cmi_loop.c). TUNE, PLAY, SAMP,
 * STRT, LEN, LOOP and LEV are the sound's slots 17-24 (ONESHOT's meaning: CMI renders as ONESHOT). The rest
 * are machine-pages' extra values in slots 46-51 (p-locks, LFOs, copy/paste, the project save):
 *   (LLEN, extra 8 / slot 46: on no page since 0.13; the loop runs from LOOP to END)
 *   LMOD  (with ENG = TIME: SMEAR, timesynth.c) HOLD (loops on after release) or SUS (loops while held, then plays on to the end, or the start)
 *   TIME  (page 2 D, TIME only; was LFIN, the fine loop start, until 0.19)
 *   RETRO a switch: the sample as a CMI Series I voice plays it: 8 bits at 24 kHz, its clock following the
 *         note's pitch (cmi_loop.c, digicmi_render)
 *   ENG   a switch: SMPL, or TIME (the time synth, timesynth.c: single cycles cut from the sample, played in turn)
 *   BLND  (page 2 B, TIME only) 0 steps from wave to wave, 127 crossfades
 * The two switches are 0 (off) or 127 (on): a turn either way flips them at once (poly.c, digipoly_setparam,
 * snaps the value the knob sends; machine-pages wants every extra 0-127). Extra 10 (slot 48, once DIR) and 14 (slot
 * 52, Digi Poly kitstore's marker) are on no page; BR is on no page (stock BR crushes rate and bits). */
#include "digitakt-mk1/core3.h"

#define EX_LLEN 8
#define EX_LMOD 9
#define EX_LFIN 11                              /* slot 49: TIME (the time synth's) since 0.20; LFIN before */
#define EX_8BIT 12
#define EX_ENG  13
#define EX_SPRD 10
#define EX_LEND 14                              /* slot 52: LOOP END (0.31) */
#define EX_BLND 8                               /* the time synth's BLND (slot 46: 0.3-0.12's LLEN, 0.15's FORMANT) */
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

/* ENG = TIME on track 1's sound (CMI is only ever on track 1): C and G mean SMEAR and TIME there. */
static int time_mode(void)
{
    uint8_t *kit = fw_kit, *snd;
    if (!kit)
        return 0;
    snd = FW_SOUND(kit, 0);
    return snd[FW_SOUND_MACHINE] == 40 && ((*(int16_t *)(snd + 0x14 + 2 * 51) >> 8) & 127) >= 96;
}

static int32_t fmt_lmod(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    if (time_mode()) {                          /* TIME: SMEAR, 0-127 */
        v = (v >> 8) & 127;
        if (!v)
            put_s(buf, "OFF");
        else
            put_u(buf, (unsigned)v);
        return 1;
    }
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

/* LOOP END (0.31, cmi_loop.c's lend_val): 0 = END, else 1-127 a place in the sample shown on LOOP's 0-120 scale */
static int lend_of(int32_t v)
{
    if (v & 0xff)
        return -1;
    v = (v >> 8) & 127;
    return v ? (int)v * 0x7800 / 127 : -1;
}

static int32_t fmt_pos(char *buf, int32_t v, int32_t ctx, int32_t m);
static int32_t fmt_lend(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    int k = lend_of(v);
    if (k < 0) {
        put_s(buf, ctx ? "END (TO LEN)" : "END");
        return 1;
    }
    return fmt_pos(buf, k, ctx, m);
}

static int32_t fmt_num(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    put_u(buf, (unsigned)((v >> 8) & 127));
    return 1;
}

/* RETRO, a dial since 0.23 (cmi_loop.c): six steps over 0-127 */
static int32_t fmt_retro(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    static const char *const s[6] = {"OFF", "16/44", "8/32", "8/24", "8/16", "8/8"};
    static const char *const l[6] = {"OFF 16BIT 48K", "16BIT 44.1KHZ", "8BIT 32KHZ", "8BIT 24KHZ", "8BIT 16KHZ",
                                     "8BIT 8KHZ"};          /* <= 15: pop[16] */
    int k = (((v >> 8) & 127) * 6) >> 7;
    (void)m;
    put_s(buf, ctx ? l[k] : s[k]);
    return 1;
}

static int32_t fmt_eng(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)m;
    put_s(buf, ((v >> 8) & 127) < 96 ? (ctx ? "SAMPLE" : "SMPL") : (ctx ? "TIME SYNTH" : "TIME"));   /* <= 15: pop[16] */
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

/* Page 2 D with ENG = TIME: the pass's time, 0.05 s x 2^(T/15), T = D x 120 / 127 (timesynth.c), as seconds */
static const unsigned short exp2n[17] = {       /* 2^(-i/16), Q15 */
    32768, 31379, 30048, 28774, 27554, 26386, 25268, 24196, 23170, 22188, 21247, 20347,
    19484, 18658, 17867, 17109, 16384
};

static char *put_secs(char *buf, unsigned g)
{
    unsigned ms;
    char *b;
    g = g > 120 ? 120 : g;
    ms = (50u << (g / 15)) * 32768u / exp2n[(g % 15) * 16 / 15];
    b = buf;
    if (ms < 10000) {                           /* 0.05-9.99 s */
        b = put_u(b, ms / 1000);
        *b++ = '.';
        *b++ = (char)('0' + ms / 100 % 10);
        *b++ = (char)('0' + ms / 10 % 10);
    } else {                                    /* 10.0-12.8 s */
        b = put_u(b, ms / 1000);
        *b++ = '.';
        *b++ = (char)('0' + ms / 100 % 10);
    }
    *b++ = 'S';
    *b = 0;
    return b;
}

/* B and D are the time synth's (BLND, TIME): in sample mode they have no box, don't move (poly.c's set_param
 * keeps their values) and show an empty pop-up: no name (the names below follow the engine) and no value. */
static int32_t fmt_blnd(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    if (!time_mode())
        put_s(buf, "");
    else
        put_u(buf, (unsigned)((v >> 8) & 127));
    return 1;
}

static int32_t fmt_time(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    if (!time_mode())
        put_s(buf, "");                         /* TIME only (sample mode's D is empty since 0.20) */
    else
        put_secs(buf, ((unsigned)(v >> 8) & 127u) * 120u / 127u);
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

void digicmi_ts_tick(void);
int digicmi_ts_pos(int v);

/* ---- the switches ----------------------------------------------------------------------------- */
#define SW_OFF   0                               /* poly.c's digipoly_setparam snaps a turn to 0 or 127 */
#define SW_ON    0x7f00

static int page1_drawn;                         /* a switch was drawn: this frame is page 1 */

/* A toggle drawn in the knob's 17 x 17 place (x + 1 .. x + 17, y .. y + 16; y = 0 is the bottom row). */
static int32_t switch_draw(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int on = ((value >> 8) & 127) >= 96, x0 = x + 2, x1 = x + 16, y0 = y + 5, y1 = y + 11;
    (void)flag;
    page1_drawn = 1;
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

static struct { int on, slot, play, strt, len, g, lfin, lmod, blnd, eng, lend; } shown;
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
    static const int8_t ks[] = {1, 3, 4, 5, 6, EX_LFIN, EX_BLND, EX_ENG, EX_LEND};
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
    shown.blnd = (v[6] >> 8) & 127;
    shown.eng = (v[7] >> 8) & 127;
    shown.lend = lend_of(v[8]);
    shown.lmod = (value >> 8) & 127;
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

/* UI tick: while page 2 is on screen and a CMI note sounds (or just stopped), ask for a frame every other
 * tick (15 a second) so the playheads move. "On screen" is whether the last frame drawn, on any screen, was
 * page 2 (page2_live, set by digicmi_wave_draw, which ev_draw calls for every frame), so the requests stop
 * as soon as another screen is drawn. (0.11-0.20 kept asking for up to 1000 ticks, ~33 s, after page 2 was
 * last drawn, so the screen that followed - the sample browser, say - was redrawn 30 times a second while
 * notes sounded, and the UI felt sluggish.) */
static int page2_live;
/* The pop-up names of B, C and D follow the engine (machine-pages reads a knob's lname at each pop-up): in sample
 * mode C is "Loop Mode" and B, D have none; in the time synth "Wave Blend", "Smear", "Wave Pass Time". */
static char lname_b[16] = "", lname_c[16] = "Loop Mode", lname_d[16] = "";
static int names_for = -1;

static void names_follow_engine(void)
{
    int tm = time_mode();
    if (tm == names_for)
        return;
    names_for = tm;
    put_s(lname_b, tm ? "Wave Blend" : "");
    put_s(lname_c, tm ? "Smear" : "Loop Mode");
    put_s(lname_d, tm ? "Wave Pass Time" : "");
}
/* BLND and SMEAR's boxes (0.33): the name, and the number for VAL_TICKS UI ticks after the value changes (a
 * prefix letter, 0.18-0.32, read as a digit in the 3 x 5 font: S as 5, B as 8). */
#define VAL_TICKS 45
static int blnd_age = 1000, smear_age = 1000, last_blnd = -1, last_smear = -1;
static int time_age = 1000, last_time = -1;     /* TIME's box too: the name, the seconds while it changes */
static int cmi_live;                            /* the last frame drawn was the CMI's page 1 or 2 */
static int meter_shown = -2;                    /* the load the title bar shows */
extern volatile int digicmi_cpu_load;           /* cpumeter.c: the render's peak load, 256ths; -1 not measured */

/* The CPU meter in the title bar: "42%", white on black, in x 90-103 of its rows 1-8 (from the top). That
 * strip is free in both of the bar's states: the page's own title (black, its text on the left) and the
 * pattern name with the tempo (the empty left end of the tempo box). A long sample name in the page's title
 * is cut short there. Glyphs 3 x 5, drawn pixel by pixel, top row first. */
static const unsigned char glyph[12][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    {5, 1, 2, 4, 5},                                    /* 10: % */
    {0, 0, 7, 0, 0},                                    /* 11: - */
};

static void put_glyph(void *bmp, int x, int g)
{
    int r, c;
    for (r = 0; r < 5; r++)                             /* rows 2-6 from the top: y 61 down to 57 */
        for (c = 0; c < 3; c++)
            if ((glyph[g][r] >> (2 - c)) & 1)
                fw_fillrect(bmp, x + c, 61 - r, x + c, 61 - r, 0);
}

/* A knob's pop-up ("Retro=8BIT 24KHZ") fills the title bar, the meter's strip included: the meter stays away
 * for POPUP_TICKS UI ticks after any parameter is set (poly.c flags each set_param), then comes back. */
#define POPUP_TICKS 120                         /* measured: a pop-up lasts ~96-120 UI ticks */
extern volatile int digicmi_param_moved;
static int popup_hold;
/* 0.35: B and D turned in sample mode: their pop-up ("=", no name and no value) is painted over with a plain title
 * bar for the pop-up's life. The firmware always shows a turned knob's pop-up; ev_draw draws after it. Any other
 * knob turned meanwhile shows its pop-up as usual. */
extern volatile int digicmi_mute_pop;
static int mute_hold;
volatile int digicmi_uiticks;                   /* UI ticks, for the emulator test */

static void draw_meter(void *bmp)
{
    int load = digicmi_cpu_load, pc = load < 0 ? -1 : load * 100 / 256;
    pc = pc > 99 ? 99 : pc;
    meter_shown = load;
    if (popup_hold)
        return;
    fw_fillrect(bmp, 90, 55, 103, 62, 1);
    put_glyph(bmp, 91, pc < 0 ? 11 : pc / 10);
    put_glyph(bmp, 95, pc < 0 ? 11 : pc % 10);
    put_glyph(bmp, 99, 10);
}

void digicmi_page_tick(void *ctrl)
{
    static int was, odd;
    int v, pos, any = 0;
    uint32_t vl;
    digicmi_ts_tick();                          /* timesynth.c: the waves, when track 1's sample, STRT or LEN change */
    odd ^= 1;
    digicmi_uiticks++;
    names_follow_engine();
    if (blnd_age < 1000 && ++blnd_age == VAL_TICKS && page2_live && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;    /* BLND's box back to its name */
    if (smear_age < 1000 && ++smear_age == VAL_TICKS && page2_live && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;    /* SMEAR's box back to its name */
    if (time_age < 1000 && ++time_age == VAL_TICKS && page2_live && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;    /* TIME's box back to its name */
    if (digicmi_mute_pop) {
        digicmi_mute_pop = 0;
        mute_hold = POPUP_TICKS;
        if (ctrl)
            *((unsigned char *)ctrl + 0x20) = 1;
    } else if (digicmi_param_moved && mute_hold) {
        mute_hold = 0;                          /* another knob: its pop-up shows */
    } else if (mute_hold && --mute_hold == 0 && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;
    if (digicmi_param_moved) {
        digicmi_param_moved = 0;
        popup_hold = POPUP_TICKS;
    } else if (popup_hold && --popup_hold == 0 && cmi_live && ctrl)
        *((unsigned char *)ctrl + 0x20) = 1;    /* the meter comes back */
    if (cmi_live && ctrl && digicmi_cpu_load != meter_shown)
        *((unsigned char *)ctrl + 0x20) = 1;    /* a CMI page is up and the meter's value moved (2 a second at most) */
    if (!page2_live || !ctrl) {                 /* page 2 isn't on screen: nothing to animate */
        was = 0;
        return;
    }
    for (v = 0; v < 8; v++)
        any |= digicmi_voice_pos(v, &pos, &vl);
    if ((any || was) && odd)
        *((unsigned char *)ctrl + 0x20) = 1;
    if (odd)
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
    page2_live = shown.on;                       /* this frame is page 2, or another screen */
    cmi_live = shown.on || page1_drawn;
    page1_drawn = 0;
    if (mute_hold && shown.on) {                 /* a muted pop-up: a plain title bar over it */
        fw_fillrect(bmp, 18, 55, 127, 62, 1);
        fw_fillrect(bmp, 18, 63, 126, 63, 1);
    }
    if (cmi_live)
        draw_meter(bmp);
    if (!shown.on)
        return;
    shown.on = 0;
    fw_fillrect(bmp, PX0, 8, 127, 54, 0);        /* the page's knobs go; its bottom labels stay */
    vbox(bmp, 0, shown.play < 5 ? plays[shown.play] : "?", 0);
    if (shown.eng >= 96) {                       /* TIME: B BLND, C SMEAR (LMOD's slot), D TIME (slot 49) */
        if (shown.blnd != last_blnd) {           /* changed: show the number a while */
            if (last_blnd >= 0)
                blnd_age = 0;
            last_blnd = shown.blnd;
        }
        if (blnd_age < VAL_TICKS)
            put_u(b, (unsigned)shown.blnd);
        else
            put_s(b, "BLND");
        vbox(bmp, 1, b, 0);
        if (shown.lmod != last_smear) {
            if (last_smear >= 0)
                smear_age = 0;
            last_smear = shown.lmod;
        }
        if (smear_age < VAL_TICKS)
            put_u(b, (unsigned)shown.lmod);
        else
            put_s(b, "SMEAR");
        vbox(bmp, 2, b, 0);
        if (shown.lfin != last_time) {
            if (last_time >= 0)
                time_age = 0;
            last_time = shown.lfin;
        }
        if (time_age < VAL_TICKS)
            put_secs(b, (unsigned)shown.lfin * 120u / 127u);
        else
            put_s(b, "TIME");
        vbox(bmp, 3, b, 0);
    } else {
                                                 /* B and D: nothing (the time synth's; 0.34) */
        vbox(bmp, 2, shown.lmod < 64 ? "HOLD" : "SUS", 0);

    }

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
    loopv = clampi(shown.g ? clampi(shown.g - 1, 0, FULL) : shown.strt, shown.eng >= 96 ? shown.strt : 0, endv);
    looped = shown.play == 1 || shown.play == 2 || shown.play == 4;
    if (shown.eng >= 96) {                       /* TIME: the 16 waves' places, a tick above the wave each,
                                                    STRT and END dotted, an arrow per note at its place in the pass */
        int a = col_of(shown.strt), z = col_of(endv);
        for (c = 0; c < 16; c++) {
            int x = WX0 + a + (z - a) * c / 15;
            fw_fillrect(bmp, x, WMID + WHALF + 2, x, WMID + WHALF + 3, 1);
        }
        dotted(bmp, a);
        dotted(bmp, z);
        for (c = 0; c < 8; c++) {
            int pos;
            uint32_t vl;
            if (digicmi_voice_pos(c, &pos, &vl)) {
                int x = WX0 + a + (z - a) * digicmi_ts_pos(c) / 65535;
                fw_fillrect(bmp, x, 10, x, 10, 1);
                fw_fillrect(bmp, clampi(x - 1, PX0, 127), 9, clampi(x + 1, PX0, 127), 9, 1);
                fw_fillrect(bmp, clampi(x - 2, PX0, 127), 8, clampi(x + 2, PX0, 127), 8, 1);
            }
        }
        if (looped && shown.lend >= 0) {         /* LOOP END's place, dotted with a bar on top */
            int l = col_of(clampi(shown.lend, loopv, endv));
            dotted(bmp, l);
            fw_fillrect(bmp, clampi(WX0 + l - 2, 0, 127), WMID + WHALF + 2, WX0 + l, WMID + WHALF + 2, 1);
        }
        if (looped) {                            /* LOOP's place, dotted with a bar on top, as the sample's */
            int l = col_of(loopv);
            dotted(bmp, l);
            fw_fillrect(bmp, WX0 + l, WMID + WHALF + 2, clampi(WX0 + l + 2, 0, 127), WMID + WHALF + 2, 1);
        }
        return;
    }
    le = shown.lend >= 0 ? clampi(shown.lend, loopv, endv) : endv;   /* the loop runs to LOOP END, or END */
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
    {"CMI LOOP", {1, EX_BLND, EX_LMOD, EX_LFIN, 4, 5, 6, EX_LEND}, 0, {0, 0, 0}},   /* drawn by digicmi_wave_draw;
                                                                                   H LOOP END (LEV on page 1) */
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
        EXTRA("BLND", lname_b, 127, fmt_blnd, 24),          /* slot 46: the time synth's (0.3-0.12's LLEN) */
        { .name = "LMOD", .lname = lname_c, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 25, .min = 0, .max = 127 << 8, .def = 0, .fmt = fmt_lmod, .draw = lmod_draw },
        EXTRA("SPRD", "Stereo Spread", 0, fmt_sprd, 26),     /* slot 48 (0.3-0.6's DIR): cmi_loop.c */
        EXTRA("TIME", lname_d, 64, fmt_time, 27),   /* slot 49, TIME only (0.3-0.19's LFIN; timesynth.c) */
        EXTRA("RETRO", "Retro", 0, fmt_retro, 28),         /* slot 50: a dial of six steps (a switch 0.8-0.22) */
        { .name = "ENG", .lname = "Engine", .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
          .cc = 29, .min = 0, .max = 127 << 8, .def = SW_OFF, .fmt = fmt_eng, .draw = switch_draw },
        /* 14: on no page, but machine-pages refuses the whole page unless all seven are valid. Slot 52: Digi
         * Poly's marker until 0.19, the time synth's ROOT 0.20-0.22 (removed; the time synth is always AUTO). */
        EXTRA("LEND", "Loop End", 0, fmt_lend, 30),          /* slot 52: LOOP END (0.31); 0 = END */
    },
    .page_count = 2, .pages = pages,
};
