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

#define CB_ACC 0u                        /* acordeón: melodía, remates y acordes (SAMPLE, set ACORD) */
#define CB_BAJO 1u
#define CB_PERC 2u
#define CB_VOZ 3u                        /* gritos (SAMPLE, USR2) con el eco de sonidero */
#define CB_ACORDES CB_ACC

/* ---------------------------------------------------- controles (cumbia_ui.c) --- */
static const uint8_t CB_KEYS[6] = {67, 72, 65, 74, 69, 70};   /* tono del acordeón: Sol Do Fa Re La Si bemol */
static const char *const CB_KEY_NAMES[6] = {"SOL", "DO", "FA", "RE", "LA", "SIb"};
static struct {
    uint8_t key;                         /* 0..5: CB_KEYS */
    int8_t bpm;                          /* -20..+20 sobre el tempo de la canción */
    uint8_t reb;                         /* rebajada 0..35 (% menos de velocidad) */
    uint8_t rev;                         /* reverb 0..20 */
    uint8_t eco, bb;                     /* eco de sonidero, boom bap */
    volatile uint8_t cinta;              /* 1 mientras se mantiene la cinta para atrás */
} cbc = {0, 0, 0, 6, 1, 0, 0};
static int32_t cb_tn = 64;               /* la tónica de la canción: el relativo menor del tono del acordeón */

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
#define CB_KIT_KEY0 48u          /* el kit en USR1 (tools/gen_kit.py): instrumento i en la tecla CB_KIT_KEY0 + i */
static uint8_t cb_kit;           /* 1 = la pista de percusión toca el kit de USR1; 0 = DRUM (notas GM) */
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
/* el kit de USR1 suena hasta el final de cada golpe: para no quitarle voces al acordeón ni a los acordes, la
 * percusión usa a lo más CB_PERC_MAX voces y, con todas ocupadas, cede la suya más vieja (se apaga en un bloque) */
#define CB_PERC_MAX 4u
static void cb_perc_room(void)
{
    track_t *t = &trk[CB_PERC];
    uint32_t i, n = 0, old = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].stage != 4u) {
            n++;
            if (old == NVOICE || t->v[i].age < t->v[old].age)
                old = i;
        }
    if (old != NVOICE && (n >= CB_PERC_MAX || voices_busy() >= NVOICE))
        voice_kill(&t->v[old]);
}
static volatile uint8_t cb_acc_hit;
static void q_fire(cb_q_t *e)
{
    track_t *t = &trk[e->tr];
    if (e->vel) {
        if (e->tr == CB_PERC && cb_kit)
            cb_perc_room();
        if (e->tr == CB_ACC)
            cb_acc_hit = cb_acc_hit ? 2u : 1u;                /* (cumbia_ui.c: varias a la vez, un acorde) */
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
    note(CB_ACC, cb_tn + m, at, (int32_t)(len * sd * 9u / 10u + len * sd / 10u), vel_of(v));
}
static void acc_chord(cb_ch_t c, int32_t at, uint32_t len, int stac, int32_t v, int bajo)
{
    uint32_t sd = cb_len16, k;
    int32_t dur = (int32_t)(len * sd) * (stac ? 55 : 100) / 100 + (int32_t)FS / 50;
    for (k = 0; k < 4u && CB_CHORD[c.type][k] >= 0; k++) {
        int32_t m = cb_tn + (c.root + CB_CHORD[c.type][k]) % 12;
        if (m < cb_tn + 2)
            m += 12;                                          /* entre la 2a y la 14a sobre la tónica */
        note(CB_ACORDES, m, at + (int32_t)k * (int32_t)FS / 166, dur, vel_of(v * 72 / 100));   /* (la misma pista que la melodía: un poco abajo) */
    }
    if (bajo) {
        int32_t d4 = (int32_t)(4u * sd);
        note(CB_BAJO, cb_tn - 24 + c.root, at, dur < d4 ? dur : d4, 90);
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

/* un golpe del kit (USR1: CB_KIT_KEY0 + i), o su nota GM en DRUM si el kit no está */
#define GI_KICK 14u              /* boom bap (tools/gen_kit.py): bombo y tarola; el hat es la güira corta */
#define GI_SNARE 15u
#define GI_HAT 6u
#define GI_TIMBAL_H 11u
#define GI_TIMBAL_L 12u
#define GI_CRASH 13u
static const uint8_t GI_GM_BB[2] = {36, 38};
static volatile uint32_t cb_hits;        /* bit i: el instrumento i sonó (cumbia_ui.c: las tarjetas) */
static void perc_hit(uint32_t inst, int32_t at, int32_t vel)
{
    uint32_t m = cb_kit ? CB_KIT_KEY0 + inst : inst >= GI_KICK ? GI_GM_BB[inst - GI_KICK] : GI_NOTE[inst];
    note(CB_PERC, (int32_t)m, at, (int32_t)(cb_kit ? cb_len16 / 4u : cb_len16), vel);
    cb_hits |= 1u << inst;
}
static const uint8_t CB_BB_K[2][4][2] = {{{0, 120}, {7, 96}, {10, 108}}, {{0, 120}, {3, 72}, {10, 108}, {11, 60}}};
static const uint8_t CB_BB_S[4][2] = {{4, 112}, {12, 112}, {9, 20}, {15, 25}};   /* 2 y 4, y los fantasmas */

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
            perc_hit(b % 2u ? GI_TIMBAL_L : GI_TIMBAL_H, 0, 30 + 8 * (int32_t)(b - 12u));
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
            perc_hit(GI_CRASH, 0, 47);
            perc_hit(GI_TIMBAL_L, 0, 53);
        }
    }
    /* bajo */
    if (s->bajo && !(s->cut && last) && !(s->role == R_FINAL && last))
        for (k = 0; k < 4u; k++)
            if ((uint32_t)CB_BAJOS[cb.bass][k][0] == b)
                note(CB_BAJO, cb_tn - 24 + ch.root + CB_BAJOS[cb.bass][k][1], cb_jit((int32_t)FS / 160),
                     (int32_t)((uint32_t)CB_BAJOS[cb.bass][k][2] * sd * 88u / 100u), b == 0 ? 96 : 88);
    /* percusión: el groove real de la cumbia lenta, con su micro-tiempo; con el boom bap, bombo y tarola con swing
     * y solo el güiro y las congas del groove, más abajo */
    if (s->role == R_FINAL && last)
        return;
    if (cbc.bb) {
        int32_t sw = b % 2u ? (int32_t)(sd * 16u / 100u) : 0;     /* swing en los 16avos impares */
        for (k = 0; k < 4u; k++) {
            if (CB_BB_K[bar % 2u][k][1] && CB_BB_K[bar % 2u][k][0] == b)
                perc_hit(GI_KICK, sw, CB_BB_K[bar % 2u][k][1]);
            if (CB_BB_S[k][1] && CB_BB_S[k][0] == b)
                perc_hit(GI_SNARE, sw, CB_BB_S[k][1]);
        }
        if (b % 2u == 0)
            perc_hit(GI_HAT, sw, 40);
    }
    if (s->perc != PC_NADA || cbc.bb) {
        uint32_t gs = g % CB_GROOVE_STEPS;
        for (k = CB_GROOVE_IDX[gs]; k < CB_GROOVE_IDX[gs + 1u]; k++) {
            const cb_hit_t *h = &CB_GROOVE[k];
            if (cbc.bb ? h->inst <= GI_CONGA2 || h->inst == GI_GUIRO_L || h->inst == GI_GUIRO_S : inst_ok(h->inst, s->perc))
                perc_hit(h->inst, (int32_t)((uint32_t)h->frac * sd / 256u), cbc.bb ? h->vel * 9 / 10 : h->vel * 3 / 2);
        }
    }
}

/* -------------------------------------------------------- transporte --- */
static const int16_t CB_LEVEL[NPART] = {116, 88, 92, 112};   /* acordeón, bajo, percusión, gritos */
static void set_engine_of(track_t *t, uint32_t ei);         /* ui.c */
static void apply_preset_to(track_t *t, uint32_t pi);
static void track_defaults_steps(track_t *t);
static uint8_t cb_voz;                   /* 1 = los gritos están en USR2 */
/* una pista de samples de usuario (percusión USR1, gritos USR2): one-shots que suenan enteros */
static void cb_user_track(track_t *t, uint32_t slot, uint32_t rel)
{
    if (t->eng_req != ENGI_SAMPLE)
        set_engine_of(t, ENGI_SAMPLE);
    t->p[P_E0] = (int16_t)(SMP_NSETS + slot);
    t->p[P_E1] = 0;                       /* TONE: la altura grabada */
    t->p[P_E3] = 0;                       /* sin LOOP */
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 127;
    t->p[P_SUS] = 127;
    t->p[P_REL] = (int16_t)rel;
}
/* las pistas de samples de usuario: los one-shots de USR1 / USR2 si el instalador los cargó, si no DRUM (la
 * percusión) o nada (los gritos). Al encender (cumbia_tracks) y al terminar de escribirse una ranura (editor.c) */
static void cb_perc_setup(void)
{
    track_t *t = &trk[CB_PERC];
    cb_kit = usr_nz[0] != 0;
    cb_voz = usr_nz[1] != 0;
    if (cb_kit)
        cb_user_track(t, 0, 88);          /* la cola suena entera aunque la nota dure un paso */
    else if (t->eng_req != ENGI_DRUM) {
        set_engine_of(t, ENGI_DRUM);
        apply_preset_to(t, 0);
    }
    cb_user_track(&trk[CB_VOZ], 1, 100);
}
/* los envíos: reverb en todas (el bajo, menos), eco de sonidero solo en los gritos (el delay de Felucca a 1/8 con
 * puntillo, la repetición oscura); ciclo principal, cuando cambian */
static void cb_sends(void)
{
    static const uint8_t REV_W[NPART] = {100, 35, 80, 100};
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk[i].p[P_REV] = (int16_t)(cbc.rev * 5u * REV_W[i] / 100u);
        trk[i].p[P_DLY] = 0;
        trk[i].p[P_CHOR] = 0;
    }
    trk[CB_VOZ].p[P_DLY] = cbc.eco ? 72 : 0;
    song.g[G_DTIME] = 10;                 /* 1/8D: 3/4 de tiempo */
    song.g[G_DFDBK] = 54;                 /* ~0.38 */
    song.g[G_DCOLOR] = 33;                /* ~2.5 kHz */
    song.g[G_DMIX] = 90;
    song.g[G_RTYPE] = 0;                  /* ROOM: corta y oscura */
    song.g[G_RSIZE] = 70;
    song.g[G_RDAMP] = 80;
}
static void cumbia_tracks(void)          /* las cuatro pistas con los sonidos de la cumbia (aunque el proyecto guardado traiga
                                          * otros): main.c al encender, después de restaurar la última sesión (contexto de la UI) */
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        track_t *t = &trk[i];
        if (i < 2u && (t->eng_req != TRK_DEF[i][0] || t->preset != TRK_DEF[i][1])) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
        }
        t->p[P_MUTE] = 0;
        t->p[P_LEVEL] = CB_LEVEL[i];      /* la mezcla del prototipo: acordeón al frente, percusión presente */
        t->p[P_SLCR] = 0;                 /* (sl_buf es la cinta para atrás) */
        track_defaults_steps(t);          /* sin patrones guardados: suena solo la canción */
    }
    song.g[G_TUNE] = 0;
    song.g[G_SWING] = 0;
    cb_perc_setup();
    cb_sends();
}

/* la cinta: velocidad (rebajada con inercia), tempo y afinación juntos, como una tornamesa que se frena */
static const int16_t CB_CENTS[41] = {0, -17, -35, -53, -71, -89, -107, -126, -144, -163, -182, -202, -221, -241, -261,
    -281, -302, -323, -344, -365, -386, -408, -430, -452, -475, -498, -521, -545, -569, -593, -617, -642, -668, -693,
    -719, -746, -773, -800, -828, -856, -884};
static uint32_t cb_rate = 65536;         /* Q16: 1.0 a velocidad normal */
static uint32_t cb_wow1, cb_wow2;        /* el vaivén de la cinta (0.55 y 0.23 Hz) */
static uint32_t cb_amt_q16(void) { return 65536u - cb_rate; }   /* cuánto está frenada */
static uint32_t cb_rate_tgt(void) { return 65536u - (uint32_t)cbc.reb * 65536u / 100u; }
static void cb_rate_step(void)           /* un 16avo: se acerca al valor de la perilla (el 0.18 del prototipo) */
{
    int32_t d = (int32_t)cb_rate_tgt() - (int32_t)cb_rate;
    cb_rate = d > -132 && d < 132 ? cb_rate_tgt() : (uint32_t)((int32_t)cb_rate + d * 46 / 256);
}
static uint32_t cb_bpm_now(void) { return (uint32_t)((int32_t)cb.bpm + cbc.bpm); }
static uint32_t cb_len16_now(void)       /* muestras por 16avo al tempo de la cinta */
{
    return (uint32_t)(((uint64_t)FS * 15u << 16) / ((uint64_t)cb_bpm_now() * cb_rate));
}
static void cb_tune_block(uint32_t n)    /* G_TUNE: la cinta lenta, más el vaivén (solo se oye en rebajada) */
{
    uint32_t a = cb_amt_q16(), pc = a * 100u >> 16, fr = a * 100u & 0xFFFFu, depth;
    int32_t c = pc < 40u ? CB_CENTS[pc] + (int32_t)(((int32_t)(CB_CENTS[pc + 1u] - CB_CENTS[pc]) * (int32_t)fr) >> 16)
                         : CB_CENTS[40];
    cb_wow1 += 53565u * n;
    cb_wow2 += 22400u * n;
    depth = 1u + (a * 55u >> 16);                                     /* centésimas: 55 x lo frenado, 16 a lo más */
    depth = depth > 16u ? 16u : depth;
    c += ((osc_sine(cb_wow1) + osc_sine(cb_wow2) * 7 / 10) * (int32_t)depth) >> 15;
    song.g[G_TUNE] = (int16_t)c;
    song.g[G_BPM] = (int16_t)((cb_bpm_now() * cb_rate + 32768u) >> 16);   /* (el eco y la pantalla lo siguen) */
}

static uint32_t cb_next_seed = 1, cb_prev_seed[8], cb_nprev;
static volatile uint8_t cb_done;         /* la canción terminó: cumbia_ui.c detiene el transporte */
static void cb_new_seed(void)            /* el dado */
{
    if (cb.seed) {
        cb_prev_seed[cb_nprev % 8u] = cb.seed;
        cb_nprev++;
    }
    cb_next_seed = cb_next_seed * 1103515245u + 12345u + (uint32_t)fm1_ms;
    cb_generate(cb_next_seed);
}
static void cb_back_seed(void)           /* la canción anterior */
{
    if (!cb_nprev)
        return;
    cb_nprev--;
    cb_generate(cb_prev_seed[cb_nprev % 8u]);
}
static void cumbia_start(void)
{
    cb_all_off();
    if (!cb.nsec)
        cb_new_seed();
    cb_step = 0;
    cb_pos = 0;
    cb_play = 1;
    cb_end = 0;
    cb_done = 0;
}
static void cumbia_stop(void)
{
    cb_play = 0;
    cb_all_off();
}

/* ------------------------------------------------------------- gritos --- */
/* tools/gen_gritos.py: el grito i en las teclas 12 + 24 i .. 35 + 24 i, su altura natural en CB_VOZ_ROOT + 24 i;
 * CB_VOZ_M0: la nota MIDI (redondeada) que canta. Se afina a la tónica o la quinta más cercana: dentro del tono */
#define CB_NVOZ 5u
static const uint8_t CB_VOZ_M0[CB_NVOZ] = {63, 55, 70, 61, 48};   /* wepa, cumbia, opa, a bailar, Monterrey */
static volatile uint8_t cb_grito_on;     /* el último grito disparado + 1 (cumbia_ui.c: el micrófono) */
static void cb_grito(uint32_t i)         /* contexto de audio (keyboard_block) */
{
    int32_t best = 0, bd = 99, m, at = 0;
    if (!cb_voz || i >= CB_NVOZ)
        return;
    for (m = 36; m < 84; m++) {
        int32_t pc = ((m - cb_tn) % 12 + 12) % 12, d = m - CB_VOZ_M0[i];
        d = d < 0 ? -d : d;
        if ((pc == 0 || pc == 7) && d < bd) {
            bd = d;
            best = m;
        }
    }
    if (cb_play) {                       /* en la siguiente corchea */
        at = (int32_t)(cb_len16 > cb_pos ? cb_len16 - cb_pos : 0);
        if (cb_step % 2u)
            at += (int32_t)cb_len16;
    }
    note(CB_VOZ, 24 + 24 * (int32_t)i + best - CB_VOZ_M0[i], at, (int32_t)FS * 3, 110);
    cb_grito_on = (uint8_t)(i + 1u);
}

/* ---------------------------------------------------- teclado del FM-1 --- */
/* las teclas blancas tocan el acordeón en la escala de la canción (menor natural desde la tónica); las negras, los
 * gritos (las cinco del grupo de abajo y de nuevo en las de arriba). Contexto de audio (seq.c keyboard_block) */
static const uint8_t CB_KB_WHITE[27] = {0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9, 0xFF, 10, 11,
    0xFF, 12, 0xFF, 13, 14, 0xFF, 15};
static const uint8_t CB_KB_BLACK[27] = {0, 0, 0, 1, 0, 2, 0, 0, 3, 0, 4, 0, 0, 5, 0, 6, 0, 7, 0, 0, 8, 0, 9, 0, 0, 10, 0};
static uint8_t cb_kb_note[27];
static volatile uint32_t cb_kb_lit;      /* teclas que suenan (cumbia_ui.c: las luces) */
static void cumbia_keys(uint32_t cur, uint32_t ch)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {
            if (CB_KB_WHITE[k] != 0xFF) {
                uint32_t d = CB_KB_WHITE[k];
                int32_t m = d == 0u ? cb_tn - 2 : cb_tn + 12 * (int32_t)((d - 1u) / 7u) + CB_NAT[(d - 1u) % 7u];
                cb_kb_note[k] = (uint8_t)m;
                trk_note_on(&trk[CB_ACC], (uint32_t)m, 96);
            } else if (CB_KB_BLACK[k])
                cb_grito((CB_KB_BLACK[k] - 1u) % CB_NVOZ);
            cb_kb_lit |= 1u << k;
        } else {
            if (CB_KB_WHITE[k] != 0xFF && cb_kb_note[k])
                trk_note_off(&trk[CB_ACC], cb_kb_note[k]);
            cb_kb_note[k] = 0;
            cb_kb_lit &= ~(1u << k);
        }
    }
}

static void cumbia_tick(uint32_t n)
{
    uint32_t i, k;
    cb_tune_block(n);
    if (cb_play && song.playing) {
        if (cb_step == 0) {                                   /* PLAY: el paso 0 ahora */
            cb_len16 = cb_len16_now();
            cb_play_step(0);
            cb_step = 1;
            cb_pos = 0;
        }
        cb_pos += n;
        while (!cb_end && cb_pos >= cb_len16) {               /* los pasos que empiezan en este bloque */
            cb_pos -= cb_len16;
            cb_rate_step();
            cb_len16 = cb_len16_now();
            if (cb_step >= cb.start[cb.nsec])
                cb_end = 1;                                   /* fin: deja sonar el último acorde */
            else
                cb_play_step(cb_step++);
        }
    } else {
        cb_rate = cb_rate_tgt();
        cb_len16 = cb_len16_now();
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
    if (cb_play && cb_end && !cb_nq) {
        cb_play = 0;
        cb_done = 1;
    }
}

/* ------------------------------------------------ cinta para atrás y color --- */
/* Lo último que sonó, grabado sin parar en sl_buf (el SLICER no se usa): mono, 11.025 kHz, 8 bits mu-law, 2.97 s.
 * Mientras se mantiene, la grabación se congela y lo último corre al revés (agarra velocidad en 150 ms), en bucle
 * sobre los últimos 2.5 s; la música sigue debajo, callada, y regresa donde va al soltar. */
#define CR_N 32768u
#define CR_LOOP 27562u                   /* 2.5 s */
static uint32_t cr_w, cr_k;
static int32_t cr_acc;
static uint32_t cr_rd, cr_spd;           /* posición de lectura y velocidad, Q16 en muestras de la cinta */
static int32_t cr_g, cr_main = 32767;    /* nivel de la cinta y de la música, Q15 */
static uint8_t cr_on;
static int32_t cr_lo[2], cr_hi[2];       /* el color de la rebajada: estados de los filtros */
static uint8_t cr_mulaw(int32_t x)
{
    uint32_t s = x < 0 ? 0x80u : 0u, m, e = 7;
    x = x < 0 ? -x : x;
    x = (x > 32635 ? 32635 : x) + 132;
    for (m = 0x4000; e && !((uint32_t)x & m); m >>= 1)
        e--;
    return (uint8_t)~(s | e << 4 | (((uint32_t)x >> (e + 3u)) & 15u));
}
static int32_t cr_unmu(uint8_t u)
{
    int32_t e, m, x;
    u = (uint8_t)~u;
    e = (u >> 4) & 7;
    m = u & 15;
    x = ((m << 3) + 132) << e;
    x -= 132;
    return u & 0x80u ? -x : x;
}
static void cumbia_post(int32_t *l, int32_t *r, int32_t *wet, uint32_t n)
{
    uint8_t *buf = (uint8_t *)sl_buf;
    uint32_t i, a = cb_amt_q16(), pc = a * 100u >> 16;
    static const uint16_t LO[41] = {4096, 4211, 4329, 4450, 4575, 4703, 4835, 4970, 5109, 5252, 5400, 5551, 5706, 5866,
        6031, 6200, 6373, 6552, 6735, 6924, 7118, 7317, 7522, 7733, 7950, 8173, 8402, 8637, 8879, 9128, 9383, 9646, 9917,
        10194, 10480, 10774, 11075, 11386, 11705, 12033, 12370};
    static const uint16_t HI[41] = {4096, 3966, 3840, 3718, 3600, 3486, 3376, 3269, 3165, 3065, 2967, 2873, 2782, 2694,
        2608, 2526, 2445, 2368, 2293, 2220, 2150, 2081, 2015, 1951, 1890, 1830, 1772, 1715, 1661, 1608, 1557, 1508, 1460,
        1414, 1369, 1325, 1283, 1243, 1203, 1165, 1128};
    int32_t lg = (int32_t)LO[pc > 40u ? 40u : pc] - 4096, hg = HI[pc > 40u ? 40u : pc];
    if (cbc.cinta && !cr_on) {
        cr_on = 1;
        cr_rd = (cr_w - 1u) << 16;
        cr_spd = 16384;                                       /* 0.25 */
    }
    for (i = 0; i < n; i++) {
        int32_t x[2] = {l[i] + wet[i], r[i] + wet[i]}, c;
        if (pc) {                                             /* más bajo y menos agudos conforme se frena */
            for (c = 0; c < 2; c++) {
                cr_lo[c] += (int32_t)(((int64_t)(x[c] - cr_lo[c]) * 555) >> 15);
                cr_hi[c] += (int32_t)(((int64_t)(x[c] - cr_hi[c]) * 18830) >> 15);
                x[c] = cr_hi[c] + (int32_t)(((int64_t)(x[c] - cr_hi[c]) * hg) >> 12) +
                       (int32_t)(((int64_t)cr_lo[c] * lg) >> 12);
            }
        }
        if (!cr_on) {
            cr_acc += (x[0] + x[1]) >> 1;
            if (++cr_k == 4u) {
                buf[cr_w++ & (CR_N - 1u)] = cr_mulaw(cr_acc >> 4);   /* (la mezcla está a x4: >> 2 más) */
                cr_acc = 0;
                cr_k = 0;
            }
        } else {
            uint32_t p = cr_rd >> 16, f = cr_rd & 0xFFFFu;
            int32_t s0 = cr_unmu(buf[p & (CR_N - 1u)]), s1 = cr_unmu(buf[(p + 1u) & (CR_N - 1u)]);
            int32_t y = (s0 + (((s1 - s0) * (int32_t)(f >> 1)) >> 15)) << 2;
            cr_rd -= cr_spd >> 2;                             /* la cinta va a 1/4 de la frecuencia */
            if (cr_w - 1u - (cr_rd >> 16) > CR_LOOP)
                cr_rd += CR_LOOP << 16;
            if (cbc.cinta) {
                cr_g += (32767 - cr_g) >> 6;                  /* ~1.5 ms */
                cr_main -= cr_main >> 8;                       /* ~6 ms */
                if (cr_spd < 65536u)
                    cr_spd += 7;                              /* 0.25 -> 1 en ~150 ms */
            } else {
                cr_g -= cr_g >> 9;                            /* ~12 ms */
                cr_main += (32767 - cr_main) >> 9;
                if (cr_g < 64) {
                    cr_on = 0;
                    cr_g = 0;
                    cr_main = 32767;
                }
            }
            for (c = 0; c < 2; c++)
                x[c] = (int32_t)(((int64_t)x[c] * cr_main + (int64_t)y * cr_g) >> 15);
        }
        l[i] = x[0];
        r[i] = x[1];
        wet[i] = 0;
    }
}
