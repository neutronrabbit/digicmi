/* digicmi: CMI-style loop controls for the CMI machine's voices (its own track's and borrowed ones).
 *
 * The stock ONESHOT player (measured in digiemu, OS 1.54; the render's code and data are the same in 1.53):
 * every block the render asks ONESHOT's window function (0x40074c9c) for the voice's window, then reads its
 * PLAY (0 REV, 1 REV.L, 2 FWD.L, 3 FWD) at one place per player (cmi_loop.s). The window comes back in d0:d1
 * in samples, in the order the direction needs (forward: d0 the start, d1 the end; reverse: the other way).
 * On a note's first block it is [STRT, END] and the playhead (V(v) + 4) starts at d0; after that a looping
 * voice gets [LOOP, END], and a forward voice that runs past d1 jumps back by the window's length (the loop).
 * A later stage reads PLAY again for its loop flag alone and ends a voice that does not loop at its end.
 *
 * For a CMI voice whose PLAY is FWD.L, this replaces the window and the PLAY those places read:
 *   LLEN  the loop's end: LOOP + LLEN/128 of the sample (0: the sample's END, as stock)
 *   LFIN  the loop's start moves on by LFIN/128 of one LOOP step
 *   LMOD  HOLD loops for as long as the voice sounds; SUS loops while the note is held, then plays on to
 *         END and stops (held: the AMP envelope's phase is not 0; it turns 0 at a live note's note-off and
 *         at the end of a sequencer trig's length, both measured)
 *   DIR   FWD, or ALT: forward, then backward, and so on (the turn moves the playhead on by TURN_K samples,
 *         the resampler's delay, so the waveform carries on where it left off: digislicer's measured value
 *         for the same player)
 * The values are read every block from the voice's own copy of the parameters (after locks, smoothing
 * and LFOs), so turning LOOP or LLEN moves the loop of notes that are playing, as on the CMI. The playhead
 * moves through the sample at the note's pitch, so a higher note loops faster.
 * Any other PLAY, and every other machine, plays exactly as stock. */
#include "digitakt-mk1/core3.h"

typedef unsigned int u32;

#define CMI_ID     40
#define V(v)       (0x8000EDC4u + 94u * (u32)(v))           /* a voice's player state */
#define POS(v)     (*(volatile int *)(V(v) + 4))             /* its playhead, in samples */
#define SLEN(v)    (*(volatile u32 *)(V(v) + 0x14))          /* its sample's length, in samples */
#define LOADED(v)  (((volatile u32 *)(0x800014f0 + 0x131 * 4))[v])   /* the sound the voice has (poly.c) */
#define PARAM(v, slot) (fw_voice_params[53 * (v) + (slot) - 17])    /* slot 17 is A */

#define P_FWDL     2
#define P_REVL     1
#define P_FWD      3
#define TURN_K     14
#define MIN_LOOP   320                                       /* samples: the player keeps 141 at each end */
#define FULL       0x7800                                    /* 120.0: STRT, LEN and LOOP's scale */

static signed char dir[8];
static unsigned char released[8], have[8], eff[8];
static int last[8];

static int clamp(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static u32 scale(u32 val, u32 len)                           /* val / 120.0 of len, without 64-bit sums */
{
    return (len / FULL) * val + ((len % FULL) * val) / FULL;
}

static int is_cmi(int v)
{
    u32 s = LOADED(v);
    return core_track_machine[v] == CMI_ID || (s >= 0x40000000u && *(volatile unsigned char *)(s + 0x7e) == CMI_ID);
}

/* Both players, right after the window (cmi_loop.s): the PLAY to read, and the window in win[0..1]. */
int digicmi_play(int v, int play, int *win)
{
    if ((unsigned)v > 7)
        return play;
    eff[v] = 0;
    if (play != P_FWDL || !is_cmi(v))
        return play;

    u32 len = SLEN(v);
    int strt = clamp(PARAM(v, 21), 0, FULL);
    int endv = clamp(PARAM(v, 21) + PARAM(v, 22), 0, FULL);
    int g = PARAM(v, 23);
    int loopv = g ? clamp(g - 1, 0, FULL) : strt;
    int llen = (PARAM(v, 46) >> 8) & 127, sus = ((PARAM(v, 47) >> 8) & 127) >= 64;
    int alt = ((PARAM(v, 48) >> 8) & 127) >= 64, lfin = (PARAM(v, 49) >> 8) & 127;
    loopv = clamp(loopv + 2 * lfin, 0, endv);                /* one LOOP step is 256: LFIN's step is 2 */

    int s0 = (int)scale((u32)strt, len), se = (int)scale((u32)endv, len);
    int ls = (int)scale((u32)loopv, len);
    int le = llen ? ls + (int)((len / 128) * (u32)llen + ((len % 128) * (u32)llen) / 128) : se;
    if (le > se)
        le = se;
    if (le - ls < MIN_LOOP) {                                /* too short for the player: as short as it takes */
        le = ls + MIN_LOOP;
        if (le > se) {
            le = se;
            ls = se - MIN_LOOP > 0 ? se - MIN_LOOP : 0;
        }
    }

    int start = (int)((fw_voice_start >> v) & 1), pos = POS(v);
    if (start) {
        released[v] = 0;
        dir[v] = 1;
        have[v] = 0;
    } else if (fw_amp_env[3 * v] == 0)
        released[v] = 1;

    if (sus && released[v] && !start) {                      /* SUS, let go: on to the end, and stop there */
        win[0] = ls;
        win[1] = se;
        eff[v] = P_FWD;
        return P_FWD;
    }
    if (start || !alt) {
        win[0] = start ? s0 : ls;
        win[1] = le;
        eff[v] = P_FWDL;
        if (start)
            last[v] = s0;
        return P_FWDL;
    }

    /* ALT: turn near an end. The playhead is the render's from the block after the note's first. */
    if (have[v] >= 1) {
        int adv = pos - last[v];
        if (adv < 0)
            adv = -adv;
        if (adv < 16 || adv > 4096)                          /* none, or a jump (a wrap): a block's worth */
            adv = 32;
        if (dir[v] > 0 && pos >= ls && pos + adv + TURN_K >= le) {
            dir[v] = -1;
            POS(v) = pos += TURN_K;
        } else if (dir[v] < 0 && pos - adv - TURN_K <= ls) {
            dir[v] = 1;
            POS(v) = pos -= TURN_K;
        }
    } else
        have[v]++;
    last[v] = pos;
    if (dir[v] < 0) {
        win[0] = le;
        win[1] = ls;
        eff[v] = P_REVL;
        return P_REVL;
    }
    win[0] = ls;
    win[1] = le;
    eff[v] = P_FWDL;
    return P_FWDL;
}

/* The later stage's PLAY word (its loop flag), for voice v: ours for a CMI voice, else the stock word. */
int digicmi_stage(int v, int word)
{
    if ((unsigned)v > 7 || !eff[v])
        return word;
    return (word & 0xff) | (eff[v] << 8);
}

/* ---- the voices' sound, after playback, before the AMP envelope (machine-pages' ev_render_voices) ----
 * fw_track_blocks[v] (32 Q31 samples, voice v = track v) is the voice's sample scaled by its gain (V(v) +
 * 0x10, velocity^2 x volume): measured in digiemu, the block's peak stays 0.81 x gain x the sample's peak
 * while the AMP envelope falls, so the envelope comes later and the block is still at the sample's level.
 * One 16-bit LSB of the sample is gain x 0.81 / 2^15 there, so 8BIT keeps the sample's top 8 bits by
 * rounding the block down to a grid of gain x 0.81 / 2^7 (207/2^15 ~ 0.81/128, without 64-bit sums).
 * ENG = SPEC silences the voice: the spectral engine isn't built yet. */
#define GAIN(v)  (*(volatile int *)(V(v) + 0x10))

void digicmi_render(int32_t *blocks)
{
    int v, i;
    for (v = 0; v < 8; v++) {
        int32_t *b = blocks + 32 * v;
        if (!is_cmi(v))
            continue;
        if (((PARAM(v, 51) >> 8) & 127) >= 64) {             /* ENG = SPEC */
            for (i = 0; i < 32; i++)
                b[i] = 0;
            continue;
        }
        if (((PARAM(v, 50) >> 8) & 127) >= 64) {             /* 8BIT */
            int g = GAIN(v), step;
            if (g <= 0)
                continue;
            step = (int)((((u32)g >> 8) * 207u) >> 7);
            if (step <= 0)
                continue;
            for (i = 0; i < 32; i++)
                b[i] -= b[i] % step;                         /* towards zero, as an 8-bit sample truncates */
        }
    }
}
