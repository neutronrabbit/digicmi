/* digicmi: the time synth (ENG = TIME), after the CMI's waveform synthesis: a sound is a series of single-cycle
 * waveforms played one after another while the note sounds, so its timbre moves through them. On the CMI they
 * were drawn with the light pen; here they are cut from the track's sample.
 *
 * Tables: TS_FRAMES single cycles of TS_LEN samples (Q15, one extra sample for the wrap), cut from track 1's
 * sample at evenly spaced places between STRT and END (STRT + LEN). Each cycle is one period of the sample's
 * root: found by pitch detection (ROOT = AUTO; a YIN-style difference function over the middle of the region,
 * on the sample at half rate, then refined at full rate with a parabola) or set by ROOT (page 1 F, slot 52:
 * 0 AUTO, else MIDI note 23 + value; a value with a low byte, as kits from before 0.20 hold there, is AUTO). A region with no clear period falls back to C3 (183.47
 * samples). Each cycle starts at the first upward zero crossing after its place and is resampled to TS_LEN; a straight ramp takes out the difference between its
 * two ends so it loops without a click. (0.17-0.32 scaled all frames so the loudest peaked at 29000, which made the
 * time synth much louder than the sample; from 0.33 the waves keep the sample's own level, and SMEAR scales back to
 * the loudest unsmeared wave's peak, so it doesn't change the loudness.) All frames share one scale, so
 * quiet parts of the sample stay quieter. The tables are rebuilt in the UI tick when the sample, STRT or LEN
 * change, into the bank the render is not reading, then switched in one store.
 * Per voice (cmi_loop.c's digicmi_render calls ts_voice), from the voice's own smoothed values:
 *   A, slot 18 (PLAY 0-4) the order: 3 FWD once, 0 REV once, 2 FWD.L, 1 REV.L, 4 P, as the sample's loops:
 *                         FWD.L plays from the first wave to the last, then loops from LOOP's wave to the last;
 *                         REV.L from the last down to LOOP's, and round; P first to last, then between the
 *                         last and LOOP's, back and forth
 *   G, slot 23 (LOOP)     the loop's start: LOOP's place between STRT and END, as a wave (cmi_loop.c)
 *   D, slot 49 (0-127)    TIME of a whole pass, first wave to last: 0.05 s x 2^(T/15), T = D x 120 / 127
 *   B, slot 46 (0-127)    BLND: 0 steps from wave to wave (the CMI's segments), 127 crossfades all the way;
 *                         curved (cube), as the ear only hears the joins while a crossfade is a few ms long
 * Per track (track 1's sound, in the tables):
 *   C, slot 47 (0-127)    SMEAR (LMOD in sample mode): each wave mixed with its neighbours, a triangle up to
 *                         15 waves either side wide, for a washed tone; 0 off
 * The root plays at its own pitch on MIDI 60, as the sample does in sample mode. The voice's pitch is its stock
 * player's: while ENG = TIME the player loops the track's sample silently
 * (cmi_loop.c) and its playhead's move per block is the pitch ratio, TUNE, bends and LFOs in. The block it
 * writes is scaled as the sample's would be (gain x 0.81), so the AMP envelope, filter, pan, RETRO and SPREAD
 * act on it as on a sample. */
#include "digitakt-mk1/core3.h"

typedef unsigned int u32;

#define TS_FRAMES  16
#define TS_LEN     256
#define LAST_FRAME ((TS_FRAMES - 1) << 16)
#define CYCLE      12023779u                        /* C3: 48000 / 261.6256 samples, 16.16 */
#define FULL       0x7800                           /* 120.0: STRT and LEN's scale */
#ifdef OS154
#define SMP_TAB    0x4031a3a0u
#else
#define SMP_TAB    0x403193a0u
#endif

static short tab[2][TS_FRAMES][TS_LEN + 1];
static short raw[TS_FRAMES][TS_LEN + 1];           /* the cut waves, before SMEAR */
static volatile u32 cut_s0, cut_per = 1;           /* 0.37, TIME OFF: wave 0's sample and samples per wave */
static int raw_peak = 1;                            /* their loudest point (0.33: SMEAR scales back to it) */
static volatile int bank = -1;                      /* the bank the render reads; -1 none built yet */
static const short *built_pcm;
static u32 built_len;
static int built_strt = -1, built_lenv = -1, built_smear = -1, built_root = -1;
static u32 cycle_fix = CYCLE;                       /* the root's period, 16.16 samples */
static volatile u32 root_inc = 91443u;              /* the phase step for the root at MIDI 60: 2^32 / period / 256 */
static int root_note = 60, root_auto = 1, root_found;   /* for the page: the note in use, AUTO?, detected? */

static const unsigned short exp2n[17] = {           /* 2^(-i/16), Q15 */
    32768, 31379, 30048, 28774, 27554, 26386, 25268, 24196, 23170, 22188, 21247, 20347,
    19484, 18658, 17867, 17109, 16384
};

static u32 scale(u32 val, u32 len)                  /* val / 120.0 of len, without 64-bit sums */
{
    return (len / FULL) * val + ((len % FULL) * val) / FULL;
}

/* The sample at st + n / TS_LEN of a cycle, linearly interpolated. */
static int at(const short *pcm, u32 st, u32 n)
{
    u32 off = n * (cycle_fix / TS_LEN);             /* 16.16 */
    u32 i = st + (off >> 16);
    int a = pcm[i], b = pcm[i + 1];
    return a + ((b - a) * (int)((off & 0xffff) >> 1) >> 15);
}

/* ---- the root ---- */
static const unsigned semis[12] = {                 /* 2^(k/12), Q16 */
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715
};

static u32 cycle_of_note(int n)                     /* MIDI note n's period at 48 kHz, 16.16 */
{
    int d = 60 - n, oct = d >= 0 ? d / 12 : -((11 - d) / 12), k = d - 12 * oct;
    u32 c = ((CYCLE >> 8) * (semis[k] >> 4)) >> 4;  /* no overflow: 46967 x 7732 */
    return oct >= 0 ? c << oct : c >> -oct;
}

static int note_of_cycle(u32 c)                     /* the nearest MIDI note (24-108) to a period */
{
    int n, best = 60;
    u32 bd = 0xffffffffu, a = c >> 8;
    for (n = 24; n <= 108; n++) {
        u32 b = cycle_of_note(n) >> 8, r = a > b ? a * 1000u / b : b * 1000u / a;
        if (r < bd) {
            bd = r;
            best = n;
        }
    }
    return best;
}

#define DW    256                                   /* the detector's window, half-rate samples */
#define DMAX  512                                   /* its longest lag (half rate): 46.9 Hz */
#define DMIN  20                                    /* its shortest: 1200 Hz */
static short dx[DW + DMAX + 1];
static u32 dd[DMAX + 1];

static u32 diff_full(const short *pcm, u32 a, u32 t)   /* the difference function at full rate, lag t */
{
    u32 i, sum = 0;
    for (i = 0; i < 2 * DW; i++) {
        int e = (pcm[a + i] >> 6) - (pcm[a + i + t] >> 6);
        sum += (u32)(e * e);
    }
    return sum;
}

/* The period of the sample between s0 and se, 16.16 samples, or 0 when it has no clear one. */
static u32 detect(const short *pcm, u32 len, u32 s0, u32 se)
{
    u32 avail = se > s0 ? se - s0 : 0, lmax, a, k, t, sum = 0, best = 0, bestv = 0xffffffffu, energy = 0;
    u32 dm, d0, dp, t2;
    int off;
    if (avail < 2 * (DW + 2 * DMIN) + 8)
        return 0;
    lmax = (avail - 8) / 2 - DW;
    lmax = lmax > DMAX ? DMAX : lmax;
    a = s0 + avail / 2 - (DW + lmax);               /* the window and its lags, centred in the region */
    if (a + 2 * (DW + lmax) + 8 > len)
        return 0;
    for (k = 0; k <= DW + lmax; k++) {
        dx[k] = (short)((pcm[a + 2 * k] + pcm[a + 2 * k + 1]) >> 7);
        if (k < DW)
            energy += (u32)(dx[k] * dx[k]);
    }
    if (energy < DW * 4)                            /* silence */
        return 0;
    for (t = 1; t <= lmax; t++) {                   /* the cumulative-mean-normalised difference, Q8 */
        u32 d = 0, dsh;
        for (k = 0; k < DW; k++) {
            int e = dx[k] - dx[k + t];
            d += (u32)(e * e);
        }
        dsh = d >> 8;
        sum += dsh;
        dd[t] = dsh * t / ((sum >> 8) + 1);
    }
    for (t = DMIN; t <= lmax; t++) {                /* the first dip below 0.15, to its bottom */
        if (dd[t] < 38) {
            while (t < lmax && dd[t + 1] < dd[t])
                t++;
            best = t;
            break;
        }
        if (dd[t] < bestv) {
            bestv = dd[t];
            best = t;
        }
    }
    if (!best || (dd[best] >= 38 && bestv > 128))   /* nothing below 0.5: no clear period */
        return 0;
    /* refined at full rate: the lowest of 2 best - 2 .. 2 best + 2, then a parabola through it */
    t2 = 2 * best;
    if (t2 < 4 || a + 4 * DW + t2 + 4 > len)
        return t2 << 16;
    bestv = 0xffffffffu;
    for (t = t2 - 2; t <= t2 + 2; t++) {
        u32 d = diff_full(pcm, a, t);
        if (d < bestv) {
            bestv = d;
            best = t;
        }
    }
    dm = diff_full(pcm, a, best - 1) >> 8;
    d0 = bestv >> 8;
    dp = diff_full(pcm, a, best + 1) >> 8;
    off = 0;
    if (dm + dp > 2 * d0)
        off = (int)(((int)dm - (int)dp) * 128) / (int)(dm + dp - 2 * d0);   /* Q8, +-0.5 */
    off = off > 128 ? 128 : off < -128 ? -128 : off;
    return (best << 16) + (u32)(off * 256);
}

static void cut(const short *pcm, u32 len, int strt, int lenv, int root)
{
    u32 st[TS_FRAMES], s0, se, span, need;
    int f, n, peak = 1, drift[TS_FRAMES];
    raw_peak = 1;
    cycle_fix = CYCLE;
    root_found = 0;
    if (pcm && len > 1024) {
        s0 = scale((u32)strt, len);
        se = scale((u32)(strt + lenv > FULL ? FULL : strt + lenv), len);
        if (root) {                                 /* ROOT set: that note's period */
            cycle_fix = cycle_of_note(root);
            root_found = 1;
        } else if ((cycle_fix = detect(pcm, len, s0, se < len ? se : len)) != 0)
            root_found = 1;
        else
            cycle_fix = CYCLE;                      /* no clear period: C3 */
    }
    root_note = note_of_cycle(cycle_fix);
    root_auto = !root;
    need = (cycle_fix >> 16) + 3;
    if (!pcm || len < need + 2) {                   /* no sample: silence */
        for (f = 0; f < TS_FRAMES; f++)
            for (n = 0; n <= TS_LEN; n++)
                raw[f][n] = 0;
        return;
    }
    s0 = scale((u32)strt, len);
    se = scale((u32)(strt + lenv > FULL ? FULL : strt + lenv), len);
    if (s0 > len - need)
        s0 = len - need;
    if (se > len)
        se = len;
    span = se > s0 + need ? se - s0 - need : 0;
    cut_s0 = s0;
    cut_per = span / (TS_FRAMES - 1) ? span / (TS_FRAMES - 1) : 1;
    for (f = 0; f < TS_FRAMES; f++) {
        u32 c = s0 + span / (TS_FRAMES - 1) * (u32)f + (span % (TS_FRAMES - 1)) * (u32)f / (TS_FRAMES - 1), i;
        u32 lim = c + (cycle_fix >> 16);
        st[f] = c;
        if (lim > len - need)
            lim = len - need;
        for (i = c + 1; i <= lim; i++)              /* the first upward zero crossing */
            if (pcm[i - 1] < 0 && pcm[i] >= 0) {
                st[f] = i;
                break;
            }
        drift[f] = at(pcm, st[f], TS_LEN) - at(pcm, st[f], 0);
        for (n = 0; n < TS_LEN; n++) {
            int x = at(pcm, st[f], (u32)n) - drift[f] * n / TS_LEN;
            x = x < 0 ? -x : x;
            if (x > peak)
                peak = x;
        }
    }
    raw_peak = peak > 32767 ? 32767 : peak;
    for (f = 0; f < TS_FRAMES; f++) {
        for (n = 0; n < TS_LEN; n++)
        {
            int x = at(pcm, st[f], (u32)n) - drift[f] * n / TS_LEN;   /* the sample's own level (0.33) */
            raw[f][n] = (short)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        raw[f][TS_LEN] = raw[f][0];
    }
}

/* The tables the voices read: raw's waves, SMEAR'd. Wave f is the weighted sum of waves f - r .. f + r (weights
 * a triangle, r = smear x 15 / 127 in 8.8), scaled back so the loudest wave peaks where the unsmeared ones did. */
static void smear_into(int into, int smear)
{
    int f, g, n, peak = 1, r = smear * 15 * 256 / 127;
    static int acc[TS_FRAMES][TS_LEN];
    for (f = 0; f < TS_FRAMES; f++) {
        int wsum = 0;
        for (n = 0; n < TS_LEN; n++)
            acc[f][n] = 0;
        for (g = 0; g < TS_FRAMES; g++) {
            int d = (g > f ? g - f : f - g) << 8, w = r + 256 - d;
            if (w <= 0)
                continue;
            wsum += w;
            for (n = 0; n < TS_LEN; n++)
                acc[f][n] += raw[g][n] * w >> 4;
        }
        for (n = 0; n < TS_LEN; n++) {
            int a = acc[f][n], x;
            x = acc[f][n] = (a / wsum << 4) + (a % wsum << 4) / wsum;   /* no 64-bit sums */
            x = x < 0 ? -x : x;
            if (x > peak)
                peak = x;
        }
    }
    for (f = 0; f < TS_FRAMES; f++) {
        for (n = 0; n < TS_LEN; n++)
            tab[into][f][n] = (short)(acc[f][n] / peak * raw_peak + acc[f][n] % peak * raw_peak / peak);
        tab[into][f][TS_LEN] = tab[into][f][0];
    }
}

/* UI tick: track 1's sample, STRT and LEN; the tables rebuilt when they change. Only with ENG = TIME (sample mode
 * never reads them), and only once the inputs have held still for SETTLE ticks, so scrolling through samples or
 * sweeping STRT doesn't cut, detect and smear for every value on the way (the first build is at once). */
#define SETTLE 6
void digicmi_ts_tick(void)
{
    uint8_t *kit = fw_kit, *snd;
    const volatile u32 *e;
    const short *pcm;
    u32 len;
    int strt, lenv, smear, root;
    static int last_key = -1, still;
    if (!kit)
        return;
    snd = FW_SOUND(kit, 0);
    if (snd[FW_SOUND_MACHINE] != 40)
        return;
#define W(k) (*(int16_t *)(snd + 0x14 + 2 * (k)))
    if (((W(51) >> 8) & 127) < 96)                  /* ENG = SMPL: no waves needed */
        return;
    e = (const volatile u32 *)(SMP_TAB + 16u * (u32)((W(20) >> 8) & 255));
    strt = W(21);
    lenv = W(22);
    smear = (W(47) >> 8) & 127;
    root = 0;                                       /* AUTO always: 0.20-0.22's ROOT selector (slot 52) is gone */
#undef W
    strt = strt < 0 ? 0 : strt > FULL ? FULL : strt;
    lenv = lenv < 0 ? 0 : lenv > FULL ? FULL : lenv;
    pcm = (const short *)e[0];
    len = e[2];
    if ((u32)pcm < 0x40000000u || len <= 248u)      /* the slots' silent dummy */
        pcm = 0;
    if (bank >= 0 && pcm == built_pcm && len == built_len && strt == built_strt && lenv == built_lenv
        && smear == built_smear && root == built_root)
        return;
    {                                               /* settle: the same inputs for SETTLE ticks first */
        int key = (int)((u32)pcm ^ len ^ ((u32)strt << 1) ^ ((u32)lenv << 9) ^ ((u32)smear << 17) ^ ((u32)root << 24));
        if (key != last_key) {
            last_key = key;
            still = 0;
        }
        if (bank >= 0 && ++still < SETTLE)
            return;
    }
    if (bank < 0 || pcm != built_pcm || len != built_len || strt != built_strt || lenv != built_lenv
        || root != built_root)
        cut(pcm, len, strt, lenv, root);
    smear_into(bank == 0 ? 1 : 0, smear);
    root_inc = 0xffffffffu / (cycle_fix >> 8);      /* 2^32 / period / 256: the root plays at its own pitch */
    bank = bank == 0 ? 1 : 0;
    built_pcm = pcm;
    built_len = len;
    built_strt = strt;
    built_lenv = lenv;
    built_smear = smear;
    built_root = root;
}

/* For the page: the root note in use (MIDI), whether ROOT is AUTO, and whether a period was found or set. */
int digicmi_ts_root(int *is_auto, int *found)
{
    *is_auto = root_auto;
    *found = root_found;
    return root_note;
}

/* ---- the voices ---- */
static u32 phase[8];
static int fpos[8], fdir[8], last_pos[8], rate[8];

/* Voice v's place in the pass, 0-65535 across the frames, for the page's arrows. */
int digicmi_ts_pos(int v)
{
    return (unsigned)v < 8 ? (int)((unsigned)fpos[v] / (TS_FRAMES - 1)) : 0;
}

/* Render-task: voice v's block from the tables. play_pos is the stock player's playhead (its move per block is
 * the pitch), mode PLAY's 0-4, time T's 0-120, blend B's 0-127, loop_frame / loop_end the loop's two waves (16.16
 * of 0-15; LOOP and LOOP END), gain the voice's (V + 0x10), start a note's first block.
 * Cost (0.31): measured ~1,160 instructions a voice a block in 0.30, enough with eight voices to push an audio
 * engine already at 85-94% past 100% (the UI then never runs: a "freeze"). Now the per-block sums are cached
 * per voice (TIME, BLND), a block that needs one wave reads one table, and when the render's measured load
 * (cpumeter.c) is above LITE_LOAD the waves are read without interpolation between table points. */
#define LITE_LOAD 235                               /* 256ths: ~92% */
extern volatile int digicmi_cpu_load;
static int c_time[8] = {-1, -1, -1, -1, -1, -1, -1, -1}, c_step[8];
static int c_blend[8] = {-1, -1, -1, -1, -1, -1, -1, -1}, c_width[8];

void ts_voice(int v, int32_t *b, int play_pos, int mode, int time, int blend, int loop_frame, int loop_end,
              int gain, int start)
{
    const short *t0, *t1;
    int adv, i, f0, f1, ff, scl, width, lite;
    u32 inc, p;
    if ((unsigned)v > 7)
        return;
    if (bank < 0 || gain <= 0) {
        for (i = 0; i < 32; i++)
            b[i] = 0;
        return;
    }
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
    inc = root_inc * (u32)(rate[v] >> 8);

    /* the pass, once a block: a whole one (first wave to last) takes 75 x 2^(T/15) blocks */
    time = time < 0 ? 0 : time > 120 ? 120 : time;
    if (time == 0) {                                /* 0.37, TIME OFF: the waves follow the playhead, so the pass
                                                       runs at the note's speed, like the sample engine */
        u32 per = cut_per, d = play_pos > (int)cut_s0 ? (u32)play_pos - cut_s0 : 0, fr = d / per, rem = d - fr * per;
        fr = fr >= TS_FRAMES - 1 ? (u32)LAST_FRAME
           : (fr << 16) + (per > 0xffff ? rem / ((per >> 16) + 1) : (rem << 16) / per);
        fpos[v] = (int)fr;
    } else if (time != c_time[v]) {
        int blocks = (int)(((u32)(75 << (time / 15)) * 32768u) / exp2n[(time % 15) * 16 / 15]);
        c_step[v] = LAST_FRAME / (blocks < 1 ? 1 : blocks) + 1;
        c_time[v] = time;
    }
    if (!start && time)
        fpos[v] += fdir[v] * c_step[v];
    loop_end = loop_end < 0 ? 0 : loop_end > LAST_FRAME ? LAST_FRAME : loop_end;
    loop_frame = loop_frame < 0 ? 0 : loop_frame > loop_end ? loop_end : loop_frame;
    if (!time)
        ;                                           /* OFF: the player's own loop and direction already apply */
    else if (mode == 2) {                           /* FWD.L: first to LOOP END, then round from LOOP */
        if (fpos[v] > loop_end)
            fpos[v] = loop_frame;
    } else if (mode == 4) {                         /* P: first to LOOP END, then between it and LOOP */
        if (fdir[v] > 0 && fpos[v] > loop_end) {
            fpos[v] = loop_end;
            fdir[v] = -1;
        } else if (fdir[v] < 0 && fpos[v] < loop_frame) {
            fpos[v] = loop_frame;
            fdir[v] = 1;
        }
    } else if (mode == 1) {                         /* REV.L: last down to LOOP, then round from LOOP END */
        if (fpos[v] < loop_frame)
            fpos[v] = loop_end;
    } else if (fpos[v] > LAST_FRAME)
        fpos[v] = LAST_FRAME;                       /* FWD: stay on the last wave */
    else if (fpos[v] < 0)
        fpos[v] = 0;                                /* REV: stay on the first wave */
    f0 = fpos[v] >> 16;
    f1 = f0 < TS_FRAMES - 1 ? f0 + 1 : f0;
    ff = (fpos[v] & 0xffff) >> 1;                   /* 15 bits */
    blend = blend < 0 ? 0 : blend > 127 ? 127 : blend;
    if (blend != c_blend[v]) {
        c_width[v] = blend * blend * blend / 63 * 32768 / 32513;   /* the crossfade's share: (B/127)^3 */
        c_blend[v] = blend;
    }
    width = c_width[v];
    if (width <= 0)
        ff = ff >= 16384 ? 32768 : 0;
    else if (width < 32768) {
        ff = (ff - (32768 - width) / 2) * 32768 / width;
        ff = ff < 0 ? 0 : ff > 32768 ? 32768 : ff;
    }
    scl = (int)((((u32)gain >> 15) * 207u) >> 8);   /* gain x 0.81, as a sample's block */
    t0 = tab[bank][f0];
    t1 = tab[bank][f1];
    if (ff >= 32768) {                              /* all of the next wave */
        t0 = t1;
        ff = 0;
    }
    if (f0 == f1)
        ff = 0;
    lite = digicmi_cpu_load > LITE_LOAD;
    p = phase[v];
    if (lite) {                                     /* near the limit: table points, no interpolation */
        if (!ff)
            for (i = 0; i < 32; i++, p += inc)
                b[i] = t0[p >> 24] * scl;
        else
            for (i = 0; i < 32; i++, p += inc) {
                int a = t0[p >> 24];
                b[i] = (a + ((t1[p >> 24] - a) * ff >> 15)) * scl;
            }
    } else if (!ff) {                               /* one wave */
        for (i = 0; i < 32; i++, p += inc) {
            int idx = (int)(p >> 24), fr = (int)((p >> 9) & 0x7fff), a = t0[idx];
            b[i] = (a + ((t0[idx + 1] - a) * fr >> 15)) * scl;
        }
    } else {                                        /* two waves, crossfaded */
        for (i = 0; i < 32; i++, p += inc) {
            int idx = (int)(p >> 24), fr = (int)((p >> 9) & 0x7fff);
            int a = t0[idx] + ((t0[idx + 1] - t0[idx]) * fr >> 15);
            int c = t1[idx] + ((t1[idx + 1] - t1[idx]) * fr >> 15);
            b[i] = (a + ((c - a) * ff >> 15)) * scl;
        }
    }
    phase[v] = p;
}
