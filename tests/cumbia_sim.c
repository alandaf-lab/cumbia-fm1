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
    if (getenv("KIT")) {
        FILE *a = fopen("build/kit_perc.hdr", "rb"), *b = fopen("build/kit_perc.bin", "rb");
        if (!a || !b || !fread(host_slots[0], 1, 512, a) || !fread(host_slots[0] + 512, 1, sizeof host_slots[0] - 512, b))
            return 1;
        fclose(a);
        fclose(b);
        smp_user_scan(0);
        host_preset(&trk[CB_PERC], ENGI_SAMPLE, 0);
        trk[CB_PERC].p[P_LEVEL] = CB_LEVEL[CB_PERC];
        cb_perc_setup();
        printf("kit: %u zonas\n", usr_nz[0]);
    }
    cb_next_seed = seed;
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
    printf("máx por pista: acordeón %u, bajo %u, percusión %u, acordes %u; bloques con las 8 voces ocupadas: %u%%\n", pmax[0], pmax[1], pmax[2], pmax[3], full * 100u / blocks);
    printf("voces a la vez (máx): %u, muestras saturadas: %u, al final tocando: %u\n", vmax, clips, cb_play);
    return 0;
}
