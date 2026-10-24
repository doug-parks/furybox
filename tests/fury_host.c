/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Doug Parks (FURYBOX), on Felucca by Leo Kuroshita, Hügelton Instruments */
/* FURYBOX: a standalone host of the FURY engine (src/eng_fury.c), after Felucca-Engines' tests/fe_host.c. Renders
 * every preset through fe_host's phrase on a minimal track / voice / envelope and prints one line per render (an
 * FNV-1a 64 hash and the peak), and every DCO value on the defaults; with a file name, writes a 16-bit mono WAV of
 * the presets played in turn (to listen to on a computer).
 *   cc -O2 -I firmware/engines/build -o fury_host tests/fury_host.c   (fe_tables.h: tools/gen_engine_tables.py)
 *   fury_host [out.wav] */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "../firmware/engines/src/felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
} track_t;
static track_t trk[NPART];

#include "../firmware/engines/src/dsp.c"
#include "../firmware/src/eng_fury.c"

static uint32_t vage;

static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note, int32_t fine)
{
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s[8];
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = 100;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = fine;
    if (!sounding)
        v->env = v->env_out = 0;
    e->note_on(t, v);
    if (sounding) {
        uint32_t k;
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

/* a plain envelope, once per block: attack in 4 blocks, sustain 3/4, release by 1/16 a block */
static int32_t env_tick(voice_t *v)
{
    if (!v->active)
        return 0;
    switch (v->stage) {
    case 1:
        v->env += 1 << 22;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env -= (v->env - (3 << 22)) >> 3;
        break;
    case 3:
        v->env -= v->env >> 4;
        if (v->env < (1 << 24) / 1000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;
}

#define NBLK 240
typedef struct { uint64_t h; int32_t peak; } result_t;
static FILE *wav;
static uint32_t wav_frames;

static void wav_put(const int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = clamp(out[i], -32768, 32767);
        fputc(x & 255, wav);
        fputc((x >> 8) & 255, wav);
        wav_frames++;
    }
}

static result_t render(const engine_t *e, const preset_t *pr, uint32_t part, int wavout)
{
    track_t *t = &trk[part];
    int32_t out[CTL];
    int32_t last[NVOICE] = {0};
    uint32_t b, i, k;
    result_t r = {14695981039346656037ull, 0};
    memset(t, 0, sizeof *t);
    vage = 0;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    t->p[P_ATK] = pr->env[0];
    t->p[P_REL] = pr->env[3];
    for (b = 0; b < NBLK; b++) {
        if (b == 0)
            voice_on(t, e, &t->v[0], 43, 0);
        if (b == 30)
            voice_on(t, e, &t->v[1], 62, 23);
        if (b == 70)
            voice_on(t, e, &t->v[0], 50, 0);
        if (b == 100)
            voice_on(t, e, &t->v[2], 79, -17);
        if (b == 150)
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].active) {
                    t->v[k].gate = 0;
                    t->v[k].stage = 3;
                }
        if (e->block)
            e->block(t);
        memset(out, 0, sizeof out);
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k];
            vmod_t m;
            int32_t a;
            uint32_t base;
            if (!v->active)
                continue;
            a = env_tick(v);
            if (e->amp)
                a = e->amp(t, v, a);
            if (!v->active)
                continue;
            base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
            memset(&m, 0, sizeof m);
            m.pitch16 = v->pitch16;
            m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
            m.fine = v->fine;
            m.amp0 = last[k];
            m.amp1 = a;
            m.envq15 = a;
            m.cutoff = ((int32_t)(b % 96u) - 48) << 7;
            m.shape = (64 << 8) + (((int32_t)(b % 64u) - 32) << 8);
            last[k] = a;
            e->render(t, v, out, CTL, &m);
        }
        if (wavout && wav)
            wav_put(out, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t x = (uint32_t)out[i];
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            for (k = 0; k < 4u; k++) {
                r.h ^= (x >> (8u * k)) & 0xFFu;
                r.h *= 1099511628211ull;
            }
            if (a > r.peak)
                r.peak = a;
        }
    }
    return r;
}

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 2u, riff = 36u + data, v;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    v = riff; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    v = 1 | (1u << 16); fwrite(&v, 4, 1, f);             /* PCM, mono */
    v = FS; fwrite(&v, 4, 1, f);
    v = FS * 2u; fwrite(&v, 4, 1, f);
    v = 2 | (16u << 16); fwrite(&v, 4, 1, f);            /* block align 2, 16 bits */
    fwrite("data", 1, 4, f);
    v = data; fwrite(&v, 4, 1, f);
}

int main(int argc, char **argv)
{
    const engine_t *e = &ENG_FURY;
    uint32_t j;
    int32_t v;
    if (argc > 1) {
        wav = fopen(argv[1], "wb");
        if (wav)
            wav_header(wav, 0);
    }
    for (j = 0; j < e->npresets; j++) {
        result_t r = render(e, &e->presets[j], j % NPART, 1);
        printf("fury/%u %016llx %d %s\n", j, (unsigned long long)r.h, r.peak, e->presets[j].name);
    }
    {   /* every DCO1 value on the defaults, and every DCO2 value with DCO1 at its default */
        preset_t pr;
        memset(&pr, 0, sizeof pr);
        for (j = 0; j < 8u; j++)
            pr.e[j] = (int8_t)e->edit[j].def;
        pr.env[3] = 40;
        for (v = e->edit[0].min; v <= e->edit[0].max; v++) {
            result_t r;
            pr.e[0] = (int8_t)v;
            r = render(e, &pr, (uint32_t)v % NPART, 0);
            printf("fury/e0=%d %016llx %d\n", v, (unsigned long long)r.h, r.peak);
        }
        pr.e[0] = (int8_t)e->edit[0].def;
        for (v = e->edit[1].min; v <= e->edit[1].max; v++) {
            result_t r;
            pr.e[1] = (int8_t)v;
            r = render(e, &pr, (uint32_t)v % NPART, 0);
            printf("fury/e1=%d %016llx %d\n", v, (unsigned long long)r.h, r.peak);
        }
    }
    if (wav) {
        wav_header(wav, wav_frames);
        fclose(wav);
        fprintf(stderr, "%s: %u frames\n", argv[1], wav_frames);
    }
    return 0;
}
