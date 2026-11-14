/* SPDX-License-Identifier: GPL-3.0-only */
/* CUMBIA: la canción generada (puerto del prototipo web). Una sola cumbia colombiana de Monterrey en menor:
 * intro de dos acordes -> acordeón -> espacio para voz -> acordeón ... -> final. Cada PLAY genera una canción
 * nueva (semilla) y la toca sobre las cuatro pistas:
 *   pista 1  acordeón (SAMPLE, set ACORD): melodía, remates
 *   pista 2  bajo (ANALOG SUB BASS)
 *   pista 3  percusión (DRUM, mapa GM: congas, campanas, güiro, timbales; luego, los samples reales)
 *   pista 4  acordeón (SAMPLE, set ACORD): acordes de la mano derecha
 * El reloj es el del secuenciador (BPM, bloques de audio); las notas se agendan con su tiempo en muestras
 * (micro-tiempo, duración) y suenan con trk_note_on / trk_note_off. Incluido por seq.c. */

#define CB_ACC 0u
#define CB_BAJO 1u
#define CB_PERC 2u
#define CB_ACORDES 3u
#define CB_TN 64                         /* acordeón en Sol: canción en Mi menor (el relativo, como el prototipo) */

/* ---------------------------------------------------------------- azar --- */
static uint32_t cb_seed_state;
static uint32_t cb_rnd(void)             /* mulberry32 (el del prototipo): la misma semilla, la misma canción */
{
    uint32_t t;
    cb_seed_state += 0x6D2B79F5u;
    t = cb_seed_state;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return t ^ (t >> 14);
}
static uint32_t cb_pick(uint32_t n) { return (uint32_t)(((uint64_t)cb_rnd() * n) >> 32); }
static int cb_chance(uint32_t pct) { return cb_pick(100u) < pct; }
static uint32_t cb_hum = 0x1234567u;     /* humanización (no forma parte de la canción) */
static int32_t cb_jit(int32_t span)      /* -span/2 .. span/2 */
{
    cb_hum = cb_hum * 1664525u + 1013904223u;
    return span ? (int32_t)((cb_hum >> 8) % (uint32_t)span) - span / 2 : 0;
}

/* ------------------------------------------------------------ armonía --- */
enum { CH_m, CH_M, CH_7 };
static const int8_t CB_CHORD[3][4] = {{0, 3, 7, -1}, {0, 4, 7, -1}, {0, 4, 7, 10}};
typedef struct { int8_t root; uint8_t type; } cb_ch_t;
typedef struct { uint8_t n; cb_ch_t c[4]; } cb_prog_t;
static const cb_prog_t CB_PROGS[6] = {
    {2, {{0, CH_m}, {7, CH_7}}},                              /* i-V7 */
    {2, {{0, CH_m}, {10, CH_M}}},                             /* i-VII */
    {4, {{0, CH_m}, {5, CH_m}, {7, CH_7}, {0, CH_m}}},        /* i-iv-V7-i */
    {4, {{0, CH_m}, {8, CH_M}, {10, CH_M}, {7, CH_7}}},       /* i-VI-VII-V7 */
    {4, {{0, CH_m}, {5, CH_m}, {0, CH_m}, {7, CH_7}}},        /* i-iv-i-V7 */
    {4, {{0, CH_m}, {10, CH_M}, {8, CH_M}, {7, CH_7}}},       /* i-VII-VI-V7 */
};
static const int8_t CB_NAT[7] = {0, 2, 3, 5, 7, 8, 10};

static uint32_t chord_has(cb_ch_t c, int32_t m)
{
    uint32_t pc = (uint32_t)((m % 12 + 12) % 12), k;
    for (k = 0; k < 4u && CB_CHORD[c.type][k] >= 0; k++)
        if ((uint32_t)((c.root + CB_CHORD[c.type][k]) % 12) == pc)
            return 1;
    return 0;
}
static uint32_t scale_has(cb_ch_t c, int32_t m)
{
    uint32_t pc = (uint32_t)((m % 12 + 12) % 12), k;
    for (k = 0; k < 7u; k++) {
        int32_t x = CB_NAT[k];
        if (c.type == CH_7 && c.root == 7 && x == 10)
            x = 11;                                            /* sobre V7, la sensible */
        if ((uint32_t)x == pc)
            return 1;
    }
    return 0;
}
/* lista de notas (relativas a la tónica) de un acorde o de la escala, entre lo y hi */
static uint32_t tones(cb_ch_t c, int32_t lo, int32_t hi, int scale, int8_t *out)
{
    uint32_t n = 0;
    int32_t m;
    for (m = lo; m <= hi; m++)
        if (scale ? scale_has(c, m) : chord_has(c, m))
            out[n++] = (int8_t)m;
    return n;
}
static int32_t nearest(int32_t p, const int8_t *l, uint32_t n)
{
    int32_t best = l[0];
    uint32_t k;
    for (k = 1; k < n; k++) {
        int32_t d = l[k] - p, db = best - p;
        d = d < 0 ? -d : d;
        db = db < 0 ? -db : db;
        if (d < db || (d == db && l[k] > best))
            best = l[k];
    }
    return best;
}
static uint32_t index_of(int32_t p, const int8_t *l, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++)
        if (l[k] == p)
            return k;
    return 0;
}

/* ------------------------------------------------------------ canción --- */
enum { R_ACORDES, R_MELODIA, R_CALLADO, R_FINAL };
enum { PC_FULL, PC_LIGERA, PC_GUIRO, PC_CAMPANA, PC_NADA };
enum { G_SOST, G_ANTIC, G_TRES, G_ESPERA };
enum { E_NADA, E_REMATE, E_TIMBAL };
typedef struct { uint16_t st; int8_t p; uint8_t len; } cb_ev_t;      /* paso (16avos desde la sección), nota, pasos */
typedef struct {
    uint8_t role, bars, perc, bajo, gesto, mano, entrada, comp, cut;
    cb_ch_t ch[16];
    uint16_t e0, ne;                     /* melodía / remates en cb_ev */
    uint16_t s0, ns;                     /* golpes de acorde de los remates en cb_ev (p = duración) */
} cb_sec_t;
#define CB_NSEC 8
#define CB_NEV 900
static struct {
    uint32_t seed;
    uint8_t bpm, bass, nsec;
    cb_sec_t sec[CB_NSEC];
    uint16_t start[CB_NSEC + 1];         /* paso de inicio de cada sección; start[nsec] = largo */
} cb;
static cb_ev_t cb_ev[CB_NEV];
static uint32_t cb_nev;

static void ev_put(uint32_t st, int32_t p, uint32_t len)
{
    if (cb_nev < CB_NEV)
        cb_ev[cb_nev++] = (cb_ev_t){(uint16_t)st, (int8_t)p, (uint8_t)len};
}

static const uint8_t CB_CELLS[6][8][2] = {     /* ritmos de medio compás: {paso, duración}, {0,0} = fin */
    {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 2}, {6, 2}},
    {{0, 2}, {2, 1}, {3, 1}, {4, 4}},
    {{0, 3}, {3, 1}, {4, 2}, {6, 2}},
    {{0, 1}, {1, 1}, {2, 2}, {4, 4}},
    {{0, 2}, {2, 2}, {4, 2}, {6, 2}},
    {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 2}},
};
static const uint8_t CB_LONGS[3][2][2] = {{{0, 8}}, {{0, 6}, {6, 2}}, {{0, 2}, {2, 6}}};

/* frase de 2 compases: arpegios por tonos del acorde, notas de paso en semicorcheas débiles, cierre largo */
static void gen_phrase(const cb_sec_t *s, uint32_t start_bar, int32_t center)
{
    int8_t ct[24], sc[24];
    uint32_t nct = tones(s->ch[start_bar], -1, 19, 0, ct), h, first = 1;
    int32_t p = nearest(center, ct, nct), dir = cb_chance(50) ? 1 : -1;
    for (h = 0; h < 4u; h++) {
        uint32_t bar = start_bar + (h >> 1), k, nsc;
        const uint8_t(*cell)[2] = h == 3u ? CB_LONGS[cb_pick(3)] : CB_CELLS[cb_pick(6)];
        uint32_t ncell = h == 3u ? 2u : 8u;
        nct = tones(s->ch[bar], -1, 19, 0, ct);
        for (k = 0; k < ncell && cell[k][1]; k++) {
            uint32_t st = cell[k][0], len = cell[k][1];
            if (!first) {
                if (st % 4u == 0 || len >= 2u || cb_chance(60)) {
                    int32_t i = (int32_t)index_of(nearest(p, ct, nct), ct, nct), j = i + dir * (cb_chance(70) ? 1 : 2);
                    if (j < 0 || j >= (int32_t)nct || cb_chance(20)) {
                        dir = -dir;
                        j = i + dir;
                    }
                    j = j < 0 ? 0 : j >= (int32_t)nct ? (int32_t)nct - 1 : j;
                    p = ct[j];
                } else {
                    int32_t i;
                    nsc = tones(s->ch[bar], -1, 19, 1, sc);
                    i = (int32_t)index_of(nearest(p, sc, nsc), sc, nsc) + dir;
                    i = i < 0 ? 0 : i >= (int32_t)nsc ? (int32_t)nsc - 1 : i;
                    p = sc[i];
                }
            }
            first = 0;
            if (len >= 4u)
                p = nearest(p, ct, nct);
            ev_put(bar * 16u + (h % 2u) * 8u + st, p, len);
        }
    }
}
/* respuesta en registro medio: dos notas a una tercera, figura larga-corta */
static void gen_tercera(const cb_sec_t *s, uint32_t start_bar)
{
    static const uint8_t F[15][3] = {{0, 0, 2}, {2, 1, 1}, {3, 0, 2}, {5, 1, 1}, {6, 0, 2}, {8, 1, 1}, {9, 0, 2},
        {11, 1, 1}, {12, 1, 4}, {16, 2, 2}, {18, 3, 1}, {19, 2, 2}, {21, 3, 1}, {22, 2, 2}, {24, 3, 8}};
    int8_t ct[24], v[4];
    uint32_t k, n;
    for (k = 0; k < 2u; k++) {
        int32_t a;
        uint32_t i;
        n = tones(s->ch[start_bar + k], 2, 14, 0, ct);
        a = nearest(3 + (int32_t)cb_pick(5), ct, n);
        i = index_of(a, ct, n);
        v[2 * k] = (int8_t)a;
        v[2 * k + 1] = i + 1u < n ? ct[i + 1] : (int8_t)a;
    }
    for (k = 0; k < 15u; k++)
        ev_put(start_bar * 16u + F[k][0], v[F[k][1]], F[k][2]);
}
/* la misma frase sobre otros acordes: conserva ritmo y contorno */
static void map_phrase(const cb_sec_t *s, uint32_t a0, uint32_t a1, uint32_t from_bar, uint32_t to_bar)
{
    int8_t l[24];
    uint32_t k;
    for (k = a0; k < a1; k++) {
        cb_ev_t e = cb_ev[k];
        uint32_t bar = (e.st >> 4) - from_bar + to_bar, n;
        int scale = !(e.st % 4u == 0 || e.len >= 2u);
        n = tones(s->ch[bar], -1, 19, scale, l);
        ev_put(e.st + (to_bar - from_bar) * 16u, nearest(e.p, l, n), e.len);
    }
}
static void gen_melody(cb_sec_t *s)
{
    uint32_t base;
    s->e0 = (uint16_t)cb_nev;
    for (base = 0; base < s->bars; base += 8u) {
        uint32_t a0 = cb_nev, a1;
        gen_phrase(s, base, base ? (cb_chance(50) ? 12 : 15) : (cb_chance(50) ? 7 : 12));
        a1 = cb_nev;
        map_phrase(s, a0, a1, base, base + 2u);
        if (cb_chance(45))
            gen_tercera(s, base + 4u);
        else
            gen_phrase(s, base + 4u, cb_chance(50) ? 3 : 7);
        map_phrase(s, a0, a1, base, base + 6u);
    }
    s->ne = (uint16_t)(cb_nev - s->e0);
}
/* remates: el acordeón calla y llama al final de cada 4 compases */
static void gen_remates(cb_sec_t *s)
{
    uint32_t base, stabs[8], ns = 0, k;
    s->e0 = (uint16_t)cb_nev;
    for (base = 0; base < s->bars; base += 4u) {
        uint32_t bar = base + 3u, o = bar * 16u, tipo = cb_pick(3), n, i;
        int8_t ct[24];
        n = tones(s->ch[bar], 2, 19, 0, ct);
        if (tipo == 0) {
            i = index_of(nearest(5, ct, n), ct, n);
            for (k = 0; k < 4u; k++)
                ev_put(o + 8u + k, ct[i + k < n ? i + k : n - 1u], 1);
            ev_put(o + 12u, ct[i + 4u < n ? i + 4u : n - 1u], 4);
        } else if (tipo == 1) {
            int32_t a = nearest(7, ct, n);
            uint32_t j = index_of(a, ct, n);
            int32_t b = j + 1u < n ? ct[j + 1] : a;
            ev_put(o + 8u, a, 2);
            ev_put(o + 10u, b, 1);
            ev_put(o + 11u, a, 1);
            ev_put(o + 12u, b, 4);
        } else if (ns < 7u) {
            stabs[ns++] = o + 8u;
            stabs[ns++] = o + 12u;
        }
    }
    s->ne = (uint16_t)(cb_nev - s->e0);
    s->s0 = (uint16_t)cb_nev;
    for (k = 0; k < ns; k++)
        ev_put(stabs[k], (stabs[k] % 16u) == 8u ? 3 : 4, 0);   /* p = duración del golpe */
    s->ns = (uint16_t)(cb_nev - s->s0);
}

static void chords_for(cb_sec_t *s, const cb_prog_t *pr, uint32_t pat)
{
    uint32_t i;
    for (i = 0; i < s->bars && i < 16u; i++)
        s->ch[i] = pr->n == 2u ? pr->c[(pat >> (3u - i % 4u)) & 1u] : pr->c[i % 4u];
}
static cb_sec_t *sec_add(uint32_t role, uint32_t bars, const cb_prog_t *pr, uint32_t pat)
{
    cb_sec_t *s = &cb.sec[cb.nsec++];
    memset(s, 0, sizeof *s);
    s->role = (uint8_t)role;
    s->bars = (uint8_t)bars;
    chords_for(s, pr, pat);
    return s;
}

static void cb_generate(uint32_t seed)
{
    static const uint8_t PATS[3] = {0x3, 0x6, 0x5};          /* '0011' '0110' '0101' */
    const cb_prog_t *pa, *pb;
    uint32_t pat, cycles, c, i, acc = 0;
    cb_sec_t *s;
    cb_seed_state = seed;
    memset(&cb, 0, sizeof cb);
    cb_nev = 0;
    cb.seed = seed;
    (void)cb_pick(12);                                        /* (la tónica del prototipo: aquí la fija el acordeón) */
    cb.bpm = (uint8_t)(74u + cb_pick(15));
    cb.bass = (uint8_t)!cb_chance(70);                        /* 0 tumbao, 1 dub */
    pa = &CB_PROGS[cb_pick(6)];
    pb = cb_chance(50) ? pa : &CB_PROGS[cb_pick(6)];
    pat = PATS[cb_pick(3)];
    s = sec_add(R_ACORDES, cb_chance(50) ? 2 : 4, pa, pat);
    {
        static const uint8_t P4[4] = {PC_GUIRO, PC_GUIRO, PC_CAMPANA, PC_NADA};
        s->perc = P4[cb_pick(4)];
    }
    s->gesto = (uint8_t)cb_pick(4);
    s->mano = (uint8_t)cb_chance(60);
    s->entrada = (uint8_t)cb_pick(3);
    cycles = cb_chance(50) ? 2u : 3u;
    for (c = 0; c < cycles; c++) {
        static const uint8_t B3[3] = {8, 8, 16};
        s = sec_add(R_MELODIA, B3[cb_pick(3)], c % 2u ? pb : pa, pat);
        s->bajo = 1;
        if (c + 1u < cycles) {
            static const uint8_t C3[3] = {4, 8, 8};
            uint32_t comp = cb_pick(3) == 2u;                 /* 'remates' 'remates' 'comp' */
            s = sec_add(R_CALLADO, C3[cb_pick(3)], pa, pat);
            s->comp = (uint8_t)comp;
            s->perc = cb_chance(30) ? PC_LIGERA : PC_FULL;
            s->bajo = 1;
            s->cut = (uint8_t)cb_chance(50);
        }
    }
    s = sec_add(R_FINAL, cb_chance(50) ? 2 : 4, pa, pat);
    s->bajo = 1;
    s->ch[s->bars - 1u] = (cb_ch_t){0, CH_m};                 /* se cierra en la tónica */
    for (i = 0; i < cb.nsec; i++) {
        s = &cb.sec[i];
        if (s->role == R_MELODIA)
            gen_melody(s);
        else if (s->role == R_CALLADO && !s->comp)
            gen_remates(s);
        cb.start[i] = (uint16_t)acc;
        acc += s->bars * 16u;
    }
    cb.start[cb.nsec] = (uint16_t)acc;
}

/* ----------------------------------------------------- percusión (GM) --- */
#include "cumbia_groove.h"                                    /* CB_GROOVE[]: la cumbia lenta, 8 compases (tools/gen_cumbia.py) */
static uint32_t inst_ok(uint32_t inst, uint32_t perc)
{
    switch (perc) {
    case PC_NADA: return 0;
    case PC_GUIRO: return inst == GI_GUIRO_L || inst == GI_GUIRO_S;
    case PC_CAMPANA: return inst == GI_GUIRO_L || inst == GI_GUIRO_S || inst == GI_COWBELL_B || inst == GI_COWBELL_S;
    case PC_LIGERA: return inst == GI_GUIRO_L || inst == GI_GUIRO_S || inst == GI_CONGA1 || inst == GI_CONGA2 ||
                           inst == GI_COWBELL_S;
    default: return 1;
    }
}

/* ------------------------------------------------------------ agenda --- */
typedef struct { int32_t at; uint8_t tr, note, vel, gen; } cb_q_t;   /* at: muestras que faltan; vel 0 = soltar */
#define CB_NQ 96
static cb_q_t cb_q[CB_NQ];
static uint32_t cb_nq;
static uint8_t cb_gen[NTRK][128];        /* la nota k de la pista: su nota-on más reciente (un off viejo no la corta) */
static uint8_t cb_on[NTRK][16];          /* bit: suena por nosotros */
static uint8_t cb_play, cb_end;
static uint32_t cb_step, cb_pos, cb_len16;
static uint32_t cb_next_seed = 1;

static void q_put(int32_t at, uint32_t tr, int32_t note, uint32_t vel)
{
    if (note < 0 || note > 127 || cb_nq >= CB_NQ)
        return;
    cb_q[cb_nq++] = (cb_q_t){at < 0 ? 0 : at, (uint8_t)tr, (uint8_t)note, (uint8_t)vel,
                             vel ? 0u : cb_gen[tr][note]};
}
/* una nota: encendida en at (muestras desde ahora), suelta dur muestras después */
static void note(uint32_t tr, int32_t m, int32_t at, int32_t dur, int32_t vel)
{
    vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
    if (m < 0 || m > 127)
        return;
    q_put(at, tr, m, (uint32_t)vel);
    q_put(at + (dur > 64 ? dur : 64), tr, m, 0);
}
static void q_fire(cb_q_t *e)
{
    track_t *t = &trk[e->tr];
    if (e->vel) {
        cb_gen[e->tr][e->note]++;
        cb_on[e->tr][e->note >> 3] |= (uint8_t)(1u << (e->note & 7u));
        trk_note_on(t, e->note, e->vel);
    } else if (e->gen + 1u == cb_gen[e->tr][e->note]) {      /* el off de la nota que suena (no de una anterior) */
        cb_on[e->tr][e->note >> 3] &= (uint8_t)~(1u << (e->note & 7u));
        trk_note_off(t, e->note);
    }
}
static void cb_all_off(void)
{
    uint32_t tr, k;
    for (tr = 0; tr < NTRK; tr++)
        for (k = 0; k < 128u; k++)
            if (cb_on[tr][k >> 3] & (1u << (k & 7u)))
                trk_note_off(&trk[tr], k);
    memset(cb_on, 0, sizeof cb_on);
    cb_nq = 0;
}

/* --------------------------------------------------- tocar un paso --- */
static int32_t vel_of(int32_t q8) { return q8 * 100 / 256; }      /* 1.0 del prototipo = 100 */

static void acc_note(int32_t m, int32_t at, uint32_t len, uint32_t local)
{
    uint32_t b = local % 16u, sd = cb_len16;
    int32_t v = 256;
    if (b % 4u == 0)
        v += 28;                                              /* tiempo fuerte */
    else if (b % 2u == 1u)
        v -= 34;                                              /* semicorchea de paso */
    if (len >= 3u)
        v += 18;
    v += cb_jit(36);
    at += cb_jit((int32_t)FS / 90) + (b % 4u == 2u ? (int32_t)FS / 200 : 0);
    note(CB_ACC, CB_TN + m, at, (int32_t)(len * sd * 9u / 10u + len * sd / 10u), vel_of(v));
}
static void acc_chord(cb_ch_t c, int32_t at, uint32_t len, int stac, int32_t v, int bajo)
{
    uint32_t sd = cb_len16, k;
    int32_t dur = (int32_t)(len * sd) * (stac ? 55 : 100) / 100 + (int32_t)FS / 50;
    for (k = 0; k < 4u && CB_CHORD[c.type][k] >= 0; k++) {
        int32_t m = CB_TN + (c.root + CB_CHORD[c.type][k]) % 12;
        if (m < CB_TN + 2)
            m += 12;                                          /* entre la 2a y la 14a sobre la tónica */
        note(CB_ACORDES, m, at + (int32_t)k * (int32_t)FS / 166, dur, vel_of(v * 62 / 100));
    }
    if (bajo) {
        int32_t d4 = (int32_t)(4u * sd);
        note(CB_BAJO, CB_TN - 24 + c.root, at, dur < d4 ? dur : d4, 90);
    }
}
static const uint8_t CB_TRESILLO[3][2] = {{0, 4}, {6, 4}, {12, 3}};
static const uint8_t CB_GESTO[4][3][3] = {   /* {paso, duración, staccato} */
    {{0, 16, 0}},
    {{0, 10, 0}, {10, 6, 0}},
    {{0, 4, 1}, {6, 4, 1}, {12, 3, 1}},
    {{0, 3, 1}, {14, 2, 1}},
};
static const int8_t CB_BAJOS[2][4][3] = {   /* {paso, intervalo, duración}: tumbao, dub */
    {{0, 0, 3}, {6, 0, 1}, {8, -5, 3}, {14, -5, 1}},
    {{0, 0, 7}, {8, 0, 1}, {10, 7, 3}, {14, -5, 2}},
};

static void cb_play_step(uint32_t g)
{
    uint32_t i = 0, local, bar, b, k, sd = cb_len16, last;
    const cb_sec_t *s;
    cb_ch_t ch;
    while (i + 1u < cb.nsec && cb.start[i + 1u] <= g)
        i++;
    s = &cb.sec[i];
    local = g - cb.start[i];
    bar = local >> 4;
    b = local % 16u;
    ch = s->ch[bar < 16u ? bar : 15u];
    last = bar + 1u == s->bars;
    /* acordeón */
    if (s->role == R_ACORDES) {
        for (k = 0; k < 3u && CB_GESTO[s->gesto][k][1]; k++)
            if (CB_GESTO[s->gesto][k][0] == b && !(last && s->entrada == E_REMATE && b >= 8u))
                acc_chord(ch, CB_GESTO[s->gesto][k][2] ? cb_jit((int32_t)FS / 80) : 0, CB_GESTO[s->gesto][k][1],
                          CB_GESTO[s->gesto][k][2], 256 + 26 * (int32_t)(bar % 2u), s->mano && b == 0);
        if (last && s->entrada == E_REMATE && b >= 8u && b <= 12u) {
            int8_t ct[24];
            uint32_t n = tones(ch, 2, 19, 0, ct), j = index_of(nearest(5, ct, n), ct, n) + (b - 8u);
            acc_note(ct[j < n ? j : n - 1u], 0, b == 12u ? 4u : 1u, local);
        }
        if (last && s->entrada == E_TIMBAL && b >= 12u)
            note(CB_PERC, b % 2u ? 66 : 65, 0, (int32_t)sd, 45 + 12 * (int32_t)(b - 12u));
    } else if (s->role == R_CALLADO) {
        if (s->comp) {
            for (k = 0; k < 3u; k++)
                if (CB_TRESILLO[k][0] == b)
                    acc_chord(ch, cb_jit((int32_t)FS / 80), CB_TRESILLO[k][1], 1, 180, 0);
        } else {
            for (k = s->e0; k < s->e0 + s->ne; k++)
                if (cb_ev[k].st == local)
                    acc_note(cb_ev[k].p, 0, cb_ev[k].len, local);
            for (k = s->s0; k < s->s0 + s->ns; k++)
                if (cb_ev[k].st == local)
                    acc_chord(ch, 0, (uint32_t)cb_ev[k].p, cb_ev[k].p < 4, 230, 0);
        }
    } else if (s->role == R_MELODIA) {
        for (k = s->e0; k < s->e0 + s->ne; k++)
            if (cb_ev[k].st == local)
                acc_note(cb_ev[k].p, 0, cb_ev[k].len, local);
    } else {
        if (!last) {
            for (k = 0; k < 3u; k++)
                if (CB_TRESILLO[k][0] == b)
                    acc_chord(ch, cb_jit((int32_t)FS / 80), CB_TRESILLO[k][1], 1, 256, 0);
        } else if (b == 0) {
            acc_chord((cb_ch_t){0, CH_m}, 0, 16, 0, 300, 1);
            note(CB_PERC, 49, 0, (int32_t)(4u * sd), 70);
            note(CB_PERC, 66, 0, (int32_t)sd, 80);
        }
    }
    /* bajo */
    if (s->bajo && !(s->cut && last) && !(s->role == R_FINAL && last))
        for (k = 0; k < 4u; k++)
            if ((uint32_t)CB_BAJOS[cb.bass][k][0] == b)
                note(CB_BAJO, CB_TN - 24 + ch.root + CB_BAJOS[cb.bass][k][1], cb_jit((int32_t)FS / 160),
                     (int32_t)((uint32_t)CB_BAJOS[cb.bass][k][2] * sd * 88u / 100u), b == 0 ? 96 : 88);
    /* percusión: el groove real de la cumbia lenta, con su micro-tiempo */
    if (!(s->role == R_FINAL && last) && s->perc != PC_NADA) {
        uint32_t gs = g % CB_GROOVE_STEPS;
        for (k = CB_GROOVE_IDX[gs]; k < CB_GROOVE_IDX[gs + 1u]; k++) {
            const cb_hit_t *h = &CB_GROOVE[k];
            if (inst_ok(h->inst, s->perc))
                note(CB_PERC, GI_NOTE[h->inst], (int32_t)((uint32_t)h->frac * sd / 256u), (int32_t)sd, h->vel);
        }
    }
}

/* -------------------------------------------------------- transporte --- */
static void set_engine_of(track_t *t, uint32_t ei);         /* ui.c */
static void apply_preset_to(track_t *t, uint32_t pi);
static void track_defaults_steps(track_t *t);
static void cumbia_tracks(void)          /* las cuatro pistas con los sonidos de la cumbia (aunque el proyecto guardado traiga
                                          * otros): main.c al encender, después de restaurar la última sesión (contexto de la UI) */
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        track_t *t = &trk[i];
        if (t->eng_req != TRK_DEF[i][0] || t->preset != TRK_DEF[i][1]) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
        }
        t->p[P_MUTE] = 0;
        track_defaults_steps(t);          /* sin patrones guardados: suena solo la canción */
    }
}
static void cumbia_start(void)
{
    cb_all_off();
    cb_generate(cb_next_seed);
    cb_next_seed = cb_next_seed * 1103515245u + 12345u + (uint32_t)fm1_ms;   /* la siguiente: otra */
    song.g[G_BPM] = cb.bpm;
    cb_step = 0;
    cb_pos = 0;
    cb_play = 1;
    cb_end = 0;
}
static void cumbia_stop(void)
{
    cb_play = 0;
    cb_all_off();
}
static void cumbia_tick(uint32_t n)
{
    uint32_t i, k;
    if (!cb_play || !song.playing)
        return;
    cb_len16 = beat_samples() / 4u;
    if (cb_step == 0) {                                       /* PLAY: el paso 0 ahora */
        cb_play_step(0);
        cb_step = 1;
        cb_pos = 0;
    }
    cb_pos += n;
    while (!cb_end && cb_pos >= cb_len16) {                   /* los pasos que empiezan en este bloque */
        cb_pos -= cb_len16;
        if (cb_step >= cb.start[cb.nsec])
            cb_end = 1;                                       /* fin: deja sonar el último acorde */
        else
            cb_play_step(cb_step++);
    }
    for (i = 0, k = 0; i < cb_nq; i++) {                      /* lo agendado que toca en este bloque */
        if (cb_q[i].at < (int32_t)n)
            q_fire(&cb_q[i]);
        else {
            cb_q[i].at -= (int32_t)n;
            cb_q[k++] = cb_q[i];
        }
    }
    cb_nq = k;
    if (cb_end && !cb_nq)
        cb_play = 0;
}
