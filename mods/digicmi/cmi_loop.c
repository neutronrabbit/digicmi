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
 *   (LLEN, 0.3-0.12: gone in 0.13; the loop ends at the sample's END, STRT + LEN, as stock)
 *   (LFIN, 0.3-0.19: gone in 0.20; slot 49 is the time synth's TIME)
 *   LMOD  HOLD loops for as long as the voice sounds; SUS loops while the note is held, then plays the
 *         sample's tail: on past END to the sample's own end (REV.L: past STRT down to its start) and stops.
 *         With no tail there (END at the sample's end; REV.L: LOOP at its start) it keeps looping and the AMP
 *         envelope's release fades it, as HOLD. (0.13-0.23 played on to END only: with the loop ending at END
 *         since 0.13, a released SUS note stopped within one pass, whatever the release.) (held: the AMP envelope's phase is not 0; it turns 0 at a live note's note-off and
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
#define P_REV      0
#define P_PP       4                                         /* CMI's own: ping-pong */
#define TURN_K     14
#define TAIL_MIN   64                                        /* samples: less than this is no tail to play */
#define MIN_LOOP   320                                       /* samples: the player keeps 141 at each end */
#define FULL       0x7800                                    /* 120.0: STRT, LEN and LOOP's scale */

#define PCM(v)     (*(const short *volatile *)V(v))          /* the playing sample's PCM (V + 0, measured) */
#define ZC_REACH   256                                       /* samples either side searched for a zero crossing */

/* The loop's points on zero crossings (0.23): the nearest upward crossing (one sample < 0, the next >= 0) to
 * where LOOP and END put them, within ZC_REACH, so both ends of the loop meet the wave going the same way and
 * the join doesn't click. Kept per voice and searched again only when a point, the sample or its length move
 * (LOOP under an LFO searches each block it moves: ZC_REACH x 2 reads at most). None in reach: as it was. */
static const short *zc_pcm[8];
static int zc_in[8][2], zc_out[8][2];

static int zc_near(const short *pcm, int len, int pos, int lo, int hi)
{
    int d, i;
    lo = lo < 1 ? 1 : lo;
    hi = hi > len - 1 ? len - 1 : hi;
    for (d = 0; d <= ZC_REACH; d++) {
        i = pos + d;
        if (i >= lo && i <= hi && pcm[i - 1] < 0 && pcm[i] >= 0)
            return i;
        i = pos - d;
        if (d && i >= lo && i <= hi && pcm[i - 1] < 0 && pcm[i] >= 0)
            return i;
    }
    return pos;
}

static void zc_snap(int v, int *ls, int *le)
{
    const short *pcm = PCM(v);                               /* the sample the player is playing (0.24: was found
                                                                through SAMP, which lags a voice's first blocks) */
    int len = (int)SLEN(v);
    if ((u32)pcm < 0x40000000u || len < 1024)
        return;
    if (pcm != zc_pcm[v] || *ls != zc_in[v][0] || *le != zc_in[v][1]) {
        zc_pcm[v] = pcm;
        zc_in[v][0] = *ls;
        zc_in[v][1] = *le;
        zc_out[v][1] = zc_near(pcm, len, *le, *ls + MIN_LOOP, len - 1);
        zc_out[v][0] = zc_near(pcm, len, *ls, 1, zc_out[v][1] - MIN_LOOP);
    }
    *ls = zc_out[v][0];
    *le = zc_out[v][1];
}

/* (0.26-0.27 switched a released voice off once "the AMP envelope" had faded. Withdrawn in 0.28: the value
 * read, fw_amp_env's level, follows the FILTER envelope's settings (kit slots 29 attack, 30 decay, 31 sustain,
 * 32 release), not the audio's level: measured in digiemu, the output stays constant through a held note while
 * that level falls to -135 dB, and fades after release while it doesn't move. So it cut voices that were still
 * sounding. The phase beside it (2 attack, 1 held, 0 released) still tells the gate.) */
/* Voice off when silent (0.30), by the real AMP envelope: the render's amp stage (0x400716c0) keeps, per voice,
 * a phase (2 attack, 1 hold, 0 decay) and a value (in decay: the level, negated Q31, falling to 0) at AMPENV;
 * measured against the output, the value is the sound's level. A CMI voice that is on, in decay and below
 * AMP_SILENT for AMP_BLOCKS blocks running is ended: the gain stage is told "not looping, end reached" and
 * quarters the gain each block (digicmi_stage), and once the gain is 0 the voice is switched off (V + 0x28 = 0,
 * as after boot). Without this a looping note keeps its voice on and streaming for good after it has faded; on
 * the unit released notes piled up on all eight voices and the audio load rose (86% -> 94%). The "end reached"
 * flag set here is cleared when the voice's next note starts. */
#ifdef OS154
#define AMPENV     0x4199ed64u
#else
#define AMPENV     0x4199dd64u                               /* by the 0x1000 shift of this region: unverified */
#endif
#define AMP_PHASE(v)  (*(volatile int *)(AMPENV + 8u * (u32)(v)))
#define AMP_VALUE(v)  (*(volatile int *)(AMPENV + 8u * (u32)(v) + 4))
#define AMP_SILENT  0x10000                                  /* Q31 magnitude: -90 dB */
#define AMP_BLOCKS  64
static unsigned char fade_end[8], fade_count[8], fade_flagged[8];

static signed char dir[8];
static unsigned char released[8], have[8], eff[8];
static int last[8];

static int clamp(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* LOOP END (0.31, page 2 H, slot 52): 0 = END (the loop runs to STRT + LEN, as 0.13-0.30), else 1-127 = a place
 * in the sample, value / 127 of it on STRT/LEN/LOOP's 0-120 scale, kept between LOOP and END. A value with a low
 * byte (Digi Poly's marker in kits from before 0.20) counts as 0. Returns that 0-120 value, or -1 for END. */
static int lend_val(int v)
{
    int w = PARAM(v, 52), k;
    if (w & 0xff)
        return -1;
    k = (w >> 8) & 127;
    return k ? k * FULL / 127 : -1;
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
    if (is_cmi(v) && ((PARAM(v, 51) >> 8) & 127) >= 96) {   /* ENG = TIME: the player loops the whole sample,
                                                                silently, as the synth's pitch (timesynth.c) */
        win[0] = 0;
        win[1] = (int)SLEN(v);
        eff[v] = P_FWDL;
        return P_FWDL;
    }
    if ((play != P_FWDL && play != P_REVL && play != P_PP) || !is_cmi(v))
        return play;                                         /* REV, FWD and other machines: stock */

    u32 len = SLEN(v);
    int strt = clamp(PARAM(v, 21), 0, FULL);
    int endv = clamp(PARAM(v, 21) + PARAM(v, 22), 0, FULL);
    int g = PARAM(v, 23);
    int loopv = g ? clamp(g - 1, 0, FULL) : strt;
    int llen = 0, sus = ((PARAM(v, 47) >> 8) & 127) >= 64;     /* 0.13: no LLEN; the loop ends at END */
    int alt = play == P_PP, rev = play == P_REVL;
    loopv = clamp(loopv, 0, endv);

    int s0 = (int)scale((u32)strt, len), se = (int)scale((u32)endv, len);
    int ls = (int)scale((u32)loopv, len);
    int lendv = lend_val(v);                                 /* 0.31: LOOP END, or END */
    int le = lendv >= 0 ? (int)scale((u32)clamp(lendv, loopv, endv), len) : se;
    (void)llen;
    if (le > se)
        le = se;
    if (le - ls >= MIN_LOOP)
        zc_snap(v, &ls, &le);                                /* 0.23: both on zero crossings */
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

    if (rev) {                                               /* REV.L: the same loop, backwards from END */
        if (sus && released[v] && !start && s0 >= TAIL_MIN) {    /* SUS, let go: down past STRT to the
                                                                    sample's start, and stop */
            win[0] = le;
            win[1] = 0;
            eff[v] = P_REV;
            return P_REV;
        }
        win[0] = start ? se : le;
        win[1] = ls;
        eff[v] = P_REVL;
        return P_REVL;
    }
    if (sus && released[v] && !start && lendv >= 0 && se - le >= TAIL_MIN) {   /* SUS with a LOOP END, let
                                                                    go: on from the loop to END, and stop */
        win[0] = ls;
        win[1] = se;
        eff[v] = P_FWD;
        return P_FWD;
    }
    if (sus && released[v] && !start && lendv < 0 && (int)len - se >= TAIL_MIN) {   /* SUS, loop to END, let
                                                                    go: on past END to the sample's end */
        win[0] = ls;
        win[1] = (int)len;
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
    if ((unsigned)v < 8 && ((fw_voice_start >> v) & 1)) {    /* a new note: not faded, and no stale end flag */
        fade_end[v] = 0;
        fade_count[v] = 0;
        if (fade_flagged[v]) {
            *(volatile unsigned char *)(V(v) + 0x29) = 0;
            fade_flagged[v] = 0;
        }
    }
    if ((unsigned)v < 8 && fade_end[v]) {                    /* silent: end it, as at a sample's end */
        *(volatile unsigned char *)(V(v) + 0x29) = 1;
        fade_flagged[v] = 1;
        return (word & 0xff) | (P_FWD << 8);
    }
    if ((unsigned)v > 7 || !eff[v])
        return word;
    return (word & 0xff) | (eff[v] << 8);
}

/* ---- the voices' sound, after playback, before the AMP envelope (machine-pages' ev_render_voices) ----
 * fw_track_blocks[v] (32 Q31 samples, voice v = track v) is the voice's sample scaled by its gain (V(v) +
 * 0x10, velocity^2 x volume): measured in digiemu, the block's peak stays 0.81 x gain x the sample's peak
 * while the AMP envelope falls, so the envelope comes later and the block is still at the sample's level.
 * One 16-bit LSB of the sample is gain x 0.81 / 2^15 there, so RETRO keeps the sample's top 8 bits by
 * rounding the block down to a grid of gain x 0.81 / 2^7 (207/2^15 ~ 0.81/128, without 64-bit sums).
 * ENG = TIME replaces the block with the time synth's (timesynth.c); RETRO then acts on it as on a sample. */
#define GAIN(v)  (*(volatile int *)(V(v) + 0x10))
void ts_voice(int v, int32_t *b, int play_pos, int mode, int time, int blend, int loop_frame, int loop_end,
              int gain, int start);

/* The time synth's loop: LOOP's (G 0: STRT) and LOOP END's places between STRT and END as wave positions, 16.16
 * of 0-15 (LOOP END 0: the last wave) */
static int ts_frame_of(int v, int pos)
{
    int strt = clamp(PARAM(v, 21), 0, FULL), endv = clamp(PARAM(v, 21) + PARAM(v, 22), 0, FULL);
    if (endv <= strt)
        return 0;
    return ((clamp(pos, strt, endv) - strt) * 15 * 256 / (endv - strt)) << 8;
}

static int ts_loop_frame(int v)
{
    int strt = clamp(PARAM(v, 21), 0, FULL), g = PARAM(v, 23);
    return ts_frame_of(v, g ? g - 1 : strt);
}

static int ts_loop_end(int v)
{
    int lendv = lend_val(v);
    return lendv < 0 ? 15 << 16 : ts_frame_of(v, lendv);
}

/* RETRO's 24 kHz: a CMI Series I voice played its 24 kHz sample with a clock of 24 kHz x the note's pitch ratio
 * (it pitched by clock, not by resampling). Here each output value is held until the voice's playhead has moved
 * two samples of the 48 kHz sample, one 24 kHz sample: at the root a value lasts two output samples, higher
 * notes less, and from an octave up none. The ratio is the playhead's own move this block (TUNE, bends and LFOs
 * in), in 16.16 per output sample. */
/* RETRO (0.23, a dial: slot 50, 0-127 in six steps): 0 off (16 bit 48 kHz), 1 16 bit 44.1 kHz, 2 8 bit 32 kHz,
 * 3 8 bit 24 kHz (0.14-0.22's RETRO), 4 8 bit 16 kHz, 5 8 bit 8 kHz. A step is 48 kHz / rate samples of the
 * sample, 16.16. (A kit saved with 0.14-0.22's switch on holds 127: the dial's last step, 8 kHz.) */
#define RETRO_MODE(v) ((((PARAM(v, 50) >> 8) & 127) * 6) >> 7)
static const u32 hold_steps[6] = {0x10000, 71332, 98304, 131072, 196608, 393216};
static u32 HOLD_STEP;
static int32_t hold_val[8];
static u32 hold_phase[8], hold_inc[8];
static int hold_last[8];

static void retro_rate(int v, int32_t *b, int mode)
{
    HOLD_STEP = hold_steps[mode];
    int pos = POS(v), adv = pos - hold_last[v], i;
    if (adv < 0)
        adv = -adv;
    hold_last[v] = pos;
    if ((fw_voice_start >> v) & 1) {                         /* a new note: its first value at once */
        hold_phase[v] = HOLD_STEP;
        hold_inc[v] = 0x10000;
    } else if (adv > 0 && adv < 4096)                        /* not a loop's wrap or a jump */
        hold_inc[v] = (u32)adv << 11;                        /* adv / 32 output samples, 16.16 */
    for (i = 0; i < 32; i++) {
        hold_phase[v] += hold_inc[v];
        if (hold_phase[v] >= HOLD_STEP) {
            hold_phase[v] %= HOLD_STEP;
            hold_val[v] = b[i];
        }
        b[i] = hold_val[v];
    }
}

static signed char newest = -1;                              /* the voice of the page's one playhead: the latest note */

void digicmi_render(int32_t *blocks)
{
    int v, i;
    for (v = 0; v < 8; v++) {
        int32_t *b = blocks + 32 * v;
        if (((fw_voice_start >> v) & 1) && is_cmi(v))
            newest = (signed char)v;                         /* the page's one playhead follows the latest note */
        if ((fw_voice_start >> v) & 1) {
            fade_end[v] = 0;                                 /* a new note */
            fade_count[v] = 0;
        } else if (fade_end[v]) {
            if (GAIN(v) == 0)                                /* faded to nothing: the voice off, as after boot */
                *(volatile unsigned char *)(V(v) + 0x28) = 0;
        } else if (is_cmi(v) && *(volatile unsigned char *)(V(v) + 0x28) && AMP_PHASE(v) == 0
                   && AMP_VALUE(v) > -AMP_SILENT && AMP_VALUE(v) < AMP_SILENT) {
            if (++fade_count[v] >= AMP_BLOCKS)
                fade_end[v] = 1;                             /* in decay and silent for a while: end it */
        } else
            fade_count[v] = 0;
        if (!is_cmi(v)) {
            hold_last[v] = POS(v);
            continue;
        }
        if (!RETRO_MODE(v))
            hold_last[v] = POS(v);                           /* RETRO off: keep the playhead's place */
        if (((PARAM(v, 51) >> 8) & 127) >= 96)              /* ENG = TIME: the time synth (timesynth.c) */
            ts_voice(v, b, POS(v), (PARAM(v, 18) >> 8) & 7, ((PARAM(v, 49) >> 8) & 127) * 120 / 127,
                     (PARAM(v, 46) >> 8) & 127, ts_loop_frame(v), ts_loop_end(v), GAIN(v),
                     (int)((fw_voice_start >> v) & 1));
        if (RETRO_MODE(v)) {                                 /* RETRO: its rate at the note's clock; 8 bits from 2 */
            int g = GAIN(v), step, mode = RETRO_MODE(v);
            retro_rate(v, b, mode);
            if (mode < 2 || g <= 0)
                continue;
            step = (int)((((u32)g >> 8) * 207u) >> 7);
            if (step <= 0)
                continue;
            for (i = 0; i < 32; i++)
                b[i] -= b[i] % step;                         /* towards zero, as an 8-bit sample truncates */
        }
    }
}

/* The page's playhead (cmi_page.c), one arrow since 0.29: the most recently started CMI note, while it is held
 * (the gate: the envelope phase beside fw_amp_env's level is 2 in attack, 1 held, 0 released; a key down, or a
 * trig's length). So the page stops asking for frames as soon as the note is let go, however long it rings.
 * (0.11-0.28: an arrow for every voice whose "AMP envelope" was above silence, but that level is the filter
 * envelope's, see above.) */
int digicmi_voice_pos(int v, int *pos, u32 *len)
{
    if (v != newest || (unsigned)v > 7 || !*(volatile unsigned char *)(V(v) + 0x28) || !is_cmi(v)
        || fw_amp_env[3 * v] == 0)
        return 0;
    *pos = POS(v);
    *len = SLEN(v);
    return 1;
}
