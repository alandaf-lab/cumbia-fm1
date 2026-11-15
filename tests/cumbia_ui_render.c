/* SPDX-License-Identifier: GPL-3.0-only */
/* Las pantallas de la cumbia renderizadas en la computadora (el mismo código: cumbia_ui.c sobre los stubs de
 * tests/ui_test.c), a PPM: build/host/cumbia_ui_render OUTDIR
 * (tests/cumbia_ui_render.sh lo compila: ui_test.c sin su main) */
#include "ui_test_nomain.c"
#include "../firmware/src/cumbia_ui.c"
static void slot_load(uint32_t k, const char *hdr, const char *bin)
{
    FILE *a = fopen(hdr, "rb"), *b = fopen(bin, "rb");
    uint8_t *s = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    if (a && b) {
        if (fread(s, 1, 512, a) && fread(s + 512, 1, SMP_USER_SIZE - 512, b))
            smp_user_scan(k);
    }
    if (a) fclose(a);
    if (b) fclose(b);
}
static void shot(const char *dir, const char *name)
{
    char p[256];
    FILE *f;
    uint32_t i;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    f = fopen(p, "wb");
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = host_screen[i];
        c = (uint16_t)((c >> 8) | (c << 8));
        fputc((c >> 11) << 3, f);
        fputc(((c >> 5) & 63) << 2, f);
        fputc((c & 31) << 3, f);
    }
    fclose(f);
}
static void cb_press(uint32_t b) { host_pressed |= 1u << panel.btn[b]; }
static void run_ms(uint32_t ms)              /* audio y UI juntos: un cuadro de UI cada 15 ms */
{
    int32_t o[2 * CTL];
    static uint32_t base;
    uint32_t t, acc = 0;
    for (t = 0; t < ms * 441u / 10u; t += CTL) {
        mix_block(o, CTL);
        acc += CTL;
        fm1_ms = base + t / 44u;
        if (acc >= 661u) {
            acc = 0;
            cb_ui_input();
            cb_ui_draw();
        }
    }
    base = fm1_ms;
}
int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : "build/ui_cumbia";
    uint32_t i;
    host_tracks_init();
    panel = PANEL_DEFAULT;
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], TRK_DEF[i][0], TRK_DEF[i][1]);
    slot_load(0, "build/kit_perc.hdr", "build/kit_perc.bin");
    slot_load(1, "build/gritos.hdr", "build/gritos.bin");
    slot_load(2, "build/gritos2.hdr", "build/gritos2.bin");
    cumbia_tracks();
    cb_splash();
    shot(d, "0_arranque");
    cb_ui_boot();
    cb_ui_draw();
    shot(d, "1_parado");
    cb_press(B_PLAY);
    run_ms(3000);
    shot(d, "2_tocando");
    run_ms(200);
    shot(d, "2b_tocando");
    run_ms(200);
    shot(d, "2c_tocando");
    host_enc[panel.enc[EN_K1]] = -25;
    host_enc[panel.enc[EN_K2]] = 4;
    run_ms(2000);
    shot(d, "3_rebajada");
    cb_grito(0);
    run_ms(1500);
    shot(d, "4_grito");
    cb_press(B_SEQ);
    run_ms(2500);
    shot(d, "5_boombap");
    cb_press(B_SEQ);
    host_enc[panel.enc[EN_K4]] = 1;
    host_enc[panel.enc[EN_SELECT]] = 2;
    run_ms(1500);
    shot(d, "6_color_tono_eco");
    host_enc[panel.enc[EN_SELECT]] = -2;
    run_ms(3000);
    fm1_in.buttons |= 1u << panel.btn[B_FX];
    run_ms(500);
    shot(d, "7_cinta");
    fm1_in.buttons = 0;
    cb_press(B_GLO);
    run_ms(100);
    host_enc[panel.enc[EN_K3]] = 6;
    run_ms(300);
    shot(d, "7b_mezcla");
    cb_press(B_GLO);
    cb_press(B_REC);
    run_ms(200);
    shot(d, "8_dado");
    run_ms(1000);
    shot(d, "9_cancion_nueva");
    cb_press(B_GLO);
    run_ms(100);
    cb_press(B_SCL);
    run_ms(100);
    fm1_in.buttons |= 1u << panel.btn[B_GLO];
    cb_press(B_LFO);
    run_ms(200);
    fm1_in.buttons = 0;
    shot(d, "a_mezcla_mute_solo");
    cb_press(B_HOME);
    cb_solo = 0xFF;
    cb_mute = 0;
    cb_levels();
    run_ms(2500);
    cb_loading = 1;
    cb_load_off = 40000;
    cb_load_ms = 0;          /* (run_ms cuenta fm1_ms desde 0) */
    run_ms(100);
    shot(d, "b_cargando");
    run_ms(5500);
    {                                    /* HOME mantenido: el mapa; tocado: la cholombiana */
        uint32_t hb = 1u << panel.btn[B_HOME];
        host_pressed |= hb;
        fm1_in.buttons |= hb;
        run_ms(700);
        shot(d, "c_mapa");
        fm1_in.buttons &= ~hb;
        run_ms(200);
        host_pressed |= hb;
        fm1_in.buttons |= hb;
        run_ms(60);
        fm1_in.buttons &= ~hb;
        run_ms(1500);
        shot(d, "d_cholombiana");
    }
    printf("pantallas en %s\n", d);
    return 0;
}
