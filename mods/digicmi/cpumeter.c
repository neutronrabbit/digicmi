/* digicmi: a CPU meter for the audio render, drawn in the CMI pages' title bar (cmi_page.c).
 *
 * Each audio block (1500 a second) the core calls ev_render_in at the render's entry and ev_render_out at its
 * exit (the audio interrupt). Both read the counter of PIT0, the RTOS's time-slice timer: a 16-bit down-counter
 * that runs from PMR to 0 and reloads, every 20 ms. The mod only reads it and changes no timer setting. A block's
 * load is the time from entry to exit over the time from the previous entry to this one (the block period),
 * in 256ths, so the timer's own rate cancels out. The meter shows the highest load over the last ~0.5 s (750
 * blocks): the peak is what decides whether the audio breaks up. Anything the render's interrupt itself is
 * interrupted by is counted in. When the counter doesn't move (digiemu doesn't model it), the load reads -1. */
#include "digitakt-mk1/core3.h"

typedef unsigned int u32;

#define PIT0_PMR    (*(volatile unsigned short *)0xFC080002u)
#define PIT0_PCNTR  (*(volatile unsigned short *)0xFC080004u)
#define WINDOW      750                             /* blocks: 0.5 s */

static unsigned short in_cnt, prev_in;
static int have_prev, blocks, peak;
static u32 period;
volatile int digicmi_cpu_load = -1;                 /* the published peak, 256ths of a block; -1 not measured */

static u32 elapsed(unsigned short from, unsigned short to)    /* down-counter ticks from one reading to a later one */
{
    int d = (int)from - (int)to;
    if (d < 0)
        d += (int)PIT0_PMR + 1;
    return (u32)d;
}

void digicmi_cpu_in(void)
{
    unsigned short c = PIT0_PCNTR;
    if (have_prev)
        period = elapsed(prev_in, c);
    prev_in = c;
    have_prev = 1;
    in_cnt = c;
}

void digicmi_cpu_out(void)
{
    u32 busy = elapsed(in_cnt, PIT0_PCNTR);
    int load = period ? (int)(busy * 256u / period) : -1;
    if (load > peak || blocks == 0)
        peak = load;
    if (++blocks >= WINDOW) {
        digicmi_cpu_load = peak;
        blocks = 0;
    }
}
