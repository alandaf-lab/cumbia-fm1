/* SPDX-License-Identifier: GPL-3.0-only */
/* La canción de cumbia renderizada en la computadora (el mismo código del FM-1):
 *   build/host/cumbia_sim SEMILLA SEGUNDOS OUT.wav */
static unsigned char host_slots[3][0x14000];          /* KIT=1: build/kit_perc.hdr/.bin en USR1 (tools/gen_kit.py) */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots[k])
#define main hostsim_main
#include "hostsim.c"
#undef main
static void set_engine_of(track_t *t, uint32_t ei) { host_preset(t, ei, 0); }      /* (ui.c en el FM-1) */
static void apply_preset_to(track_t *t, uint32_t pi) { host_preset(t, t->eng_req, pi); }
int main(int argc, char **argv)
{
    uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u, secs = argc > 2 ? (uint32_t)atoi(argv[2]) : 30u, f, i;
    FILE *w = fopen(argc > 3 ? argv[3] : "cumbia.wav", "wb");
    int32_t o[2 * CTL];
    uint32_t vmax = 0, clips = 0, pmax[NPART] = {0}, full = 0, blocks = 0;
    host_tracks_init();
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], TRK_DEF[i][0], TRK_DEF[i][1]);
        trk[i].p[P_LEVEL] = CB_LEVEL[i];
    }
    {                                         /* USR1 el kit, USR2 los gritos (tools/gen_kit.py, gen_gritos.py) */
        static const char *const F[3][2] = {{"build/kit_perc.hdr", "build/kit_perc.bin"}, {"build/gritos.hdr", "build/gritos.bin"},
                                            {"build/gritos2.hdr", "build/gritos2.bin"}};
        for (i = 0; i < 3u && !getenv("NOKIT"); i++) {
            FILE *a = fopen(F[i][0], "rb"), *b = fopen(F[i][1], "rb");
            if (!a || !b) {
                if (i < 2u)
                    return 1;
                continue;                     /* (USR3: opcional) */
            }
            if (!fread(host_slots[i], 1, 512, a) || !fread(host_slots[i] + 512, 1, sizeof host_slots[i] - 512, b))
                return 1;
            fclose(a);
            fclose(b);
            smp_user_scan(i);
        }
        for (i = 0; i < NPART; i++)
            trk[i].p[P_SLCR] = 0;
        cb_perc_setup();
        cb_sends();
        printf("kit: %u zonas, gritos: %u\n", usr_nz[0], usr_nz[1]);
    }
    if (getenv("REB")) cbc.reb = (uint8_t)atoi(getenv("REB"));
    if (getenv("BB")) cbc.bb = 1;
    if (getenv("KEY")) cbc.key = (uint8_t)atoi(getenv("KEY"));
    if (getenv("SENT")) cbc.sent = (uint8_t)atoi(getenv("SENT"));
    if (getenv("BAJO")) cbc.bajo = (uint8_t)atoi(getenv("BAJO"));
    cb_tn = CB_KEYS[cbc.key] - 3;
    cb_levels();
    {                                         /* como al encender: el audio corre antes de que haya canción */
        int32_t ob[2 * CTL];
        for (i = 0; i < 64u; i++)
            mix_block(ob, CTL);
    }
    cb_generate(seed);
    if (getenv("SOLO"))                       /* SOLO=k: solo la pista k (0..3) */
        for (i = 0; i < NPART; i++)
            trk[i].p[P_LEVEL] = i == (uint32_t)atoi(getenv("SOLO")) ? trk[i].p[P_LEVEL] : 0;
    seq_start();
    printf("canción %u: %u BPM, bajo %s, %u secciones, %u pasos, %u eventos\n", cb.seed, cb.bpm, cb.bass ? "dub" : "tumbao",
           cb.nsec, cb.start[cb.nsec], cb_nev);
    for (i = 0; i < cb.nsec; i++)
        printf("  %u: papel %u, %u compases, perc %u, gesto %u\n", i, cb.sec[i].role, cb.sec[i].bars, cb.sec[i].perc,
               cb.sec[i].gesto);
    wav_hdr(w, FS * secs);
    for (f = 0; f < FS * secs; f += CTL) {
        uint32_t v = 0, p, k;
        if (getenv("GRITOS") && f >= FS * 6u && f % (FS * 4u) < CTL)   /* GRITOS: uno cada 4 s desde el segundo 6 */
            cb_grito((f / (FS * 4u)) % CB_NVOZ);   /* (los 11, uno tras otro) */
        if (getenv("CINTA"))                  /* CINTA=s: la cinta para atrás de s a s + 1.5 */
            cbc.cinta = f >= FS * (uint32_t)atoi(getenv("CINTA")) && f < FS * (uint32_t)atoi(getenv("CINTA")) + FS * 3u / 2u;
        if (getenv("STOP") && f / CTL == FS * (uint32_t)atoi(getenv("STOP")) / CTL)   /* STOP=s: el transporte para */
            seq_stop();
        if (getenv("PUSH")) {                 /* PUSH=e: el efecto e de perform.c (100 = ahogada) de 10 a 12 s */
            static int on;
            int want = f >= FS * 10u && f < FS * 12u, e = atoi(getenv("PUSH"));
            if (want != on) {
                on = want;
                if (e == 100)
                    perf_k[0] = (int8_t)(on ? -80 : 0);
                else if (e == 101) {
                    cbc.corte = on ? 1u : 2u;
                    cb_levels();
                }
                else
                    perf_press((uint32_t)e, on);
                if (e == PF_ETHR)
                    song.g[G_DMIX] = (int16_t)(on ? atoi(getenv("DMIXT") ? getenv("DMIXT") : "60") : 90);
            }
        }
        if (getenv("ECO"))
            cbc.eco = 1, cb_sends();
        mix_block(o, CTL);
        for (p = 0; p < NPART; p++) {
            uint32_t pv = 0;
            for (k = 0; k < NVOICE; k++)
                pv += trk[p].v[k].active != 0;
            pmax[p] = pv > pmax[p] ? pv : pmax[p];
            v += pv;
        }
        full += v >= NVOICE;
        blocks++;
        vmax = v > vmax ? v : vmax;
        for (i = 0; i < CTL; i++) {
            clips += o[2 * i] >= 32700 || o[2 * i] <= -32700;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    printf("máx por pista: acordeón %u, bajo %u, percusión %u, gritos %u; bloques con las 8 voces ocupadas: %u%%\n", pmax[0], pmax[1], pmax[2], pmax[3], full * 100u / blocks);
    printf("voces a la vez (máx): %u, muestras saturadas: %u, al final tocando: %u; TUNE %d, BPM %d, paso %u\n", vmax, clips, cb_play, song.g[G_TUNE], song.g[G_BPM], cb_step);
    {                                         /* el tempo: los pasos que debieron sonar (sin rebajada ni fin de canción) */
        uint32_t want = (uint32_t)((uint64_t)cb.bpm * 4u * secs / 60u);
        if (!cbc.reb && cb_play && !getenv("STOP") && (cb_nsteps + 3u < want || cb_nsteps > want + 3u)) {
            printf("FALLA: %u pasos en %u s, debían ser %u\n", cb_nsteps, secs, want);
            return 1;
        }
    }
    return 0;
}
