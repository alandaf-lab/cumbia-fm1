/* SPDX-License-Identifier: GPL-3.0-only */
/* CUMBIA en el navegador: el mismo código del FM-1 (cumbia.c, cumbia_ui.c y el motor de Felucca, sobre los stubs de
 * tests/ui_test.c) compilado a WebAssembly. Lo corre un AudioWorklet (player.js): audio, pantalla, botones y teclas.
 *   sh web/player/build.sh  ->  site/play/cumbia.wasm */
#include "ui_test_nomain.c"
#include "../firmware/src/cumbia_ui.c"
#undef __attribute__
#define EX(n) __attribute__((export_name(#n)))

static float p_out[2 * 4096];
static uint32_t p_ui, p_samples, p_keys;

EX(p_slots) uint8_t *p_slots(void) { return (uint8_t *)host_slots; }
EX(p_slot_size) uint32_t p_slot_size(void) { return SMP_USER_SIZE; }
EX(p_screen) uint16_t *p_screen(void) { return host_screen; }
EX(p_outbuf) float *p_outbuf(void) { return p_out; }
EX(p_rate) uint32_t p_rate(void) { return FS; }

EX(p_init) void p_init(void)            /* después de escribir las ranuras (p_slots) */
{
    uint32_t i;
    int32_t o[2 * CTL];
    host_tracks_init();
    panel = PANEL_DEFAULT;
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], TRK_DEF[i][0], TRK_DEF[i][1]);
    for (i = 0; i < 3u; i++)
        smp_user_scan(i);
    cumbia_tracks();
    cb_ui_boot();
    for (i = 0; i < 64u; i++)
        mix_block(o, CTL);
}
EX(p_button) void p_button(uint32_t label, uint32_t down)
{
    uint32_t b = 1u << panel.btn[label];
    if (down) {
        fm1_in.buttons |= b;
        host_pressed |= b;
    } else
        fm1_in.buttons &= ~b;
}
EX(p_buttons) void p_buttons(uint32_t mask)   /* los 14 botones a la vez (bit = panel.c B_*) */
{
    uint32_t i, m = 0;
    for (i = 0; i < NB; i++)
        if ((mask >> i) & 1u)
            m |= 1u << panel.btn[i];
    host_pressed |= m & ~fm1_in.buttons;
    fm1_in.buttons = m;
}
EX(p_keys_mask) void p_keys_mask(uint32_t mask) { p_keys = mask & 0x7FFFFFFu; }
EX(p_knob) void p_knob(uint32_t en, int32_t d) { host_enc[panel.enc[en]] += d; }
EX(p_key) void p_key(uint32_t k, uint32_t down)
{
    if (k < 27u)
        p_keys = down ? p_keys | 1u << k : p_keys & ~(1u << k);
}
/* n cuadros (múltiplo de CTL) en p_out, intercalados L R, de -1 a 1 */
EX(p_render) void p_render(uint32_t n)
{
    int32_t o[2 * CTL];
    uint32_t s, i;
    for (s = 0; s < n; s += CTL) {
        fm1_in.notes = p_keys;
        keyboard_block();
        mix_block(o, CTL);
        p_samples += CTL;
        fm1_ms = (uint32_t)((uint64_t)p_samples * 1000u / FS);
        host_ticks = fm1_ms;
        for (i = 0; i < 2u * CTL; i++)
            p_out[2u * s + i] = (float)o[i] * (1.0f / 32768.0f);
        if ((p_ui += CTL) >= 661u) {     /* la pantalla y los botones cada 15 ms, como en el FM-1 */
            p_ui = 0;
            cb_ui_input();
            cb_ui_draw();
        }
    }
}
