/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Doug Parks (FURYBOX), on Felucca by Leo Kuroshita, Hügelton Instruments */
/* FURY (FURYBOX): a Poly-800 style polysynth, after the KORG Poly-800 (1983) and Full Bucket Music's Fury-800
 * simulation of it (an independent implementation: nothing of either is in here).
 *
 * The Poly-800's oscillators do not draw a waveform: each DCO is four square waves an octave apart, the 16' 8' 4'
 * and 2' "harmonics", switched on and off. In SQUARE mode the switched-on footages add at equal level; in SAW mode
 * each footage is half the one below (16' 1, 8' 1/2, 4' 1/4, 2' 1/8), a staircase that approaches a sawtooth
 * (the KORG Delta did the same). That rough, hollow additive tone is the Poly-800's character. Here:
 *   DCO1   the wave and its footages, one value: S / Q (saw / square weights) and the footages on (1 8 4 2, - off)
 *   DCO2   OFF (WHOLE mode: DCO1 alone, as the Poly-800 with 8 voices) or a wave and footages as DCO1 (DOUBLE mode,
 *          the Poly-800's 4-voice mode; here every voice has both)
 *   INTV   DCO2's interval, -24 .. +24 semitones (the Poly-800: its octave and INTERVAL 0..12)
 *   DTUN   DCO2's detune, 0 .. ~63 cents
 *   CUT    the VCF, a 24 dB low-pass (two state-variable stages); RES its resonance (it never self-oscillates)
 *   KBD    the VCF's keyboard tracking: OFF, HALF, FULL (the Poly-800's KBD TRACK 0 1 2)
 *   NOIS   white noise into the VCF
 * The rest of the Poly-800 is the track's: the DEG is ENV (ATK DEC SUS REL), its VCF envelope the ENV > FILTER
 * amount (negative: the Poly-800's inverted POLARITY), the MG is the LFO (RATE, FADE as the MG DELAY, LFO > PITCH /
 * FILTER its DCO and VCF depths), the chorus the CHORUS send. The filter is per voice (Full Bucket's "God Mode"); the
 * Poly-800's one filter shared by all voices is not modelled (a part-level filter would need a host hook).
 *
 * The staircase: the four footage squares of one DCO all change state at multiples of 1/16 of the 16' period, so
 * the DCO is one phase accumulator (the 16' cycle) and a table of 16 levels (fury_levels): the output is the level
 * of the current sixteenth. Band-limited with one polyBLEP at each sixteenth's edge, scaled by the step there (0 at
 * the edges no footage crosses): the cost of one oscillator, whatever is switched on.
 * State: s[0..1] VCF stage 1, s[2] noise, s[5..6] VCF stage 2; ph[0] DCO1, ph[1] DCO2 (both the 16' phase). */

/* DCO values: wave (0 saw, 1 square) x 16 + the footages on (bit 3: 16', 2: 8', 1: 4', 0: 2') */
#define FURY_DCO(w, h) ((w) * 16 + (h))
#define FURY_DCO2(w, h) (1 + FURY_DCO(w, h))     /* DCO2: 0 is OFF */
static const char *const N_FURY_DCO[32] = {
    "S----", "S---2", "S--4-", "S--42", "S-8--", "S-8-2", "S-84-", "S-842",
    "S1---", "S1--2", "S1-4-", "S1-42", "S18--", "S18-2", "S184-", "S1842",
    "Q----", "Q---2", "Q--4-", "Q--42", "Q-8--", "Q-8-2", "Q-84-", "Q-842",
    "Q1---", "Q1--2", "Q1-4-", "Q1-42", "Q18--", "Q18-2", "Q184-", "Q1842",
};
static const char *const N_FURY_DCO2[33] = {
    "OFF",
    "S----", "S---2", "S--4-", "S--42", "S-8--", "S-8-2", "S-84-", "S-842",
    "S1---", "S1--2", "S1-4-", "S1-42", "S18--", "S18-2", "S184-", "S1842",
    "Q----", "Q---2", "Q--4-", "Q--42", "Q-8--", "Q-8-2", "Q-84-", "Q-842",
    "Q1---", "Q1--2", "Q1-4-", "Q1-42", "Q18--", "Q18-2", "Q184-", "Q1842",
};
static const char *const N_FURY_KBD[] = {"OFF", "HALF", "FULL"};

/* the 16 levels (Q15) of a DCO's staircase over one 16' cycle: footage k (0 16', 1 8', 2 4', 3 2') is a square at
 * 2^k x the 16' rate, high while bit (3 - k) of the sixteenth's index is 0; SAW weights 8 4 2 1 (16' loudest),
 * SQUARE 4 each; scaled so the loudest value peaks near half scale (the mix of two DCOs and the noise stays in range) */
static void fury_levels(int16_t *lv, uint32_t dco)
{
    uint32_t h = dco & 15u, sq = (dco >> 4) & 1u, s, k;
    int32_t total = 0, w[4];
    for (k = 0; k < 4u; k++) {
        w[k] = ((h >> (3u - k)) & 1u) ? (sq ? 4 : 8 >> k) : 0;
        total += w[k];
    }
    for (s = 0; s < 16u; s++) {
        int32_t a = 0;
        for (k = 0; k < 4u; k++)
            a += ((s >> (3u - k)) & 1u) ? -w[k] : w[k];
        lv[s] = (int16_t)(total ? a * 16000 / total : 0);
    }
}

static void fury_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = v->s[5] = v->s[6] = 0;           /* the VCF's two stages */
    if (!v->s[2])
        v->s[2] = 0x2E1D5A17 + (int32_t)v->age;          /* noise */
}

/* one DCO sample: the staircase level of sixteenth s of ph, band-limited at the sixteenth's edges */
static inline int32_t fury_dco(const int16_t *lv, uint32_t ph, uint32_t inc)
{
    uint32_t s = ph >> 28, p16 = ph << 4, i16 = inc << 4;
    int32_t d, y = lv[s];
    if (p16 < i16)
        d = lv[s] - lv[(s - 1u) & 15u];                   /* just stepped into s */
    else if (p16 > 0xFFFFFFFFu - i16)
        d = lv[(s + 1u) & 15u] - lv[s];                   /* about to step into s + 1 */
    else
        return y;
    return y + ((blep(p16, i16) * d) >> 16);
}

static void fury_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t dco1 = (uint32_t)clamp(p[P_E0], 0, 31), dco2 = (uint32_t)clamp(p[P_E1], 0, 32), i;
    int32_t intv = clamp(p[P_E2], -24, 24), dtun = clamp(p[P_E3], 0, 127), kbd = clamp(p[P_E6], 0, 2);
    int32_t res = clamp(p[P_E5], 0, 110), nz = clamp(p[P_E7], 0, 127) * 200;
    int32_t dp = v->pitch16 - 60 * 16, track = kbd == 2 ? dp * 19 : kbd == 1 ? (dp * 19) >> 1 : 0;
    int32_t cut = (p[P_E4] << 8) + m->cutoff + track;
    int16_t lv1[16], lv2[16];
    tsvf_t f1, f2;
    /* the 16' phase: an octave below the note; capped so the staircase's sixteenths stay below the sample rate */
    uint32_t inc1 = m->inc >> 1, inc2 = 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];
    int32_t a1 = v->s[0], a2 = v->s[1], nst = v->s[2], b1 = v->s[5], b2 = v->s[6];
    if (inc1 > 0x0FFFFFFFu)
        inc1 = 0x0FFFFFFFu;
    fury_levels(lv1, dco1);
    if (dco2) {
        fury_levels(lv2, dco2 - 1u);
        inc2 = cents_inc(clamp(m->pitch16 + intv * 16, 0, 2047), dtun / 2, m->fine) >> 1;
        if (inc2 > 0x0FFFFFFFu)
            inc2 = 0x0FFFFFFFu;
    }
    tsvf_coef(&f1, cut, res);                             /* 24 dB: two stages, the resonance mostly on the first */
    tsvf_coef(&f2, cut, res >> 2);
    for (i = 0; i < n; i++) {
        int32_t s = fury_dco(lv1, ph0, inc1), y;
        ph0 += inc1;
        if (dco2) {
            s += fury_dco(lv2, ph1, inc2);
            ph1 += inc2;
        }
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        y = tsvf_lp(&f1, s >> 1, &a1, &a2);
        y = tsvf_lp(&f2, y, &b1, &b2);
        s = soft_knee(y, 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[2] = nst;
    v->s[5] = b1;
    v->s[6] = b2;
}

/* presets in the Poly-800's spirit; e = {DCO1, DCO2, INTV, DTUN, CUT, RES, KBD, NOIS} */
static const preset_t FURY_PRESETS[] = {
    {"POLY BRASS", {FURY_DCO(0, 12), 0, 0, 0, 68, 20, 1, 0}, {22, 60, 100, 40}, 30, 0, FX(0, 20, 20, 40), PAT(6)},
    {"800 STRINGS", {FURY_DCO(0, 15), FURY_DCO2(0, 12), 0, 18, 58, 10, 1, 0}, {70, 90, 115, 85}, 10, 0, FX(0, 70, 20, 60), PAT(5)},
    {"SQR ORGAN", {FURY_DCO(1, 15), FURY_DCO2(1, 4), 7, 0, 110, 0, 2, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 30, 10, 30), PAT(6)},
    {"BELL 800", {FURY_DCO(1, 2), FURY_DCO2(1, 1), 19, 5, 80, 40, 2, 0}, {0, 95, 0, 90}, 40, 0, FX(0, 20, 40, 60), PAT(7)},
    {"DCO BASS", {FURY_DCO(0, 8), FURY_DCO2(1, 8), -12, 8, 45, 40, 0, 0}, {0, 55, 60, 25}, 45, 1, FX(0, 0, 10, 10), PAT(2)},
    {"POLY LEAD", {FURY_DCO(0, 4), FURY_DCO2(0, 4), 0, 25, 85, 30, 1, 0}, {2, 70, 100, 40}, 20, 1, FX(5, 10, 45, 30), PAT(4)},
    {"WHALE PAD", {FURY_DCO(0, 8), FURY_DCO2(1, 4), 0, 30, 50, 25, 1, 0}, {90, 90, 110, 100}, 35, 0, FX(0, 60, 30, 80), PAT(5)},
    {"HARPSI 800", {FURY_DCO(1, 12), FURY_DCO2(1, 3), 12, 3, 100, 20, 2, 0}, {0, 75, 0, 35}, 30, 0, FX(0, 10, 30, 30), PAT(3)},
    {"NOISE SWEEP", {FURY_DCO(0, 0), 0, 0, 0, 60, 60, 0, 127}, {60, 90, 100, 90}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"FUNK CLAV", {FURY_DCO(0, 7), 0, 0, 0, 70, 50, 2, 0}, {0, 60, 30, 20}, 55, 0, FX(0, 0, 20, 20), PAT(6)},
    {"CHOIR 800", {FURY_DCO(0, 10), FURY_DCO2(0, 12), 0, 12, 55, 15, 1, 5}, {60, 90, 120, 90}, 15, 0, FX(0, 80, 20, 70), PAT(5)},
    {"DOUBLE SAW", {FURY_DCO(0, 15), FURY_DCO2(0, 15), 0, 20, 90, 20, 1, 0}, {5, 70, 110, 50}, 15, 0, FX(0, 30, 30, 30), PAT(13)},
};

static const engine_t ENG_FURY = {
    .name = "FURY",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"DCO1", F_ENUM, 0, 31, FURY_DCO(0, 12), N_FURY_DCO, 0},
        {"DCO2", F_ENUM, 0, 32, 0, N_FURY_DCO2, 0},
        {"INTV", F_SEMI, -24, 24, 0, 0, 0},
        {"DTUN", F_INT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"KBD", F_ENUM, 0, 2, 1, N_FURY_KBD, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = FURY_PRESETS,
    .npresets = NELEM(FURY_PRESETS),
    .note_on = fury_note_on,
    .render = fury_render,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .keep = 0x63,                /* the VCF's stages (s[0..1], s[5..6]) carry over a retrigger */
};
