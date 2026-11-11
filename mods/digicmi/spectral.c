/* digicmi: the spectral engine (ENG = SPEC), after the CMI's Fourier synthesis: each sound's timbre is a
 * series of single-cycle waveforms computed from harmonic amplitudes, played back as a wavetable while the
 * timbre morphs from a start spectrum to an end spectrum.
 *
 * Tables: SPEC_FRAMES frames of SPEC_LEN samples (Q15, one extra sample for the wrap), from 32 harmonics. They
 * are rebuilt in the UI task when track 1's spectral settings change, into the bank the render is not reading,
 * then switched in one store. The settings (track 1's CMI sound; CMI is only ever on track 1):
 *   E, slot 21 (0-120)  START brightness: harmonic h at h^-k, k from 3 (dark) to 0.3 (bright)
 *   F, slot 22 (0-120)  END brightness
 *   D, slot 49 (0-127)  ODD/EVEN: 64 even, lower takes the even harmonics out, higher the odd (above the 1st)
 *   B, slot 46 (0-127)  FORMANT: 0 off, else a peak at harmonic 1 + B x 31 / 127
 * Per voice (cmi_loop.c's digicmi_render calls spec_voice), from the voice's own smoothed values:
 *   A, slot 18 (PLAY 0-4) MORPH: 3 FWD once, 0 REV once, 2 FWD.L loop, 1 REV.L loop, 4 P ping-pong
 *   G, slot 23 (0-120)  TIME of a whole sweep: 0.05 s x 2^(G/15), to about 13 s
 * The voice's pitch is its stock player's: while ENG = SPEC the player loops the track's sample silently
 * (cmi_loop.c) and its playhead's move per block is the pitch ratio, TUNE, bends and LFOs in; the root, MIDI
 * 60, plays at 261.63 Hz. The block it writes is scaled as the sample's would be (gain x 0.81), so the AMP
 * envelope, filter, pan, RETRO and SPREAD act on it as on a sample. */
#include "digitakt-mk1/core3.h"
#include "spec_tables.h"

typedef unsigned int u32;

#define SPEC_FRAMES 16
#define SPEC_LEN    256
#define LAST_FRAME  ((SPEC_FRAMES - 1) << 16)
#define ROOT_INC    91443u                          /* 261.6256 Hz / 48 kHz x 2^32, / 256 */

static short tab[2][SPEC_FRAMES][SPEC_LEN + 1];
static volatile int bank = -1;                      /* the bank the render reads; -1 none built yet */
static int built_key = -1;

/* Harmonic h's amplitude (Q15, a formant's peak can pass 1.0) for a brightness (0-120), ODD/EVEN, FORMANT. */
int digicmi_spec_amp(int h, int bright, int odd, int form)
{
    int k = 768 - bright * 691 / 120;               /* k in Q8: 3.0 down to 0.3 */
    int e = k * spec_lg[h] >> 8, a, f;
    if (e >> 8 > 15)
        return 0;
    a = spec_exp2n[(e & 255) >> 4] >> (e >> 8);     /* 2^-e */
    if (h > 1) {
        if (h & 1)                                  /* odd */
            a = odd <= 64 ? a : a * (127 - odd) / 63;
        else                                        /* even */
            a = odd >= 64 ? a : a * odd / 64;
    }
    if (form) {
        f = 1 + form * 31 / 127;
        f = h > f ? h - f : f - h;
        a = f == 0 ? a * 4 : f == 1 ? a * 5 / 2 : f == 2 ? a * 3 / 2 : a;
    }
    return a;
}

static void build(int into, int b0, int b1, int odd, int form)
{
    int fr, n, h, amps[33];
    for (fr = 0; fr < SPEC_FRAMES; fr++) {
        int bright = b0 + (b1 - b0) * fr / (SPEC_FRAMES - 1), peak = 1;
        int acc[SPEC_LEN];
        for (h = 1; h <= 32; h++)
            amps[h] = digicmi_spec_amp(h, bright, odd, form);
        for (n = 0; n < SPEC_LEN; n++) {
            int s = 0;
            for (h = 1; h <= 32; h++)
                if (amps[h])
                    s += amps[h] * spec_sin[(h * n) & (SPEC_LEN - 1)] >> 15;
            acc[n] = s;
            if (s > peak)
                peak = s;
            else if (-s > peak)
                peak = -s;
        }
        for (n = 0; n < SPEC_LEN; n++)
            tab[into][fr][n] = (short)(acc[n] * 29000 / peak);
        tab[into][fr][SPEC_LEN] = tab[into][fr][0];
    }
}

/* UI tick: track 1's spectral settings, the tables rebuilt when they change. */
void digicmi_spec_tick(void)
{
    uint8_t *kit = fw_kit, *snd;
    int b0, b1, odd, form, key;
    if (!kit)
        return;
    snd = FW_SOUND(kit, 0);
    if (snd[FW_SOUND_MACHINE] != 40)
        return;
#define W(k) (*(int16_t *)(snd + 0x14 + 2 * (k)))
    b0 = (W(21) >> 8) & 127;
    b1 = (W(22) >> 8) & 127;
    odd = (W(49) >> 8) & 127;
    form = (W(46) >> 8) & 127;
#undef W
    b0 = b0 > 120 ? 120 : b0;
    b1 = b1 > 120 ? 120 : b1;
    key = b0 | b1 << 7 | odd << 14 | form << 21;
    if (key == built_key && bank >= 0)
        return;
    build(bank == 0 ? 1 : 0, b0, b1, odd, form);
    bank = bank == 0 ? 1 : 0;
    built_key = key;
}

/* ---- the voices ---- */
static u32 phase[8];
static int fpos[8], fdir[8], last_pos[8], rate[8];

/* Voice v's place in the morph, 0-65535 across the frames, for the page's arrows. */
int digicmi_spec_pos(int v)
{
    return (unsigned)v < 8 ? (int)((unsigned)fpos[v] / (SPEC_FRAMES - 1)) : 0;
}

/* Render-task: voice v's block from the tables. play_pos is the stock player's playhead (its move per block is
 * the pitch), mode PLAY's 0-4, time G's 0-120, gain the voice's (V + 0x10), start a note's first block. */
void spec_voice(int v, int32_t *b, int play_pos, int mode, int time, int gain, int start)
{
    const short (*t)[SPEC_LEN + 1];
    int adv, i, f0, f1, ff, blocks, whole, scale;
    u32 inc;
    if ((unsigned)v > 7)
        return;
    if (bank < 0 || gain <= 0) {
        for (i = 0; i < 32; i++)
            b[i] = 0;
        return;
    }
    t = (const short (*)[SPEC_LEN + 1])tab[bank];
    adv = play_pos - last_pos[v];
    adv = adv < 0 ? -adv : adv;
    last_pos[v] = play_pos;
    if (start) {
        phase[v] = 0;
        rate[v] = 1 << 16;
        fdir[v] = mode == 0 || mode == 1 ? -1 : 1;
        fpos[v] = fdir[v] < 0 ? LAST_FRAME : 0;
    } else if (adv > 0 && adv < 4096)
        rate[v] = adv << 11;                        /* adv / 32 output samples, 16.16 */
    inc = ROOT_INC * (u32)(rate[v] >> 8);

    /* the morph, once a block: a whole sweep takes 75 x 2^(G/15) blocks */
    time = time < 0 ? 0 : time > 120 ? 120 : time;
    whole = time / 15;
    blocks = (int)(((u32)(75 << whole) * 32768u) / spec_exp2n[(time % 15) * 16 / 15]);
    if (blocks < 1)
        blocks = 1;
    fpos[v] += fdir[v] * (LAST_FRAME / blocks + 1);
    if (fpos[v] > LAST_FRAME) {
        if (mode == 2)                              /* FWD.L: round again */
            fpos[v] = 0;
        else if (mode == 4) {                       /* P: back */
            fpos[v] = LAST_FRAME;
            fdir[v] = -1;
        } else
            fpos[v] = LAST_FRAME;                   /* FWD: stay at the end */
    } else if (fpos[v] < 0) {
        if (mode == 1)                              /* REV.L */
            fpos[v] = LAST_FRAME;
        else if (mode == 4) {
            fpos[v] = 0;
            fdir[v] = 1;
        } else
            fpos[v] = 0;
    }
    f0 = fpos[v] >> 16;
    f1 = f0 < SPEC_FRAMES - 1 ? f0 + 1 : f0;
    ff = (fpos[v] & 0xffff) >> 1;                   /* 15 bits */
    scale = (int)((((u32)gain >> 15) * 207u) >> 8); /* gain x 0.81, as a sample's block */

    for (i = 0; i < 32; i++) {
        u32 p = phase[v];
        int idx = (int)(p >> 24), fr = (int)((p >> 9) & 0x7fff);
        int a = t[f0][idx] + ((t[f0][idx + 1] - t[f0][idx]) * fr >> 15);
        int c = t[f1][idx] + ((t[f1][idx + 1] - t[f1][idx]) * fr >> 15);
        int s = a + ((c - a) * ff >> 15);
        b[i] = s * scale;
        phase[v] = p + inc;
    }
}
