# FURYBOX

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)
[![firmware](https://github.com/doug-parks/furybox/actions/workflows/firmware.yml/badge.svg)](https://github.com/doug-parks/furybox/actions/workflows/firmware.yml)

**FURYBOX** is firmware for the **M-VAVE FM-1** that turns it into a KORG Poly-800 style polysynth. It is a fork of
[Felucca](https://github.com/hugelton/Felucca) by Hügelton Instruments with one new engine, **FURY**, after the
Poly-800 (1983) and Full Bucket Music's [Fury-800](https://www.fullbucket.de/music/fury800.html) simulation of it.
Everything else Felucca has (its other engines, the sequencer, song mode, effects, web editor, backups) is still there.

- **Install:** [web installer](https://doug-parks.github.io/furybox/webapp/installer/) (Chrome or Edge, a USB data cable)
- **Try it in your browser:** [emulator](https://doug-parks.github.io/furybox/webapp/try/), no FM-1 needed
- **Edit:** [web editor](https://doug-parks.github.io/furybox/webapp/editor/)
- **Build:** [BUILDING.md](BUILDING.md) (Felucca's; the GitHub Actions workflow in `.github/workflows/firmware.yml` is
  the reference build)

Installing is at your own risk. Back up your FM-1 first. Felucca's installer **Return to official V15** (with M-VAVE's
`FM-1.fwsc`) takes you back to stock; M-VAVE's own Workbench takes you to their Open firmware.

## The FURY engine

The Poly-800's oscillators don't draw a waveform. Each DCO is four square waves an octave apart, the 16' 8' 4' and 2'
"harmonics", switched on and off. In SQUARE mode the footages add at equal level; in SAW mode each footage is half the
one below, a staircase that approaches a sawtooth. That rough, hollow additive tone is the Poly-800's character, and
FURY builds its voices the same way (band-limited, one oscillator's cost per DCO whatever is switched on).

FURY's eight engine parameters (EDIT 1 and EDIT 2 on the FM-1, the DCO and VCF pages):

| Parameter | What it is |
| --- | --- |
| **DCO1** | Wave and footages: `S` saw or `Q` square weights, then the footages on (`1` 16', `8`, `4`, `2`; `-` off). `S18--` is a saw of 16' and 8'. |
| **DCO2** | `OFF` (WHOLE mode: DCO1 alone, as the 8-voice Poly-800) or a wave and footages like DCO1 (DOUBLE mode) |
| **INTV** | DCO2's interval, -24 .. +24 semitones (the Poly-800's octave switch and INTERVAL) |
| **DTUN** | DCO2's detune, 0 .. ~63 cents |
| **CUT** | The VCF, a 24 dB low-pass; **RES** its resonance (it never self-oscillates, like the original) |
| **KBD** | The VCF's keyboard tracking: OFF, HALF, FULL |
| **NOIS** | White noise into the VCF |

The rest of the Poly-800 is the track's, in Felucca's pages: the DEG is **ENV** (ATK DEC SUS REL), the VCF envelope is
**ENV > FILTER** (negative = the Poly-800's inverted POLARITY), the MG is the **LFO** (RATE, FADE as the MG delay, LFO >
PITCH and FILTER as its DCO and VCF depths) and the chorus is the **CHORUS** send. The filter is per voice (Full Bucket's
"God Mode"); the Poly-800's single shared VCF is not modelled.

Twelve presets come with it: POLY BRASS, 800 STRINGS, SQR ORGAN, BELL 800, DCO BASS, POLY LEAD, WHALE PAD, HARPSI 800,
NOISE SWEEP, FUNK CLAV, CHOIR 800, DOUBLE SAW. The FM-1 powers on with FURY on tracks 1 to 3 and Felucca's drums on 4.

MIDI: CC74 and CC71 set CUT and RES, as on every Felucca engine; the editor and MIDI LEARN reach every parameter.

## Roadmap

- The Poly-800's six-stage DEG (attack, decay, break point, slope, sustain, release) as its own envelope
- Separate DEGs for DCO1, DCO2 and the VCF, and the SINGLE / MULTI trigger
- A shared (paraphonic) VCF option
- Poly-800 SysEx import (EX-800 / Poly-800 II banks) into FURY sounds
- Full Bucket's BRIGHT and +6 dB switches

## Credits and licence

FURYBOX is free software, [GPL-3.0-only](LICENSE), like Felucca. The FURY engine and the changes that add it are
Copyright (C) 2026 Doug Parks; everything else is Felucca's, Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
Hügelton Instruments, and the community ([CREDITS.md](CREDITS.md), [LICENSING.md](LICENSING.md)). The engines in
`firmware/engines` are MIT ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)).

FURYBOX is an independent implementation. It contains no code or data from KORG's Poly-800 or Full Bucket Music's
Fury-800, which inspired it. KORG and Poly-800 are trademarks of KORG Inc.; M-VAVE and FM-1 are trademarks of their
owner; Fury-800 is Full Bucket Music's. None of them is affiliated with or endorses FURYBOX.

FURYBOX is developed with the assistance of AI coding agents, as Felucca is.
