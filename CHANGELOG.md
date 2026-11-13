# Changelog

Digi CMI, Digitakt mk1 OS 1.54. Each version is a tagged commit. Hardware: builds up to 0.14 and 0.25 ran on the author's unit; the rest from 0.17 only in digiemu.

## 0.35 (2026-11-13): SUS plays on and fades; no pop-up for unused knobs

On the unit, a SUS note held then released stopped dead without playing the rest of the sample. On release, SUS now keeps the voice looping: it plays on through the rest of the sample, then loops the part after the loop while the AMP decay fades it. Turning B or D on the sample page no longer shows a pop-up.

## 0.34 (2026-11-13): sample page 2 without the time synth's controls

In sample mode, page 2's B and D (the time synth's BLND and TIME) have no box, and turning them changes nothing. Pop-up names follow the engine: in the time synth knob C says "Smear", not "Loop Mode/Smear". Known issue: turning B or D in sample mode still flashes an "=" in the title bar.

## 0.33 (2026-11-13): time synth at the sample's level; named boxes

The time synth no longer turns its waves up to near full scale: they keep the sample's own level, and SMEAR keeps it too. Page 2's BLND, SMEAR and TIME boxes show their names, and their value for a moment while it changes (the S and B prefixes read as 5 and 8 in the small font).

## 0.32 (2026-11-13): RETRO much lighter

With RETRO on, the UI slowed right down while playing, in both engines: RETRO worked out a remainder (a slow division on this processor) on every output sample of every voice. It now does so once per held value, and voices that are off are skipped. The sound is unchanged.

## 0.31 (2026-11-13): LOOP END, lighter time synth

LOOP END is back, on page 2 H (level stays on page 1): END by default, or a place in the sample, snapped to a zero crossing; with SUS, releasing plays from the loop on to END. In the time synth it sets the wave the loop runs to. The time synth costs about a third less, and reads its waves more cheaply when the audio is near its limit. Known issue: the unit froze on 0.30 when ENG was switched while notes sounded; not reproduced in the emulator, and an audio overload is the likely cause.

## 0.30 (2026-11-13): silent voices switch off, by the real amp envelope

A released note's voice is switched off once its amp envelope has decayed below -90 dB for 64 blocks. The amp envelope is the firmware's own (found by disassembling the render), checked against the audio output; 0.26-0.27 used a value that turned out to be the filter envelope's. Known issue: OS 1.53's address is inferred, and the effect on the audio load needs checking on the unit.

## 0.29 (2026-11-13): one playhead, stealing by gate and age

Page 2 shows a single playhead arrow, for the most recent note while it is held, so the page stops redrawing as soon as the note is let go. Voice stealing no longer uses the unreliable envelope level: a voice that is off first, else the oldest released note, else the oldest held sequencer note.

## 0.28 (2026-11-12): voice switch-off withdrawn

0.26 and 0.27 switched a released voice off when an envelope value had faded, but that value follows the filter envelope's settings, not the sound's level, so notes that were still sounding stopped dead. The switch-off is removed; the player is 0.25's again. Known issue: released notes keep their voices running after they fade, which raised the audio load on the unit; arrows and voice stealing use the same unreliable value.

## 0.27 (2026-11-12): faded voices: no dead stops

0.26 ended a released voice as it passed -72 dB and left its "end reached" flag set, so on the unit released notes stopped dead, and a later note on a reused voice was cut when it stopped looping. A voice is now switched off only after its envelope has rested at its floor (about -108 dB) for 64 blocks, and the flag is cleared when the next note starts. Withdrawn: never released; see 0.28.

## 0.26 (2026-11-12): faded voices switch off

On the unit, 0.25 left released notes running silently on every voice (the AMP envelope never quite reaches zero), and the audio load rose from 86% to 94% after playing, with the UI lagging everywhere. A released note that has faded below -72 dB is now ended and its voice switched off, as after boot. Known issue: the load effect is the real chip's and is not measurable in the emulator; check the meter after playing.

## 0.25 (2026-11-12): released notes ring on

A MIDI note took track 1's own voice whenever no key held it, cutting the previous note's release tail, so with SPRD the old note seemed to jump to the new note's side and the two sides never overlapped. Each new MIDI note now takes the quietest free voice. Known issue: track 1's own sequencer trigs still share one voice.

## 0.24 (2026-11-12): SUS plays the tail

A released SUS note stopped at once: since 0.13 the loop ends at END, so "play on to END" had nothing left to play, and the voice ended whatever the AMP release. Now it plays the sample's tail past END to its own end (REV.L: past STRT to its start), or, with no tail, keeps looping so the AMP release fades it. Loop points snap using the sample the voice is actually playing.

## 0.23 (2026-11-12): zero-crossing loops, RETRO dial, no ROOT

Loop start and end snap to the nearest upward zero crossing. RETRO is a dial: off (16 bit 48 kHz), 16 bit 44.1 kHz, and 8 bit 32, 24, 16 and 8 kHz. The ROOT selector is gone (the time synth always detects the pitch). SMEAR's box reads S0-S127 like BLND's. The CPU meter hides while a knob's pop-up is up. Known issue: a project saved with the old RETRO switch on opens at 8 bit 8 kHz.

## 0.22 (2026-11-12): CPU meter

Both CMI pages show the audio engine's peak load over the last half second in the title bar ("42%"), timed from the render's entry to its exit with the unit's RTOS timer, read only. Known issue: its readings are untested on hardware; the emulator only checks its drawing ("--%").

## 0.21 (2026-11-12): lighter on the UI

Page 2 asks for playhead redraws at 15 frames a second instead of 30, and only while the last frame drawn was page 2. The time synth's waves are rebuilt only with ENG = TIME and only once the sample, STRT, LEN, SMEAR or ROOT have held still for a moment, not for every value while scrolling.

## 0.20 (2026-11-12): TIME takes the sample's loop layout; LFIN removed; ROOT on page 1

LFIN (fine loop start) is gone from both engines. In TIME, TIME moves to page 2 D and LOOP (G) sets where the wave loop starts, so PLAY and LOOP work as for the sample. ROOT moves to page 1 F, in the slot Digi Poly's unused kit marker held; older kits read it as AUTO.

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
