# Changelog

Digi CMI, Digitakt mk1 OS 1.54. Each version is a tagged commit. Hardware: builds up to 0.14 ran on the author's unit; 0.17-0.19 only in digiemu.

## 0.19 (2026-11-12): time synth ROOT with pitch detection

TIME now finds the sample's pitch (ROOT = AUTO) or takes it from ROOT (page 2 D), so each wave is exactly one cycle and the root plays at the sample's own pitch. mod.json's description and author are brought up to date.

## 0.18 (2026-11-12): BLND curve, SMEAR, TIME readout

BLND's response is cubic so the whole knob is usable; SMEAR (page 2 C in TIME mode) mixes each wave with its neighbours; G shows the pass time in seconds while turning.

## 0.17 (2026-11-12): time synth replaces the spectral engine

ENG = TIME plays 16 single cycles cut from the sample between STRT and END in turn (PLAY order, G time, B blend). LEV is back on page 2.

## 0.16 (2026-11-11): SPREAD fixed, playheads stop

SPREAD wrote slot 45, which is the AMP volume; PAN is slot 44. Playhead arrows stop when the AMP envelope has finished. The untested spectral engine is compiled out.

## 0.15 (2026-11-11): spectral engine (work in progress)

A Fourier-style wavetable engine for ENG = SPEC and a FORMANT control; paused before testing.

## 0.14 (2026-11-11): RETRO

8BIT becomes RETRO: 8 bits at 24 kHz with the CMI Series I's rate character (the clock follows the note's pitch).

## 0.13 (2026-11-11): LLEN removed

The loop always runs from LOOP (+LFIN) to END. Page 2: PLAY, (empty), LMOD, LFIN / STRT, LEN, LOOP.

## 0.12 (2026-11-11): SPREAD without p-locks

SPREAD writes each voice's pan into the smoothed parameter block every audio block, for any kind of note message (MIDI in included).

## 0.11 (2026-11-11): ENG freeze fix, SPRD, playheads

ENG's pop-up text overflowed machine-pages' 16-byte buffer (the freeze). SPRD alternates notes left/right via PAN p-locks. Page 2 draws a playhead arrow per sounding note.

*Source verified by rebuilding: the firmware patched from it is byte-identical to the 0.11 firmware that was flashed.*

## 0.10 (2026-11-11): CMI on track 1 is the 8-voice mode

The SETTINGS row is gone: CMI on track 1 makes tracks 2-8 its voices (their trigs are silenced and their machines locked). CMI can only be chosen on track 1.

## 0.9 (2026-11-11): fixes from the first hardware test

Machines other than CMI stick on track 1 again; switches snap in set_param instead of being parked every tick (which broke labels and froze ENG).

## 0.8 (2026-11-11): Digitakt II-style page 2, switches

Page 2 is drawn by the mod in the Digitakt II's waveform-page style (value boxes, dotted markers). 8BIT and ENG are drawn as switches. Back on stock machine-pages 1.10.

## 0.7 (2026-11-11): stock waveform view as page 2, PLAY P

Page 2 used the mk1's own ONESHOT waveform view (needs machine-pages 1.11, later dropped). PLAY gains P (ping-pong), replacing DIR; REV.L takes the loop controls.

## 0.6 (2026-11-11): page layout, 8BIT and ENG

Page 1: TUNE, 8BIT, ENG, SAMP, LEV. 8BIT truncates each CMI voice's block to 8-bit steps of the sample; ENG = SPEC silences the voices (engine not built yet).

## 0.5 (2026-11-10): 8 TRACK POLY and blue track keys

A SETTINGS option makes track 1 an 8-voice instrument using tracks 2-8 as voices; holding FUNC lights the track keys blue.

*Reconstructed from the session history: the compiled output and description are identical to the 0.5 build, but cmi_page.c differs from the built file in comment or whitespace text that could not be recovered.*

## 0.4 (2026-11-10): waveform on the loop page

The loop page's lower half draws the sample's waveform with STRT, END and loop markers.

*Reconstructed from the session history: the compiled output is identical to the 0.4 build, but cmi_page.c differs from the built file in comment or whitespace text that could not be recovered.*

## 0.3 (2026-11-10): CMI-style loop controls

Page 2 (machine-pages extras): LLEN, LMOD (HOLD/SUS), DIR (FWD/ALT ping-pong) and LFIN, applied in hooks on the stock ONESHOT players. Loop speed follows the note's pitch; loop points follow knobs, locks and LFOs while notes play.

## 0.2 (2026-11-10): one one-shot sample at root C3

Velocity layers dropped: a single one-shot sample, pitched from MIDI note 60, with velocity setting each voice's volume (the stock gain).

## 0.1 (2026-11-10): CMI machine forked from Digi Poly

Machine id 40 (CMI) with Digi Poly 2.1's polyphony. Velocity layers via a SLICE chain, and voice stealing takes the quietest voice instead of the oldest.
