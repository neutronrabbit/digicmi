# CMI-style polyphonic sample machine for the Digitakt mk1

## Goal

Recreate the Fairlight CMI-style behaviour of a polyphonic sampled instrument on an original Digitakt (mk1), as an elekloader firmware mod:

- One "instrument" track plays polyphonically; the other tracks (2–8) are not used for sequencing and serve only as voices.
- **Current scope: one one-shot sample, root C3 = MIDI note 60** (the stock root: a sample plays unpitched at 60, so no transposition). Each note plays it pitched from 60. **By default the incoming MIDI note's velocity sets the voice's volume.** No multisampling: velocity layers (built in 0.1) and key zones were dropped.
- Playback modes per voice, including forward, forward loop and forward-then-reverse (ping-pong) between loop markers, so a bell can play forward and then reverse through its tail as the amp envelope fades. Granular is a possible later addition, CPU permitting.

## Platform

- Digitakt mk1, OS 1.53 or 1.54 (update the unit to 1.54 via Elektron Transfer first, so unit and build agree).
- Mods are built with **elekloader** (https://github.com/elekloader/elekloader). Read `docs/ADAPTING.md` and the repository's `AGENTS.md` before writing code; they cover the rules, the hook bus, build/lint/patch commands and a definition of done.
- CPU: ColdFire (68000 family). Toolchain: m68k binutils and gcc (`ELEKLOADER_CROSS` sets the prefix).
- Core 3.0 or later, plus the `machine-pages` mod, so the machine can describe its own SRC pages.
- Recovery: flashing the stock OS file always restores the unit; elekloader never touches the bootloader.

## Decisions so far

1. **Fork Digi Poly** (gdeo607/digi1_mods, MIT; current version 2.1, built on machine-pages) rather than starting from scratch. It provided a POLY machine that borrows other tracks' voices, a voice pool (now replaced by 8 TRACK POLY) and polyphonic MIDI in on the track's channel.
   - **Done: the mod is `digicmi` (0.1), machine CMI = id 40** (`machine:40`; Digi Poly's is 6; ids 4–9, 20–29 and 69 were taken). Changed in `poly_ui.s` (descriptor: params 3, render 3, page_from 3), `poly_glue.s` (the trig's machine check) and `poly.c` (`POLY`).
   - The fork will conflict with the original Digi Poly (same patch sites); only one can be in a build.
2. **Plays as ONESHOT** (descriptor `params` 0, `render` 0, `page_from` 0), as Digi Poly does. Note: machine-pages' second page of extra values needs SLICE's bank (`params` 3), so page-2 parameters would need a switch to that bank later.
3. ~~Samples as one chain / velocity layers~~ Dropped: a single sample. (0.1's velocity → SLICE p-lock code is gone; the message and lock-list facts below stay as reference.)
4. **Hybrid rendering** (later): one-shot renders as stock ONESHOT; loop modes, if wanted, need the machine's own renderer (`render` = own id, filling `fw_track_blocks` via `ev_render_voices`).
6. **Voice stealing takes the quietest voice**, not Digi Poly's oldest-started one, so long release and reverse tails survive when more than 8 notes are in flight. Voices holding a live (key-down) note are still never stolen.
5. **Shared, relative loop settings**, expressed as proportions of each sample's length and applied to all samples (no per-sample settings).

## Proposed parameter layout

Page 1: ONESHOT's own knobs, unchanged. (A velocity-curve or velocity-destination control is a possible later addition: "by default" velocity → volume.)

Page 2 (machine-pages extras; each must range 0–127, i.e. 128 steps):

| Knob | Parameter | Notes |
|---|---|---|
| MODE | playback mode | forward, forward loop, forward-reverse, reverse; granular later |
| LSTA | loop start | proportion of sample length |
| LEND | loop end | clamped after start |
| XFAD | turnaround smoothing | 0 = snap to zero crossings; higher = short crossfade |
| LFIN | loop start fine | optional, if 128 steps are too coarse |
| RATE | loop speed | e.g. reverse leg at a different speed |
| — | spare | |

A "show sample n" control for a waveform display would be editing state held in the mod's RAM, not a sound slot. A full waveform view would need an `ev_draw` handler; treat as a later refinement.

## Suggested order of work

1. Build Digi Poly unchanged from source; confirm it links, lints and runs in the emulator.
2. ~~Velocity → slice~~ built in 0.1, then dropped.
3. One-shot sample, root 60, velocity → volume (current target).
4. ~~Key zones~~ dropped.
4. SRC page parameters via machine-pages.
5. Own renderer: forward and forward-reverse loop modes with shared proportional markers.
6. Measure CPU with all eight voices sounding before adding anything further.
7. Granular, only if CPU headroom allows.

## Open questions

- ~~Where Digi Poly hands a voice its note; which SLICE parameter selects the slice~~ Answered (0.1, verified in digiemu on 1.54; the layer code was later removed, the facts stand):
  - `digipoly_route_c` sees every note message (0x4c bytes) before the render starts its voice. Message: +0 type, +4 on, +8 voice, +12 source (1 sequencer, 2 live), **+20 velocity (s16, 8.8: 100 = 0x6400)**, +24 note, +40 sound, **+68 p-lock list** (ref counted by `MSG_COPY` and the message free, only for type 0), +72 next.
  - Lock lists: alloc 0x400ee20e / release 0x400ee230 on 1.54 (0x400edfe6 / 0x400ee008 on 1.53, by the uniform 0x228 shift: unverified), free list head 0x421f7ee4 (1.54). +0 ref count, +8 count, from +0x14 `{u16 slot, s16 value}` 8 bytes apart; 64 lists of 106 entries. A sequencer note's list is its own (step fire, 0x4006f70a), so it is edited in place; live notes have none and get one only as type 0.
  - SLICE is **sound slot 21** (knob E), lock value `(slice + 1) << 8`. GRID is slot 23 (knob G): `4 << GRID` equal slices; its integer byte is sound + 0x42.
  - **Layers = the number of GRID slices** (4, 8, 16, 32 or 64), so a chain must have one of those counts. `layer = (vel − 1) × slices / 127`.
  - On SLICE's page knob E is SLICE, so the knob does nothing useful on a CMI track; Digi Poly still copies it to sounding voices.
- Sample memory layout (slot → address/length, playback position state) for the own renderer. Not exported by core 3.0 as far as known. Check DIGISLICER, NEIGHBOR and SOPHIE sources first, and ask the elekloader maintainers before reverse-engineering; anything found could be proposed as a core export.
- ~~How Digi Poly persists its voice pool~~ Answered: `kitstore.h` (copied from digi1_mods `src/`). Each sound record has 6 bytes the firmware saves but never uses (sound+0x00..0x03 and parameter slot 0 at +0x14..0x15), so 48 bytes a pattern, shared between the digi1_mods mods by an ownership table in that header. Very little room for per-sample settings. Parameter slots 46..52 are RAM-only (lost at power-off).
- Fast SRAM (2,304 bytes shared by all mods) for the render inner loop.

## The original CMI's looping (EMM "The Fairlight Explained", parts 4–6, 1984–85, muzines.co.uk 8020/8054/9023)

- Page 7: Control 1 = loop start, Control 2 = loop length (in segments; 128 segments in Mode 4), a switch freezes both. Saved with the voice. Page 4 shows/draws them.
- Loop active: the voice keeps looping after release until the DAMPING time (release, up to 65 s; 16 s in Mode 4) ends. No loop: plays on through the remaining segments; reaching the end stops it abruptly.
- SUSTAIN on: loops while the key is held; on release plays the segments after the loop, with no decay.
- START SEG: the start segment chosen per note from a control. SLUR: legato notes start at the loop start.
- Live control: CNTRL1 → loop start and CNTRL2 → loop length while notes sound, sweeping timbre (a playing note's loop moves).
- No loop direction options are described (ping-pong is our addition). Not stated: whether loop speed follows pitch (on a resampling player it does by nature).

## Stock ONESHOT page (seen in digiemu on 1.54)

TUNE, PLAY (FWD, REV, FWD.L, REV.L), BR, SAMP / STRT, LEN, LOOP, LEV. The stock loop runs from LOOP to the end (STRT + LEN) for as long as the amp envelope lasts, and as a resampling player its speed follows the note.

## Loop controls (0.3, built and verified in digiemu on 1.54)

- **Page 2 "CMI LOOP"** (`cmi_page.c`, machine-pages `cm_ui_v33` extras 8–11 = sound slots 46–49, CCs 24–27): LLEN (0 = to END), LMOD (0–63 HOLD, 64–127 SUS), DIR (0–63 FWD, 64–127 ALT), LFIN (LOOP step / 128). Active only when PLAY = FWD.L; every other PLAY and machine is stock.
- **Descriptor**: params 3 (SLICE's bank, needed for extras), render 0 (ONESHOT), page_from 0. With extras the A–H knobs come from the params bank, so E–G carry ONESHOT's look/name/range/default explicitly (ids 0x70–0x72; range table 0x401aa0a4 on 1.54, 52 bytes an id, `{min, max, def}`).
- machine-pages **refuses the whole page unless all 7 extras are valid** (look ≠ 0, `CM_RANGE|CM_DEFAULT`, 0..0x7f00). Extras 12–14 are dummies off-page; extra 14 is slot 52 = Digi Poly kitstore's marker, so its default is the marker (0x4b53).
- **Player hooks** (`cmi_loop.s`/`.c`, same addresses on 1.53/1.54): 0x40075258 (voice 0) and 0x400759a6 (voices 1–7, voice in d4) replace the ONESHOT PLAY read right after the window function (0x40074c9c) returns the window in d0:d1 (samples; forward d0 = start, reverse swapped); 0x400760ba (gain stage, voice in a4) reads PLAY's loop flag. Conflicts with DIGISLICER (same 0x400760ba).
- **Player facts**: voice state V(v) = 0x8000EDC4 + 94v: +4 playhead (samples), +0x10 gain, +0x14 sample length, +0x28 voice on, +0x29 end reached. A note's first block (fw_voice_start bit) uses [STRT, END] and sets the playhead to d0; a looping voice then gets [LOOP, END]. The player always wraps past d1 by the window length; a non-looping voice is ended by the gain stage (gain /4 a block, voice-on cleared) when +0x29 is set. PLAY: 0 REV, 1 REV.L, 2 FWD.L, 3 FWD.
- **Release** = AMP envelope phase 0 (`fw_amp_env[3v]`: 2 attack, 1 gate held, 0 released). It turns 0 at a live note-off and at the end of a sequencer trig's length. Sequencer trigs on an audio track send no note-off message.
- **ALT** turns a block early with a 14-sample nudge (digislicer's measured resampler delay; our own code). Turns land ~1 block early (74 vs 75 blocks).
- Loop points are read every block from `fw_voice_params` (after locks, LFOs, smoothing): turning LLEN glides the loop end on sounding notes (75-sample steps). Speed follows pitch: measured laps ×1.260 (E), ×1.498 (G), ×1.888 (B) vs C.
- **Emulator test sample**: the test maps 1 MB at 0x44000000 (empty sample pool), writes a 9,600-sample sine and points sample slot 37 at it (table 0x4031a3a0 on 1.54, 16 bytes a slot: PCM, 0, length, ratio 0x40000000 = 1.0). Every slot otherwise points at a 248-sample silent dummy.
- Tests: `DIGICMI_LOOPTEST=hold|lfin|alt|sus|live` (one ~1.5 min run each) check windows, wraps, turns, SUS release-to-end, lap time vs pitch and live loop moves; the plain run is the regression. Build: `build/digicmi-0.3-os1.54.elemod`, firmware `build/digicmi-0.3.syx`.

## Waveform on the loop page (0.4)

- `cmi_page.c`: LLEN's `CM_DRAW` callback runs only while the loop page is drawn, so it captures the shown values (`cm_src_shown`, valid only inside a knob's graphic call) and returns 0 for the stock graphic; `digicmi_wave_draw` (`ev_draw`, order 60) then paints over the page's blank lower half with `fw_fillrect` (y = 0 is the bottom row; x 24..125).
- Peaks: 102 columns, 64 probes a column, s16 PCM from the sample table, cached by PCM pointer and length, normalised to the strip. A slot of length <= 248 (the silent dummy) draws a flat line.
- Solid inside the loop, dotted outside; loop markers invert over the wave, with flags on top; STRT/END are ticks at the bottom. Loop markers and dotting only with PLAY = FWD.L. The drawn loop end ignores `MIN_LOOP`. It redraws when the page does, so an LFO moving the loop isn't animated.
- Give each firmware a unique `--version`: 0.3a-d all read `CMI3`, which hid which build a unit was on. 0.4 is `CM04`, `build/digicmi-0.4.syx`.

## 8 TRACK POLY (0.5)

- SETTINGS row `8 TRACK POLY`, one checkbox, replaces Digi Poly's per-track pool. Bit 0 of XB byte 45 (track 8's byte 6t+3) in all 128 kits; read from kit 0 into `digicmi_poly8` each UI tick. Saved with the project, so it is project-wide rather than unit-wide.
- On: track 1 is the instrument (`is_poly` = t == 0; machine byte forced to 40 each tick). Tracks 2-8's sequencer trigs get `M_ID = -1` and their p-lock list emptied; live notes on any track remap to voice 0 with track 1's sound. Pool, mutes and other tracks' trigs no longer restrict stealing. Chord copies carry `TAG_COPY` (0xC5) in M_CHORD so the drop doesn't catch them.
- Off: no borrowing; a CMI track plays one note at a time and keeps its loop controls.
- Blue keys: LED frame = clear 0x4000844e, views set (first setter wins via mgr + 44 + 4 led), finish 0x40008476. Hook at the finish clears the mark and calls set(mgr, led, 2, 0x13) on `0x400084b6(k)`, k 0-7. FUNC state is `0x4217bbb4` (1.54), from the key scan; `ev_key` misses FUNC's release after FUNC+SRC. LED frames only run on changes. 1.53's addresses are unverified.
- Test: the regression sets the mode at start, toggles it through the menu, and checks every LED frame. Old "knob" check was comparing p-locked words; it now turns FLTR knob E and compares slots that moved in the kit (slot 0x21).

## Page layout, 8BIT and ENGINE (0.6)

- Page 1 "CMI": TUNE, 8BIT (extra 12, slot 50), ENG (extra 13, slot 51), SAMP / blank x3, LEV. Page 2 "CMI LOOP": STRT, LEN, LOOP, LFIN / LLEN, LMOD, DIR, PLAY, drawn full screen by `digicmi_wave_draw` (ev_draw) over everything below the title: waveform (y 17-51), loop flags, STRT/END ticks, and the eight values as font5 text in four 32-px columns (A-D at y 8, E-H at y 1). BR is on no page. CCs: LLEN-ENG 24-29. Slots 50/51 are Digi EQ's and Digi Matrix's kitstore markers, so those mods are now conflicts.
- **Stock BR is not a bit-depth control**: measured, it holds samples (rate reduction) and snaps to coarse steps together; BR 100+ is near-square.
- **8BIT** (`digicmi_render`, machine-pages' `ev_render_voices`, order 70): `fw_track_blocks[v]` there is the sample x the voice's gain (V+0x10) x 0.81, before the AMP envelope (measured: block peak/gain stays 0.2966 while the envelope falls). So 8BIT truncates the block to a grid of gain x 207/2^15 (= one 8-bit LSB of the sample). Verified: all values on the grid, levels -46..46 for a 0.366-FS sine. Whether the filter comes before or after this point is not established.
- **ENG = SPEC** zeroes the CMI voices' blocks (the spectral engine isn't built) and page 2 shows "SPECTRAL ENGINE / NOT BUILT YET".
- Test hooks: read blocks at 0x40077fc0 (after machine-pages' render jsr at 0x40077fba), not at 0x40077fba. In zsh, `env X=1 shellfunction` fails silently: export in a subshell.

## Stock waveform view as page 2 (0.7, needs machine-pages 1.11)

- Page 2 is the mk1's own ONESHOT waveform view (SRC from the knob page), with ONESHOT's layout: PLAY, LLEN, LMOD, LFIN / STRT, LEN, LOOP, LEV. Page 1: TUNE, 8BIT, ENG, SAMP / blank x3, LEV. The custom full-screen drawing (0.4-0.6) is gone; DIR is gone (extra 10 / slot 48 now unused).
- **machine-pages 1.11** (discarded after 0.7; this records what it did): `CM_SRC_WAVE` in `cm_ui32.reserved[0]` makes SRC go page 1 -> stock waveform (page 2's knobs and labels) -> page 1 (`mp_src_key`), and a keep2 at 0x4003a9fa (`mp_wave_machine_s`) answers the view's own machine question with the render machine, so it draws ONESHOT's STRT/LOOP widget (core only maps 0x4003a43e). Docs updated (README, ADAPTING, core3.h). Worth offering upstream.
- The view draws the waveform from the firmware's own overview of a loaded sample, so digiemu (no real samples) shows only the baseline and markers. The Digitakt II's view looks different (icons on the top row, stereo); the mk1's is text labels.
- PLAY 4 = P (ping-pong; replaces DIR); REV.L now takes LLEN/LFIN/SUS (SUS release plays down to STRT). STRT/LEN/LOOP have their own `fmt_pos` (the borrowed SLICE bank formatted GRID in the pop-up).
- Tests: LOOPTEST alt = PLAY 4, new `rev` = REV.L + LLEN 32 (upward wraps). All pass on 0.7 with the regression.

## Digitakt II-style page 2 and switches (0.8; back on stock machine-pages 1.10)

- Page 2 is drawn by `digicmi_wave_draw` (ev_draw) again, in the Digitakt II's waveform-page style: value boxes for PLAY, LLEN, LMOD, LFIN (top, y 44-52, centred on the encoder columns x 31/57/83/109; dotted frame when LFIN is 0), the waveform x 24-125 / y 11-39 with dotted STRT, END, loop-start and loop-end marks (inverted over the wave; a short bar on top of the loop ends), the page's own STRT/LEN/LOOP labels, and a LEV slider in H's label place. The SMP box and LEV fader (x < 22) are the page's own. LLEN's CM_DRAW callback is the "page 2 is drawn" signal.
- The 0.7 stock-view route (machine-pages 1.11, `CM_SRC_WAVE`) is not used: the mk1's own view is text labels, not the Digitakt II look. Those machine-pages changes were discarded (elekloader is back on clean `main`); `build/machine-pages-1.11-os1.54.elemod` and `build/mp-exp/` are leftovers from that experiment.
- **8BIT / ENG are switches**: CM_DRAW toggle graphics (0.8 parked them per tick, which broke on the unit; see 0.9). One encoder event moves a 0-127 extra about 0.48; **the first event after the knob rests is absorbed** (knob filter), so a flip takes two events. Not yet checked on hardware.
- **Screenshots-only test**: `DIGICMI_PAGES_ONLY=1 DIGICMI_SRCSHOT=3` stops after the page shots (~25 s) with no playback or sound checks. The user asked not to run the sound tests every time: use this for UI work.

## Fixes after the first hardware test (0.9)

- 0.8 on the unit: machines other than CMI wouldn't stick on track 1 (the 8 TRACK POLY tick forced the machine byte to 40), 8BIT's label stayed on ON/OFF, and turning ENG froze it. Both switch faults came from 0.8's per-tick parking, which fought the knob (the firmware keeps knob values in whole steps).
- 0.9: 8 TRACK POLY no longer touches any machine (track 1 is the instrument whatever it plays; loop/8BIT/ENG still need CMI). Switches: `digipoly_setparam` (the set_param hook, which extras' turns do reach) snaps slots 50/51 of a CMI sound to 0x7f00 when the knob's value rises and 0 when it falls, and writes both kits' words. No tick involvement.
- Knob facts (digiemu): a 0-127 extra moves in whole steps; the first encoder event after rest is absorbed; set_param can arrive with the unchanged value. A switch flips after about three events of turning.
- Verified with the screenshots-only run: switches flip both ways, labels come back after the pop-up like TUNE's, ENG on/off and SAMP turns don't hang, and the machine menu lists all machines.

## CMI on track 1 is the mode (0.10)

- The 8 TRACK POLY SETTINGS row is gone (glue row, `ev_settings`, `settings:POLY` removed). **CMI on track 1 is the 8-voice mode**: `is_poly(kit, t)` = t == 0 and track 1's machine byte is 40. Then tracks 2-8 are voices: their trigs (and p-locks) are dropped, live notes on any track play track 1. The old kit bit (byte 45 bit 0) is no longer read.
- Machine rules: a hook at the setter's entry 0x400225ca (`digicmi_mset_s` / `digicmi_machine_ok`; object's vtable +40 = the sound) refuses CMI on tracks 2-8 and any machine change on tracks 2-8 while track 1 is CMI (returns 0, as the setter's refusal). The menu applies the machine as its cursor moves, so a refused choice stops the cursor (it won't move onto CMI on track 2, nor at all on 2-8 while track 1 is CMI). Verified in the screenshots-only run (`DIGICMI_MACHINETEST=1`).
- **No sound on the unit** (reported on 0.9): most likely ENG left at 64 by 0.8's parking, which read as SPEC (silent). Switches now count as on only at >= 96 (they store 0 or 127), so 64 is off. Checked: track 1's audio with ENG 0 and ENG 64 both play (peak 310982797).

## ENG freeze, SPREAD, playheads (0.11)

- **The ENG freeze**: ENG's pop-up text was "SPECTRAL (NOT BUILT)" (20 chars); machine-pages formats pop-ups into `char pop[16]`, so it overwrote machine-pages' memory (harmless in digiemu, a freeze on the unit). Now "SPECTRAL". **Every `cm_fmt` text must be <= 15 characters for ctx 1** (longest now 14).
- **SPRD** (page 1 knob E; extra 10 = slot 48, CC 26): each note a CMI track starts alternates sides (left first) via a PAN p-lock (meant PAN, but **slot 45 is VOL**; PAN is slot 44: see 0.16). pan = the note's pan (its step's PAN lock, or the track's) moved towards 0 or 127 by SPRD/127, so 127 is always hard left/right. Chord copies of type 0 get their own lock list (copy, release the shared reference); type != 0 messages are edited in place. Measured 16 blocks in (smoothing glides at the start); lock lists don't leak (64/64).
- **Playheads**: page 2 draws an arrow (y 8-10) under the wave at each sounding CMI voice's playhead (`digicmi_voice_pos`: V+0x28 on and is_cmi). `digicmi_page_tick` (ev_tick) asks for a frame each tick (ctrl + 0x20) while page 2 was last drawn and a note sounds; page 1's switch drawing marks page 2 as off show.
- Test options: `DIGICMI_ARROWSHOT=1` (with SRCSHOT) screenshots page 2 during a held note; `DIGICMI_DIR=127` sets slot 48 (SPRD) in the probe.

## SPREAD rework (0.12)

- On the unit, 0.11's SPREAD left every other note centred. digiemu showed 0/127 alternating, but it has no MIDI input; the likely cause is MIDI-in notes arriving as messages that can't carry a list of their own (0.11 only gave type 0 messages one). 0.12 drops the p-lock route: `cmi_spread(m, voice, sound)` records a pan per voice at its note start (any message type; centre = the step's PAN lock or the track's) and `digipoly_levels` writes it every block into the smoothed parameter block, **word 9 + 53 v + slot** (meant PAN, but slot 45 is VOL, see 0.16; the block is at 0x80002760, the same words fw_voice_params indexes), while the voice still holds a CMI sound. Verified: 0/127 alternating on own and borrowed voices; regression passes.
- Testing trap: after rebuilding, re-add the .syx to digiemu before running, or the test's MAP (from the new .elemod) won't match the firmware and every hook lands in the wrong place.

## No LLEN (0.13)

- LLEN removed at the user's request ("I'll use the sample END marker"): the loop always runs from LOOP (+ LFIN) to END (STRT + LEN). `cmi_loop.c` ignores slot 46 (old kits' values too); extra 8 is an unused dummy. Page 2: A PLAY, B empty (dotted box), C LMOD, D LFIN / E STRT, F LEN, G LOOP, H blank (no LEV slider). LMOD's CM_DRAW callback (`lmod_draw`) is now the "page 2 is drawn" signal.
- Loop tests now run with LLEN 0 (loop to END); `live` moves LOOP 60 -> 90 mid-chord and checks the loop start moves. Not rerun yet (UI-only change; screenshots checked).

## RETRO (0.14)

- Page 1 B is **RETRO** (was 8BIT; same slot 50, CC 28): 8 bits (the sample-level grid as before) **and 24 kHz with the CMI's rate character**. A CMI Series I voice pitched by its DAC clock (24 kHz x the pitch ratio), so `retro_rate` holds each output value until the voice's playhead has moved 2 samples of the 48 kHz sample: 2 output samples at the root, fewer higher up, none from an octave up. The ratio is the playhead's move per block (`POS` delta / 32, 16.16; TUNE, bends, LFOs included; a jump of 4096+ keeps the last ratio); a note start resets the phase. Applied in `digicmi_render`, before the AMP envelope.
- Measured (chord 60/64/67/71): held share 0.531/0.427/0.312/0.129 against the clock model's 0.500/0.370/0.251/0.056; the excess is the 8-bit cut making neighbours equal. All values on the 8-bit grid. CMI facts: Series I 8-bit up to 24 kHz (often lower for longer samples, down to ~8 kHz), Series II 8-bit 32 kHz.

## Spectral engine (0.15; replaced by the time synth in 0.17, code deleted)

- Written and building, **not linted, patched or tested**: `spectral.c` (tables: 16 frames x 256 samples from 32 harmonics, double-buffered, rebuilt in the UI tick from track 1's slots 21/22 brightness, 49 odd/even, 46 formant; `spec_voice` per voice: wavetable oscillator at 261.63 Hz x the stock player's pitch ratio, morph by PLAY's mode and G's time, scaled like a sample by gain x 0.81), `spec_tables.h` (generated by `digicmi/tools/gen_spec_tables.py`). Wired: `digicmi_play` loops the whole sample for SPEC voices (pitch reference, keeps gain/envelope running); `digicmi_render` calls `spec_voice`, then RETRO applies; `digicmi_page_tick` calls `digicmi_spec_tick`; page 2 B is now FORMANT (extra 8, slot 46, CC 24) and a SPEC page 2 (`spec_page`: bars, morph arrows, BRT1/BRT2/TIME labels).
- Still to do when resumed: lint/patch, screenshots, one audio check (pitch from zero crossings, morph advancing, gain), CPU cost per block, and confirm the stock player keeps a voice alive with an empty sample slot.

## SPREAD fixed, playheads stop, spectral compiled out (0.16)

- **0.11-0.14's SPREAD wrote VOLUME, not PAN.** Kit sound slot 44 is PAN (0 hard left, 0x7f00 hard right, 0x4000 centre) and slot 45 is the AMP VOL, whatever the range table's last word says (it lists Pan 45, Volume 1; it is not the kit slot index). So SPRD 127 set alternate notes' volume to 0 (silent) or 127. Measured on the mixer (0x40071d26 / 0x40072248): per voice, smoothed words 53 + 53v (pan: (pan - 0x100) clamped 0..0x7e00 -> L/R gains) and 54 + 53v (volume: (track level x VOL)^2 by 0x4007188c). `SLOT_PAN` is now 44; p-lock entries use the same slot numbers as the kit.
- **Stereo out in digiemu**: wrap `emu.ssi.Ssi0Dma.sink` (the GUI sets it to collect the SSI's 8-byte frames, two big-endian 24-bit-in-32 words, left then right). The 0x4ba8f080 buffer reads 0 and no write hooks fire there under the accelerated build. `DIGICMI_LRTEST=1` (with SRCSHOT and LOOPPROBE) plays four single notes from track 1's key and prints left/right per note; `DIGICMI_PANSET` / `DIGICMI_PANSLOT` set a kit slot. Measured on 0.16: SPRD 127 -> L, R, L, R at full level; SPRD 64 -> about 2:1 alternating; kit slot 44 = 0 / 127 -> hard left / right.
- **Playheads stop** when the AMP envelope's level magnitude is below 0x100000 (`digicmi_voice_pos`), so a voice that keeps looping silently shows no arrow and the page stops asking for redraws. Checked: arrow during a held FWD.L note, none 1200 steps after release.
- The paused spectral engine is compiled out: `CMI_SPECTRAL` 0 (cmi_loop.c, cmi_page.c) keeps ENG = SPEC silent as in 0.14 and skips the table builds. spectral.c still links (its 16 KB tables are in .bss). Set `-DCMI_SPECTRAL=1` or the define to resume.
- Build: `build/digicmi-0.16.syx` (version CM16). Only the screenshots-only and L/R runs were made; the full regression and loop tests were not rerun.

## Time synth replaces the spectral engine; LEV back on page 2 (0.17)

- At the user's request the spectral engine is gone (`spectral.c`, `spec_tables.h`, `tools/gen_spec_tables.py` deleted; `CMI_SPECTRAL` removed). ENG is now **SMPL / TIME** (pop-up "TIME SYNTH").
- `timesynth.c`: like the CMI's waveform (time-domain) synthesis, with the waves **cut from track 1's sample** instead of drawn (the user's choice). 16 single cycles at evenly spaced places between STRT and END, each one C3 period (48000 / 261.63 = 183.47 samples; the sample is assumed to be at its root, C3) from the first upward zero crossing, resampled to 256, with a linear ramp removing the end-to-start step (no click). One shared scale for all frames (loudest frame peak -> 29000). Rebuilt in the UI tick (`digicmi_ts_tick`, from `digicmi_page_tick`) when the sample pointer/length, STRT or LEN change; double-buffered.
- Per voice (`ts_voice`, from `digicmi_render`): wavetable at 261.63 Hz x the stock player's pitch ratio (the player loops the sample silently, as for SPEC). PLAY sets the order (FWD once, REV once, FWD.L, REV.L, P), G = TIME of a pass (0.05 s x 2^(G/15)), **B = BLND** (extra 8, slot 46, CC 24, default 127): 0 steps wave to wave like the CMI's segments, 127 crossfades all the way. Output scaled like a sample (gain x 0.81), so envelope, filter, pan, SPREAD, RETRO apply.
- Page 2 with TIME: boxes PLAY, BLND (C, D dotted empty), the sample's overview with STRT/END dotted, a tick for each of the 16 waves' places, an arrow per note at its place in the pass; labels STRT, LEN, TIME, LEV.
- **Page 2 H is LEV again** (both engines; the page's own label, the knob turns level).
- Measured in digiemu (440 Hz test sine, `DIGICMI_ENG=127` with LRTEST): TIME plays at period 183 samples (262 Hz, root C3) vs the sample's 109 (440 Hz); level 540k vs 470k (average |L|). The morph's timbre change wasn't measured (the test sample is a steady sine), nor CPU cost with 8 voices.
- Build: `build/digicmi-0.17.syx` (CM17). Only screenshot and L/R/pitch runs; full regression not rerun.

## BLND curve, SMEAR, TIME readout (0.18)

- **BLND** response is now cubic: crossfade share = (B/127)^3 (64 -> 12.5%, 32 -> 1.6%). The user only heard a change at very low values: once a crossfade between waves is longer than a few ms it sounds smooth, so a linear knob was dead above ~15.
- **SMEAR** (TIME mode, page 2 C = LMOD's slot 47, so it shares LMOD's value; default 0 = off): each wave is mixed with its neighbours (triangle weights, radius SMEAR x 15 / 127 waves) and renormalised to the loudest wave's peak. Per track (track 1's kit value), applied in the UI tick into the table bank (`smear_into`); the raw cut waves are kept in `raw` so a SMEAR change doesn't recut. D is empty in TIME mode.
- **TIME readout**: G's fmt (`fmt_loop`) gives seconds in TIME mode ("0.80S"; pop-up title "Loop/Time=0.80S"), and the page's TIME label overlay is skipped for G_HOLD (60) UI ticks after G moves, so the firmware's value shows cleanly (checked: digits while turning, TIME back ~600 steps later). C's pop-up: "Loop Mode/Smear". `time_mode()` reads track 1's kit ENG.
- .bss is now ~42 KB (two table banks, raw waves, the smear accumulator) of the 128 KB shared mod RAM.
- Checked in digiemu: page shots, pitch 262 Hz and level unchanged with SMEAR 40 / BLND 64. SMEAR's and BLND's sound not measurable with the steady test sine; full regression not rerun. Build `build/digicmi-0.18.syx` (CM18).
- Test: `DIGICMI_TIMESHOT=1` (with SRCSHOT, PAGES_ONLY) turns G on page 2 and snaps the label place over time; `DIGICMI_LMOD` / `DIGICMI_LLEN` set slots 47 / 46 (SMEAR / BLND).

## ROOT and pitch detection for TIME (0.19)

- `timesynth.c` `detect()`: YIN-style cumulative-mean-normalised difference over the middle of STRT..END, at half rate (window 256, lags 20-512 = 1200-47 Hz), first dip below 0.15 (else the minimum if < 0.5, else none), refined at full rate +-2 samples with a parabola. Integer only, once per sample/STRT/LEN/ROOT change in the UI tick. No clear period -> C3.
- **ROOT** = page 2 D in TIME mode (LFIN's slot 49, shared): 0 AUTO, else MIDI note 23 + value (37 = C3). The cycle length is the root's period, and `root_inc` = 2^32 / period / 256, so MIDI 60 plays the sample's own pitch (as SMPL does).
- Page: D box "A G2" (AUTO, found), "A C3?" (AUTO, none), or the set note; pop-up "AUTO G2" / "AUTO (NONE)"; lname "Loop Fine/Root". Note names Elektron style (C3 = 60).
- Measured (digiemu, `DIGICMI_TESTWAVE=sine|saw|noise`, `DIGICMI_TESTHZ`): sine 440 -> 440.4 Hz, saw 196 -> 195.9 Hz (shown G2), ROOT C3 on the sine -> 262.3 Hz, noise -> C3 fallback. Not measured: detection cost on hardware, real recorded samples.
- mod.json 0.19: description rewritten for SMPL/TIME, author "neutronrabbit (fork of Digi Poly 2.1 by gdeo607 / digi1_mods, MIT)". Build `build/digicmi-0.19.syx` (CM19).

## Loop layout for TIME, LFIN gone, ROOT on page 1 (0.20)

- LFIN removed from both engines (cmi_loop.c ignores slot 49 for loops). Slot 49 / extra 11 is now **TIME** (TIME only; 0-127 -> T = v x 120 / 127, 0.05 s x 2^(T/15); default 64 = 0.8 s), page 2 D. Sample mode's D box is empty.
- TIME's **LOOP** = G (slot 23) as in sample mode: `ts_loop_frame` maps LOOP's place between STRT and END to a wave position (16.16 of 0-15; G 0 = STRT). FWD.L wraps to it, REV.L runs from the last wave down to it then wraps to the last, P goes first->last then bounces between last and it. Measured: LOOP 60 -> 7.5; FWD.L jumps back to ~7.5, REV.L wraps at ~7.5, P turns at 7.50 and 15.00.
- **ROOT** = extra 14 = slot 52 (CC 30), page 1 F. Slot 52 was Digi Poly's kitstore marker: `kstore_tick` only rewrote the marker (mask 0 since 0.10), so the call and its state are gone. Values with a low byte (the marker 0x4b53 in older kits) read as AUTO. machine-pages saves all seven extras with the project (packed sound record v4), so slots 46-52 do persist for CMI sounds.
- G's fmt is plain fmt_pos again; the 0.18 TIME label overlay and seconds-while-turning on G are gone (TIME shows seconds in its own D box).
- Build `build/digicmi-0.20.syx` (CM20); digiemu only.

## UI load (0.21)

- The user reported the unit feeling sluggish at times (e.g. selecting samples). Changes: page 2's playhead redraw requests (ctrl + 0x20) every other tick (15/s) and only while the last frame drawn was page 2 (`page2_live`, set in `digicmi_wave_draw`, which ev_draw calls per frame); 0.11-0.20 asked for up to 1000 ticks after page 2 was last drawn. Time-synth tables: only with ENG = TIME, and only after the inputs hold still for 6 ticks (first build at once).
- Measured (digiemu, `DIGICMI_REDRAWTEST=1`): frames per 400 steps with a looping note held: page 2 53 -> 33; after switching to the TRIG page 2 in both 0.20 and 0.21, so the redraw request only drives the SRC view's own frames: the "redraws leak to other screens" theory is NOT confirmed (screens shown by the SRC view itself, like the sample list, untested). Cause of the sluggishness still unknown; a frame's cost was not measured.
- A partial screen update (arrows only) would need the firmware's LCD transfer path: not known; ev_draw only draws inside frames the firmware composes.

## CPU meter (0.22)

- `cpumeter.c`: core 4.3's `ev_render_in` (order 1) and `ev_render_out` (order 250), 1500/s at interrupt level, read PIT0's PCNTR (0xFC080004, 16-bit down-counter, PMR 0xFC080002 + 1 per 20 ms RTOS tick; read only). load = (in -> out) / (in -> next in), 256ths; peak over 750 blocks published in `digicmi_cpu_load` (-1 when the counter doesn't move: digiemu doesn't model PIT counters).
- Drawn by `draw_meter` (cmi_page.c) in the title bar, x 90-103, rows 1-8 from the top (y 55-62): black box, "NN%" in its own 3x5 glyphs, white. That strip is free in both title-bar states (page title; pattern name + tempo box's empty left end). Page 1 is recognised by `switch_draw` running in the frame (`page1_drawn`), page 2 by `lmod_draw`; `cmi_live` = the last frame was one of them. The tick asks for a frame when the published value changes while a CMI page is up (<= 2/s).
- Checked in digiemu: drawing on page 1 ("--%"), page 2 and the pattern-name bar (`DIGICMI_FAKELOAD=108` -> "42%"). Not checked: the readings on hardware (the user should compare 1 vs 8 voices, RETRO, TIME).

## Render load: what the meter's 85% means (0.22, measured)

- On the unit, right after boot, the meter read 85% (peak). PIT0's resolution is fine: PCSR 0x053f, PMR 41249, prescaler 64: 0.485 us a tick, ~1375 ticks a block (read from the emulated firmware's registers, `DIGICMI_PITREGS=1`).
- `DIGICMI_INSNPROF=1` counts every instruction inside the render (0x40077428 -> 0x400784c8) per owner, by nearest MAP symbol (hooks added mid-run need `uc.ctl_flush_tb()`). 100 blocks: idle 85,113 instructions a block (firmware 98.2%, digicmi 1.3% = 1,141, machine-pages 0.4%, core ~0); 4 looping notes 84,913 (firmware 97.6%, digicmi 2.0% = 1,695: digicmi_play 832, digicmi_render 275, digipoly_levels 204). The stock render does the same work with or without notes (all tracks, filters, effects every block), so a high, flat load is the firmware's; Digi CMI adds ~1-2% of it.
- At ~85k instructions a block and ~250 MHz, 85% of a 667 us block is ~1.6-1.7 cycles an instruction: plausible. So the UI gets what is left; Digi CMI cannot lower the stock part.

## Zero-crossing loops, RETRO dial, ROOT gone (0.23)

- `zc_snap` (cmi_loop.c): in `digicmi_play`, LOOP's start and END each move to the nearest upward zero crossing (pcm[i-1] < 0 <= pcm[i]) within 256 samples, the end first (>= start + MIN_LOOP), then the start (<= end - MIN_LOOP). Sample from SMP_TAB by the voice's SAMP (slot 20), used only when its length equals the player's (V+0x14). Cached per voice (pcm, ls, le). Measured: 4799/9600 -> 4800/9491 on the 440 Hz sine, both crossings.
- RETRO = slot 50 as a dial (no CM_DRAW, no set_param snap; only ENG, slot 51, is still a switch): mode = value x 6 >> 7: 0 off, 1 16 bit 44.1 kHz (hold step 71332), 2 8 bit 32 kHz (98304), 3 8/24 (131072, the old RETRO), 4 8/16 (196608), 5 8/8 (393216); 8-bit truncation from mode 2. Old kits with the switch on (127) open at mode 5.
- ROOT removed (page 1 F blank again; slot 52 is an unused, hidden extra, default 0); timesynth always AUTO.
- SMEAR's box is always solid ("S" + value), like BLND's.
- CPU meter vs knob pop-ups: a pop-up fills the title bar (e.g. "Retro=OFF 16BIT 48K" reaches the meter's strip). `digipoly_setparam` sets `digicmi_param_moved` for track voices; the page tick hides the meter for POPUP_TICKS = 120 UI ticks (measured: pop-ups last ~96-120 ticks in digiemu, via `digicmi_uiticks` logged at each snap), then asks for a frame. One frame of the pop-up's slide-out animation can still show the meter over it.

## SUS release fixed (0.24)

- Reported on the unit: SUS + a MIDI note, released, the sound stops at once despite an infinite AMP release. Cause: since 0.13 the loop end is END, and SUS's release window was [LOOP, END] as FWD: the voice finished its pass and hit END, where the player ends a non-looping voice regardless of the envelope.
- Now (cmi_loop.c): released SUS forward plays [LOOP, the sample's length] as FWD (the tail after END) when len - END >= TAIL_MIN (64); REV.L plays [END, 0] when STRT >= 64. No tail: no SUS branch, so it keeps looping (as HOLD) and the AMP release fades it.
- **V(v) + 0 is the playing sample's PCM pointer** (measured with `DIGICMI_VDUMP`); `zc_snap` uses it instead of SAMP's table entry, which lagged a voice's first blocks (HOLD test saw unsnapped windows there).
- Loop test fixed for 0.13/0.20/0.23 (loop to END, no LFIN, snapped points computed from the test PCM; `DIGICMI_LEN` sets LEN). Passing: sus with LEN 90 (36 tails played out) and 120 (28 looped on), hold, rev.

## Released tails ring on (0.25)

- Reported on the unit: with SPRD, pressing a new note seemed to pan the existing note to the new side; L and R never overlapped. Cause: `digipoly_route_c`'s live branch took `w = OWNER[v] < 2 ? v : steal(...)`, i.e. track 1's own voice whenever no key held it, so with a long release every new note cut the previous note's tail on voice 0. Now `w = steal(0, fp, 0)`: the quietest voice not holding a key, voice 0 included.
- Measured (`DIGICMI_OVERLAP=1`, SPRD 127, AMP decay 127 = kit slot 40): note 2 while note 1 decays: 0.24 left 0 / right 626k; 0.25 left 626k / right 626k.
- Still open: track 1's own sequencer trig always plays on voice 0 (stock), cutting the previous trig's tail there. Moving it to the quietest voice is possible in route_c (as chord copies), but the trig's length/note-off handling for a moved own-trig is untested.
- AMP kit slots, by the PAN finding: attack 38, hold 39, decay 40, overdrive 41, delay send 42, reverb send 43, pan 44, volume 45 (the range table's last field is 1 higher for these).

## Faded voices off (0.26) and the 0.25 load report

- On the unit with 0.25: meter 86% after boot, 94% after playing with every note silent; UI lag everywhere (not just page 2). Released notes piled up on all voices (0.25's free-voice choice), each looping silently for good: the AMP envelope settles near -135 dB, never 0 (`DIGICMI_PILEUP=1`).
- In digiemu the pile-up costs ~1% more render instructions (85,966 vs 85,113 a block), so the unit's +8% is likely memory/cache (six samples streaming), which the emulator can't show. The UI's share is what audio leaves (~14% -> ~6%): hence lag everywhere.
- Fix (cmi_loop.c): a released CMI voice below -72 dB gets `fade_end`; `digicmi_stage` then reports it non-looping with end reached (the gain stage quarters its gain each block); once V+0x10 (gain) is 0, `digicmi_render` clears V+0x28 (voice on), its state after boot. Only setting the end flag (first attempt) silences but leaves the voice on. Measured: six released voices all off within 450 steps; overlap, SUS and HOLD tests still pass.
- Unverified on hardware: whether the meter returns to ~86% after playing, and the lag.

## Dead stops after 0.26 (0.27)

- Reported on the unit with 0.26: long release, key held a while then released: the sound stops dead, with SUS and with HOLD. Not reproduced in digiemu with the default envelope (HOLD fades: -18 -> -45 dB in 80 steps).
- Found by reading: 0.26's `digicmi_stage` sets V+0x29 (end reached) for a faded voice and nothing cleared it; `fade_end` itself was cleared only in render, after the next note's first stage call. A later note on that voice was ended once it stopped looping (SUS release). 0.27: at a note's start the stage clears `fade_end`, `fade_count` and, if we set it, V+0x29 (`fade_flagged`).
- 0.27's rule: switch off only after phase 0 and |level| < 0x2000 (~-108 dB) for 64 blocks running (the floor is ~-135 dB). Measured: six released voices off within 600 steps; overlap, sus (LEN 90 and 120), hold pass.
- **The AMP envelope is not in kit slots 38-40**: setting slots 25-35 to 127 changes it (phase 3 appears, slow attack, holds at -9 dB); 36-43 and 1-16 don't. Which of 25-35 is attack/hold/decay: being narrowed (`DIGICMI_SETSLOT`, `DIGICMI_LONGHOLD`). The "AMP kit slots" list under 0.25 above is wrong except pan 44 / volume 45.

## fw_amp_env's level is NOT the audio's level (0.28)

- **Measured (digiemu, `DIGICMI_LONGHOLD=1`):** a held looping note's output (SSI) stays constant (~470k) for 1200 steps while fw_amp_env[3v+1] falls -35, -71, -106, -135 dB; after release the output fades (266k, 136k, 41k, 4k at +40/+80/+160/+320 steps) while that level stays at -135 dB. So the level is not the AMP envelope's.
- It follows kit slots **29 (attack), 30 (decay: 127 = stays full), 31 (sustain: 127 = stays full), 32 (release: 127 = stays after note-off), 35 (a delay: phase 3)**: an ADSR, the filter envelope's shape (`DIGICMI_SETSLOT=slot:value,...`). Slots 36-43 and 1-16 don't move it. The user also reported release behaving better with AMP HOLD at 0.
- The phase (2 attack, 1 held, 0 released) still follows the gate (key down / trig length).
- **Everything keyed to that level is unreliable**: `digicmi_voice_pos` (arrows hide when it is low), `digicmi_steal` (quietest voice), and 0.26-0.27's voice switch-off, which cut audible notes on the unit (dead stops) and is removed in 0.28. cmi_loop.c in 0.28 = 0.25's plus a comment. Earlier notes calling it "the AMP envelope level" (0.1, 0.16, 0.25, 0.26) are wrong on that point.
- **Not found yet: the real AMP envelope / a voice's audible level.** Scanned 0x41980000-0x419b0000, 0x80000000-0x80010000, 0x43990000-0x439b0000, 0x4217a000-0x4217e000 for 16/32-bit values steady while held and falling with the output after release: none (only counters that start running at release, e.g. 0x4199f240 + 20n). Needs disassembly of the render's amp stage (Ghidra) rather than scanning.
- Still open from the unit: after playing, load 94% vs 86% at boot and UI lag; piled-up silent voices cost only +1% instructions in digiemu. Unresolved without a reliable level to switch voices off by.

## One playhead; stealing by gate and age (0.29)

- `digicmi_voice_pos` returns only `newest` (the voice of the latest CMI note start, set in `digicmi_render`), while it is on and its gate is held (phase != 0). Page 2's redraw requests therefore stop at release. Checked: arrow moving while held (x 94 -> 119), none after release with a long release ringing.
- `digicmi_steal`: rank 0 voice off (V+0x28 == 0), 1 released (phase 0), 2 held by the sequencer; within a rank the oldest start. Live-held voices (OWNER 2) never. No fw_amp_env level any more. Checked: six notes took voices 0-5; overlap still both sides. The regression's "every steal took the quietest eligible voice" check is now outdated (not rerun).

## The real AMP envelope (found 0.29+, not used yet)

- **`0x4199ed64 + 8 v` on OS 1.54** (8 bytes a voice; 1.53 presumably 0x4199dd64, unverified): +0 phase, +4 value. Updated by 0x400716c0, the render's amp stage, called after the players with the blocks and three bytes a voice from the smoothed parameters: slots **38 (AMP attack), 39 (hold), 40 (decay)** (bytes 94, 96, 98 of a voice's 106).
  - phase 2 attack: value rises (negated Q31) to 0x80000000, then phase 1 and value = 0.
  - phase 1 hold: value is a **time counter**, the sound is at full; ends when it reaches the hold table's time, or, with HOLD = 127, at the note's release (then phase 0 with value 0x80000000).
  - phase 0 decay: value is the level, multiplied down each sample; 0 when idle (the array is zeroed at init, 0x40071872).
  - So a voice's audible level: phase 1 -> full; else |value| / 2^31.
- **Verified against the output (digiemu, `DIGICMI_LONGHOLD=1`):** after release the value reads 0.60, 0.31, 0.093, 0.0096 at +40/+80/+160/+320 steps; the output's level relative to held is 0.57, 0.29, 0.087, 0.0090 (averaged over the 20 steps after each).
- `fw_amp_env` (0x4199ef54 on 1.54) is the **filter** envelope: 0x40073304 fills it from parameter words 76, 78, 80, 82, 88 = slots 29, 30, 31, 32, 35 (attack, decay, sustain, release, delay). The core's name and docs are wrong; worth reporting upstream with a proposal to export the real one.
- Used from 0.30 for the voice switch-off (below). Still possible: a true "quietest voice" for stealing, arrows that follow the sound.

## Voices off when silent, by the real AMP envelope (0.30)

- cmi_loop.c: `AMP_PHASE(v)` / `AMP_VALUE(v)` at AMPENV (0x4199ed64 on 1.54; 0x4199dd64 on 1.53 by the region's 0x1000 shift, unverified). A CMI voice that is on, in phase 0 (decay) and |value| < 0x10000 (-90 dB) for 64 blocks running gets `fade_end`: `digicmi_stage` reports it non-looping with end reached (gain quartered per block), and once the gain is 0 `digicmi_render` clears V+0x28. A note's start clears `fade_end`, `fade_count` and the end flag we set (`fade_flagged`), in the stage (the first thing to run for it).
- Measured (digiemu): pile-up voices go off in order of their decay; a 1200-step held note's release fades 0.9975 -> 0.0097 by +320 steps and stays on meanwhile (no dead stop); overlap both sides; sus (LEN 90) and hold pass. The regression's old "quietest" steal check is retired (only eligibility is checked).
- Unverified on the unit: the load after playing (94% -> 86%?) and the lag.

## Freeze report; LOOP END; lighter time synth (0.31)

- On the unit with 0.30: froze when switching ENG (sample <-> time synth) while notes sounded. Not reproduced in digiemu (`DIGICMI_ENGFLIP=1`: chord held, ENG on and off on page 1; the run completes, no faults). Likely an audio overload: the meter already reads 85-94%, and the time synth measured ~1,160 instructions a voice a block (4 voices: 89,672 a block vs 85,238 in sample mode); past 100% the render's interrupt runs back to back and the UI never runs, which looks like a freeze. digiemu doesn't run in real time, so it can't show this. Unconfirmed.
- 0.31 `ts_voice`: TIME/BLND sums cached per voice; one table read when the crossfade is 0 or 1 or the two waves are the same; when `digicmi_cpu_load` > 235 (~92%) table points without interpolation. Measured, 4 voices: 4,628 -> 3,078 instructions a block (lite: 1,668).
- **LOOP END** = extra 14, slot 52 (CC 30), page 2 H in both engines (LEV on page 1 only). 0 (default; also any value with a low byte, as old kits hold) = END; 1-127 = value x 120 / 127 on the 0-120 scale, kept in [LOOP, END]. Sample: the loop window's end (snapped as before); SUS with a LOOP END plays [LOOP, END] as FWD on release; without one, 0.24's tail past END. TIME: `ts_loop_end` -> the wave the loop runs to (FWD.L to it then round from LOOP, P between them, REV.L from LOOP back to it). Page 2 draws its marker.
- Slot 52 was the last free extra: SMEAR keeps sharing LMOD's slot, and SLUR (parked on `slur-wip` as a slot-52 switch) needs reworking as LMOD settings.
- Tests: `DIGICMI_LEND` sets slot 52; the loop test computes the window with it. Passing: hold, hold LEND 100 ([4800, 7528]), sus LEND 100, sus LEN 90.

## RETRO's cost (0.32)

- On the unit (0.31): the UI slowed right down while playing with RETRO on, in both engines; fine with RETRO off. Cause: `b[i] -= b[i] % step` on every output sample of every CMI voice (a `rems.l`, ~20-35 cycles on the ColdFire, counted as one instruction by `DIGICMI_INSNPROF`, which is why the profile under-read it), plus a `%=` (remu) per held value in the clock.
- 0.32: `retro_rate(v, b, mode, step)` truncates once per taken value (the held values are the only values); the clock's wrap is a subtraction (a loop only above an octave up). `digicmi_render` skips voices that are off (V+0x28 == 0). Divisions at 8/24 near the root: ~48 -> ~16 per voice per block. Instructions, 4 voices RETRO 8/24: digicmi_render 2,834 -> 2,270 a block.
- Checked: RETRO held shares 0.531/0.427/0.312/0.129 and 0 off-grid values, same as 0.31; regression PASS. Block-by-block output can't be compared across builds in digiemu (note starts shift with code timing; `DIGICMI_BLKDUMP` writes the blocks).
- Lesson: on this CPU, count divisions, not just instructions, in per-sample code.

## Time synth level; named boxes (0.33)

- timesynth.c `cut()` keeps the cycles at the sample's level (clamped to s16, `raw_peak` = their peak); `smear_into` rescales to `raw_peak` instead of 29000. Measured on the 440 Hz test sine: TIME 469,034 vs SMPL 469,968 average output (0.32: up to ~2x).
- cmi_page.c: BLND / SMEAR / TIME boxes show "BLND", "SMEAR", "TIME", and the value (seconds for TIME) for VAL_TICKS = 45 UI ticks after it changes (`*_age`, `last_*`; the page tick asks for one frame when the time is up). The 0.18-0.32 "B"/"S" prefixes read as 8 and 5 in font5. Test: `DIGICMI_BOXTURN=1` with TIMESHOT turns B and snaps.

## Sample page 2 without the time synth's controls (0.34)

- cmi_page.c: no B/D boxes in sample mode; `fmt_blnd` / `fmt_time` give "" there. B/C/D's lnames are static char buffers (`lname_b/c/d`) rewritten by `names_follow_engine()` in the page tick (machine-pages' `mp_label` returns k->lname at each pop-up): sample "", "Loop Mode", ""; time "Wave Blend", "Smear", "Wave Pass Time".
- poly.c `digipoly_setparam`: slots 46/49 on a CMI sound with ENG (slot 51) < 96 keep their value (written back to both kits).
- A knob's fmt can't hide its pop-up: returning 0 falls back to the firmware's text (pages.c `mp_pop_text`), so B/D show "=" (empty name and value).
- Checked (`DIGICMI_BDTURN=1` with TIMESHOT): sample B/D values unchanged, C changes; time all three change; pop-ups "=" / "Smear=45".

## SUS release loops the tail; B/D pop-ups hidden (0.35)

- Reported on the unit (0.34): SUS, LOOP at 0, LOOP END ~10%, a held MIDI note released -> stops dead, the rest of the sample doesn't play (a tap fades fine). Not reproduced in digiemu (`DIGICMI_SUSREL=<steps>` holds track 1's key, releases, logs playhead, end flag, gain and the real AMP envelope): the one-shot tail played out there. Cause on the unit unknown; the release used the firmware's non-looping path (P_FWD + window to END/sample end), which ends voices.
- 0.35 (`sus_tail`): released SUS keeps P_FWDL/P_REVL with the window on the tail: forward [LOOP END, END] with a LOOP END, else [END, sample end]; REV.L [0, LOOP]. Ends on zero crossings, cached per voice. A tail < MIN_LOOP keeps the main loop. The voice never takes the non-looping path; the AMP decay fades it, 0.30's switch-off ends it. Measured with the user's settings: playhead 672 -> 7840 after release, then 4451, 3622 (round the tail), AMP 0.99 -> 0.59, still on.
- Loop test "sus" rewritten for this (window = the snapped tail, PLAY 2, playhead past LOOP END); passing for LEN 120 (40), LEN 90 (36), LEND 100 (36); hold passes.
- B/D pop-ups in sample mode: `digipoly_setparam` sets `digicmi_mute_pop`; the page tick holds `mute_hold` for POPUP_TICKS (cancelled by another knob's turn) and `digicmi_wave_draw` paints the title bar black (x 18-127, y 55-63) over the "=". Checked: B and D plain, C's pop-up shows.

## SUS dead stop found: ping-pong's end flag (0.36)

- The user plays almost always in **P (ping-pong)**; every SUS test before used FWD.L. Reproduced on 0.34 (`DIGICMI_SUSREL=300 DIGICMI_PLAY=4 DIGICMI_LOOP=0 DIGICMI_LEND=13 DIGICMI_LMOD=100`): V+0x29 (end reached) is 1 throughout ping-pong; at release SUS returned P_FWD, the gain stage saw non-looping + end reached and quartered the gain: 1048379939 -> 249 -> 0 in two steps, the playhead running on silently (so position-only checks passed).
- 0.36: SUS is a play-once tail again (0.31 rules: [LOOP, END] with a LOOP END, else [LOOP, sample end]; REV.L [LOOP END, 0]), and at the moment it leaves the loop (`sus_left`) V+0x29 is cleared. Measured: P and FWD.L, gain unchanged after release, playhead to the sample's end, then the voice ends. 0.35's looping tail (`sus_tail`) is gone.
- Loop test sus: play-once check again (FWD to END/sample end, playhead past LOOP END); `DIGICMI_SUSPLAY=4` runs it in P. Passing: sus LEN 90, LEND 100, LEN 120, P + LEND 100; hold; alt.
- **B/D on the sample page**: `digicmi_enc` (core `ev_enc`, order 40; `int f(void *brain, void *ev)`, ev+12 = encoder 1-8) returns 1 for encoders 2 and 4 while `page2_live` and ENG = SMPL: no set_param, no pop-up. 0.35's black title bar is gone. Checked: sample B/D values unchanged and the title bar keeps the pattern name; time synth B/D/C all change.
- Lesson: test in the mode the user plays (P), and check gain/audio, not only the playhead.

## Constraints and facts

- **Velocity → volume is stock.** The render (0x40077a2c on 1.54) takes each voice's velocity from its message's +20 word, adds a modulation term, clamps it to 0–127 and stores it in `fw_voice_vel[v]`. Both sample players (0x40075184, 0x400757fe) pass it with the voice's volume to 0x40074c60, which returns **gain = (vel/127)² × (VOL/127)**: a square-law curve. digicmi adds no code for it; the test checks that every CMI voice, stolen ones included, is given its own note's velocity.
- **Root C3 = MIDI 60**, the stock unpitched note: no transposition.
- **digiemu's factory project plays silence for samples** (every `fw_track_blocks` block reads 0, at the render's 0x40077fba too), so audio levels can't be measured there; check the gain's inputs instead.

- `fw_amp_env` (core export; 0x4199ef54 on 1.54) is the **filter** envelope, whatever its name (see 0.28 / "The real AMP envelope"); its phase still follows the gate. The real AMP envelope is at 0x4199ed64 + 8 v.
- `digicmi_steal` in poly.c is non-inline on purpose: `digicmi/tests/digiemu_poly.py` hooks it and checks every steal took an eligible voice (`DIGICMI_STEALS=1` prints each steal).
- Mod RAM: 128 KB shared by all mods (256 KB with core 4.0, chosen automatically when needed).
- Sample RAM: 64 MB per project; 48 kHz, 16-bit mono ≈ 96 KB/s. 16 × 6 s ≈ 9 MB.
- Audio tracks are monophonic on stock firmware; polyphony comes from borrowing tracks.
- The DT mk1 has no velocity-sensitive pads: test velocity layers with a MIDI keyboard.
- Kits saved with an added machine load that track as ONESHOT on the stock OS.

## Tooling and layout

Everything lives under `~/Dev/digihack`:

```
env.sh            # source at the start of each session (PATH, ELEKLOADER_CROSS=m68k-elf-, PYTHONPATH, venv)
tc                # wrapper: runs a command with env.sh loaded, from the digihack root (no Docker)
tools/m68k-elf/   # local binutils 2.43.1 + gcc 14.2.0 (C only), bare-metal m68k-elf target
tools/build-toolchain.sh  # rebuilds it from tools/src/ (log in tools/build.log)
tools/ghidra/  tools/jdk/
.venv/            # Python 3.9 venv: Unicorn and analysis scripts (elekloader itself needs no deps)
elekloader/       # clone
digi1_mods/       # upstream Digi Poly clone, reference only (not edited)
digicmi/          # THE mod repo: this project's code, starting from Digi Poly's source
firmware/         # stock .syx files (Digitakt_OS1.54.syx, hash checked by elekloader)
build/            # built .elemod files and test .syx (derived from stock: never commit)
ghidra-projects/
```

- **Never install tools outside `~/Dev/digihack`.** No Homebrew, global pip/npm, or system-wide installs; JDK, Ghidra, Python venvs and similar go inside this folder. If something genuinely can't be installed locally, ask first.
- `digicmi/` is the mod repo. Digi Poly's code is copied into it (keeping its MIT licence notice), and all changes are made there.
- Run builds through the wrapper, e.g. `./tc python3 -m elekloader.sdk.build <mod-dir> --stock firmware/Digitakt_OS1.54.syx`.
- Ghidra (native on the Mac) for disassembly; **digiemu** (github.com/irpina/digiemu, the emulator digi1_mods tests Digi Poly in, `tests/digiemu_poly.py`; pinned rev in digi1_mods `tools/dev.sh`) for running the real firmware; Unicorn for testing routines in isolation.
- The mod lives in `digicmi/mods/digicmi/` (Digi Poly 2.1 source, upstream commit in `UPSTREAM`, plus `kitstore.h`). Build: `./tc python3 -m elekloader.sdk.build digicmi/mods/digicmi --stock firmware/Digitakt_OS1.54.syx --out build`. Patch with `build/core-4.3-os1.54.elemod` and `build/machine-pages-1.10-os1.54.elemod`.
- digiemu is at `tools/digiemu/` (pinned rev 3206402, Python 3.12 venv via local `uv`, patched Unicorn 2.1.4 built with `PKG_CONFIG=false TMPDIR=$DIGIHACK/tools/tmp tools/install-patched-unicorn.sh`; qemu's configure aborts without pkg-config otherwise). `uv` and `cmake` are in `tools/bin/`; `env.sh` keeps uv's and pip's caches under `tools/`.
- Emulator loop: patch a `.syx` into `build/`, then `tools/digiemu/.venv/bin/python -m emu.portable --add build/<name>.syx --yes` (about a minute; `--list` shows the folder name, e.g. `dt1-cmi1-5a2e323b`), then `tools/digiemu/.venv/bin/python digicmi/tests/digiemu_poly.py --digiemu tools/digiemu --fw <folder> --stock firmware/Digitakt_OS1.54.syx --elekloader elekloader --mods build/core-4.3-os1.54.elemod build/digicmi-0.3-os1.54.elemod build/machine-pages-1.10-os1.54.elemod --png build/emu-png` (about 1 minute, prints PASS/FAIL). `DIGICMI_VELS=1` overrides each CMI note's velocity across the layer edges and checks the SLICE lock and the voice's SLICE; the lock-list leak check always runs. Use absolute paths, or run it from the digihack root.
- Always test in the emulator before flashing. Flashing is done manually by the user through Elektron Transfer; never attempt it from here.

## Git repository

- `digicmi/` is a git repo (branch `main`), one commit per version tagged `v0.1`...`v0.18`, then repo docs. Earlier versions were reconstructed by replaying the development session and checked against each `.elemod`'s `build.sources` hashes (0.11 by rebuilding the flashed .syx byte-for-byte; 0.4/0.5 match in compiled code only, `cmi_page.c` comment text differs).
- For each new version: bump `mod.json`, build, test, then commit and tag `vX.Y`, and add a CHANGELOG entry. Never commit `.syx`/`.elemod` files or anything from the stock OS (`.gitignore`).
- Modwerk (modwerk.app) takes Digitakt mods as released `.elemod` files via a PR to repeat98/modwerk (`sdk/digitakt/modules/<id>/`, owner review, hardware evidence, real LCD captures, 320x192 SVG thumbnail, changelog JSON). The elekloader shop is a separate route (Submit a mod issue form). (0.19 fixed mod.json's description and author.) Identity in anything public: the handle neutronrabbit and its no-reply email only.

## Key references

- elekloader ADAPTING.md: https://github.com/elekloader/elekloader/blob/main/docs/ADAPTING.md
- elekloader core 3.0 release: https://github.com/irpina/elekloader/releases/tag/core-v3.0
- digi1_mods (Digi Poly): https://github.com/gdeo607/digi1_mods
- Digi Poly on Modwerk: https://modwerk.app/digitakt/module/digipoly/
- DIGISLICER: https://github.com/irpina/digislicer

## TIME OFF and arrow pacing (0.37)

- TIME 0 (put_secs shows OFF) in `ts_voice`: `fpos` = (POS - cut_s0) / cut_per as 16.16 waves, clamped to LAST_FRAME; `cut()` stores wave 0's sample (`cut_s0`) and samples per wave (`cut_per`). The pass's own step and its PLAY/loop logic are skipped (the stock player already loops and turns). Emulator (LRTEST + LOOPPROBE, FWD, ENG TIME): waves 0-15 then round the loop; TUNE 76 wraps 5 times to TUNE 64's 2; TIME 20 runs 9.3-15 without wrapping.
- Arrow redraws (`digicmi_page_tick`): every 3rd UI tick, every 6th when `digicmi_cpu_load > 230`, and none until TURN_QUIET (15) ticks after the last `digicmi_param_moved`. REDRAWTEST + TURNFRAMES (D turned every 8 steps) gives the same frame counts for 0.36 and 0.37 (7/15/7 per 400 steps): the emulator's own draw rate is the limit, so the effect is only visible on the unit.

## Loop window cache and the firmware's render profile (0.38)

- `digicmi_play` caches each voice's window (s0, se, ls, le, LOOP END) keyed on SLEN, PCM and slots 21, 22, 23, 52 (`wc_*`). Checked: LOOPTEST alt and sus PASS in P; SUSREL (P, LOOP 0, LEND 13, LMOD 100) trace identical to 0.37. Instructions barely change (816 vs 832 a block, 4 notes): the saving is ~13 divisions a looping voice a block, which INSNPROF counts as one instruction each.
- `DIGICMI_FWPROF=<prefix>` (with INSNPROF) splits the firmware's share by 64-byte region, top 30 printed, all saved as JSON. Idle and 4 notes held are the same region for region (83.6k vs 82.8k a block): the stock render does all eight voices' work whether they play or not.
- By 1 KB region (idle): 0x40075800-0x40076400 (the sample players and gain stage, where cmi_loop.s hooks) ~17.6k a block, ~21% of the render; 0x40075ec0 alone 8,240 = 8 voices x 32 samples x ~32. 0x40071800-0x40074c00 ~55k (amp, filters, effects; not split by voice yet). Off voices likely play the silent dummy sample (len 248) through the full loop.
- Candidate: skip the player for a voice that is off (V+0x28 = 0), writing a zero block: up to ~2.2k instructions a voice a block. Not started: needs the player's call structure (the render calls through function pointers, (a2) x18).
- `fwdis.py` (scratchpad, not in the repo): capstone M68K_040 over the main section at 0x40000000, stepping 2 bytes past ColdFire-only opcodes.
