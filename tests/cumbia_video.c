/* SPDX-License-Identifier: GPL-3.0-only */
/* El video de presentación: la cumbia del FM-1 tocada con un guion (mismo código: cumbia_ui.c y el motor sobre los
 * stubs de tests/ui_test.c). Escribe OUT.wav y OUT.rgb (240 x 240 RGB, 30 cuadros por segundo):
 *   build/host/cumbia_video OUT SEGUNDOS  (tests/cumbia_video.sh) */
#include "ui_test_nomain.c"
#include "../firmware/src/cumbia_ui.c"
static void slot_load(uint32_t k, const char *hdr, const char *bin)
{
    FILE *a = fopen(hdr, "rb"), *b = fopen(bin, "rb");
    uint8_t *s = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    if (a && b && fread(s, 1, 512, a) && fread(s + 512, 1, SMP_USER_SIZE - 512, b))
        smp_user_scan(k);
    if (a) fclose(a);
    if (b) fclose(b);
}
/* el guion: segundo, qué, valor */
enum { PRESS, DOWN, UP, GRITO, REB, KDOWN, KUP, ENC };
#define EN(e, d) ((e) * 16 + (d) + 8)
static const struct { float t; int what; int v; } EV[] = {
    {0.0f, PRESS, B_PLAY},
    {4.6f, PRESS, B_REC},
    {16.0f, KDOWN, 14}, {16.3f, KUP, 14}, {16.35f, KDOWN, 16}, {16.65f, KUP, 16}, {16.7f, KDOWN, 18}, {17.3f, KUP, 18},
    {17.35f, KDOWN, 16}, {17.6f, KUP, 16}, {17.65f, KDOWN, 14}, {18.3f, KUP, 14},
    {19.0f, GRITO, 0}, {20.8f, GRITO, 8},
    {22.6f, REB, -35}, {29.8f, REB, 35},
    {32.6f, ENC, EN(EN_PRESET, 1)}, {34.1f, ENC, EN(EN_PRESET, 1)}, {35.6f, ENC, EN(EN_PRESET, 1)},
    {37.5f, ENC, EN(EN_ALGO, 2)}, {38.6f, ENC, EN(EN_ALGO, 2)},
    {41.2f, PRESS, B_SEQ},
    {46.2f, DOWN, B_SCL}, {48.6f, UP, B_SCL},
    {49.7f, DOWN, B_FX}, {51.3f, UP, B_FX},
    {53.2f, DOWN, B_EDIT}, {54.8f, UP, B_EDIT},
    {56.7f, DOWN, B_LFO}, {58.3f, UP, B_LFO},
    {60.2f, DOWN, B_ARP}, {60.6f, GRITO, 6}, {63.0f, UP, B_ARP},
    {64.0f, DOWN, B_GLO}, {64.5f, PRESS, B_FX}, {65.0f, UP, B_GLO},
    {66.4f, PRESS, B_SCL}, {67.2f, PRESS, B_SCL},
    {67.8f, PRESS, B_GLO}, {68.2f, DOWN, B_GLO}, {68.5f, PRESS, B_FX}, {68.8f, UP, B_GLO}, {69.8f, PRESS, B_GLO},
    {70.6f, PRESS, B_SEQ}, {71.1f, PRESS, B_OCTUP},
    {74.5f, PRESS, B_HOME}, {76.0f, GRITO, 3}, {80.5f, GRITO, 1},
};
int main(int argc, char **argv)
{
    char p[256];
    uint32_t secs = argc > 2 ? (uint32_t)atoi(argv[2]) : 62u, total, s, ui = 0, nextf = 0, ev = 0, i;
    int32_t o[2 * CTL], reb_left = 0;
    FILE *w, *v;
    snprintf(p, sizeof p, "%s.wav", argv[1]);
    w = fopen(p, "wb");
    snprintf(p, sizeof p, "%s.rgb", argv[1]);
    v = fopen(p, "wb");
    host_tracks_init();
    panel = PANEL_DEFAULT;
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], TRK_DEF[i][0], TRK_DEF[i][1]);
    slot_load(0, "build/kit_perc.hdr", "build/kit_perc.bin");
    slot_load(1, "build/gritos.hdr", "build/gritos.bin");
    slot_load(2, "build/gritos2.hdr", "build/gritos2.bin");
    cumbia_tracks();
    cb_ui_boot();
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
    total = FS * secs;
    wav_hdr(w, total);
    for (s = 0; s < total; s += CTL) {
        while (ev < sizeof EV / sizeof EV[0] && (float)s >= EV[ev].t * FS) {
            uint32_t b = EV[ev].what <= UP ? 1u << panel.btn[EV[ev].v] : 0u;
            switch (EV[ev].what) {
            case PRESS: host_pressed |= b; break;
            case DOWN: fm1_in.buttons |= b; host_pressed |= b; break;
            case UP: fm1_in.buttons &= ~b; break;
            case GRITO: cb_grito((uint32_t)EV[ev].v); break;
            case REB: reb_left = EV[ev].v; break;
            case KDOWN: if (!getenv("NOKEYS")) fm1_in.notes |= 1u << EV[ev].v; break;
            case KUP: fm1_in.notes &= ~(1u << EV[ev].v); break;
            case ENC: host_enc[panel.enc[EV[ev].v / 16]] += EV[ev].v % 16 - 8; break;
            }
            ev++;
        }
        mix_block(o, CTL);
        fm1_ms = (uint32_t)((uint64_t)s * 1000u / FS);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
        if ((ui += CTL) >= 661u) {        /* un cuadro de UI cada 15 ms, como en el FM-1 */
            ui = 0;
            static uint32_t slow;
            if (reb_left && ++slow % 4u == 0u) {   /* la perilla se gira poco a poco (2 s de punta a punta) */
                int32_t d = reb_left > 0 ? 1 : -1;
                host_enc[panel.enc[EN_K1]] += d;
                reb_left -= d;
            }
            cb_ui_input();
            cb_ui_draw();
        }
        while (s + CTL > nextf * (FS / 30u)) {   /* 30 cuadros por segundo */
            for (i = 0; i < 240u * 240u; i++) {
                uint16_t c = host_screen[i];
                c = (uint16_t)((c >> 8) | (c << 8));
                fputc((c >> 11) << 3, v);
                fputc(((c >> 5) & 63) << 2, v);
                fputc((c & 31) << 3, v);
            }
            nextf++;
        }
    }
    fclose(w);
    fclose(v);
    printf("%u cuadros, canción %u a %u BPM\n", nextf, cb.seed, cb.bpm);
    return 0;
}
