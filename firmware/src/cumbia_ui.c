/* SPDX-License-Identifier: GPL-3.0-only */
/* CUMBIA: la interfaz del FM-1 (en lugar de la de Felucca: main.c llama cb_ui_input, cb_ui_leds y cb_ui_draw).
 *
 * Botones                                   Perillas
 *   PLAY   tocar / parar la canción          KNOB 1  rebajada (0 a -35 %, con inercia, como una tornamesa)
 *   REC    el dado: canción nueva            KNOB 2  reverb
 *   OCT-   la canción anterior               KNOB 3  tempo (+-20 BPM sobre el de la canción)
 *   FX     cinta para atrás (mientras se     KNOB 4  tono del acordeón (Sol Do Fa Re La Sib)
 *          mantiene)                         SELECT  color de la pantalla
 *   SCL    boom bap sí / no
 *   LFO    eco de sonidero en los gritos sí / no
 *   GLO    la mezcla (KNOB 1..4: acordeón, bajo, percusión, gritos); otra vez, regresa
 *   ENV    ahogada (mantenido)     EDIT freno (mantenido)
 *   ARP    repetición (mantenido)  SEQ  eco a todo (mantenido)
 * Teclado: las blancas tocan el acordeón en la escala de la canción, las negras los gritos (cumbia.c cumbia_keys).
 *
 * Pantalla 240 x 240, un solo color sobre negro (el usuario lo elige):
 *   0..76     encabezado (logo, tono y tempo, estado) y las cuatro tarjetas de perilla
 *   76..198   el panel: la tarjeta del acordeón, el bailarín (el DJ con el boom bap), la tarjeta de la percusión
 *   202..240  el dado y el mapa de la canción
 * Todo lo que se mueve avanza con los pasos de la canción, no con un reloj. */
#include "cumbia_gfx.h"

static const uint32_t CB_COLORS[8] = {0xFFFFFF, 0x7CFFB2, 0xFFB000, 0x55E0FF, 0xFF6EC7, 0xFF4A3D, 0x8C9EFF, 0xFFE94D};
static const char *const CB_COLOR_NAMES[8] = {"BLANCO", "VERDE", "AMBAR", "CIAN", "ROSA", "ROJO", "AZUL", "AMARILLO"};
static struct {
    uint8_t color;
    uint16_t fg, mid, dim, bg;
    uint16_t ramp[16];                   /* el fondo -> el color, para las máscaras */
    uint16_t rdim[16];                   /* el fondo -> el color tenue */
    uint8_t dirty;                       /* bits: 1 encabezado, 2 panel, 4 pie */
    uint8_t fresh;                       /* la primera canción aún no suena: el PLAY tira el dado */
    uint8_t dice, dice_t;                /* el dado rodando: cuadros que faltan */
    uint32_t lstep;                      /* el último paso dibujado */
    uint8_t dancer, card_l, card_r, rkind;
    uint32_t hits_seen, grito_t, acc_t;
    int8_t msg_t;
    char msg[24];
    uint8_t page;                        /* 0 la canción, 1 la mezcla (GLO) */
    uint8_t push[4];                     /* los efectos push mantenidos (CB_PUSH) */
    uint16_t songs;                      /* canciones sacadas con el dado */
} cbu = {0};

static uint16_t cb_rgb(uint32_t c, uint32_t k)   /* el color a k / 15 */
{
    uint32_t r = (c >> 16 & 255u) * k / 15u, g = (c >> 8 & 255u) * k / 15u, b = (c & 255u) * k / 15u;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static void cb_theme(void)
{
    uint32_t k, c = CB_COLORS[cbu.color & 7u];
    for (k = 0; k < 16u; k++) {
        cbu.ramp[k] = cb_rgb(c, k);
        cbu.rdim[k] = cb_rgb(c, k * 6u / 15u);
    }
    cbu.fg = cbu.ramp[15];
    cbu.mid = cb_rgb(c, 10);
    cbu.dim = cb_rgb(c, 5);
    cbu.bg = 0;
    ux.bg = ux.surf = 0;                 /* (los textos de gfx.c se mezclan con el fondo del lienzo) */
    cbu.dirty = 7;
}

/* una pieza de CB_GFX en el lienzo (x, y: su esquina) */
static void cb_spr(int32_t x, int32_t y, uint32_t id, const uint16_t *ramp)
{
    const cb_spr_t *s = &CB_SPR[id];
    const uint8_t *d = CB_GFX + s->off;
    uint32_t w = s->w, h = s->h ? s->h : 256u, n = w * h, k = 0;
    while (k < n) {
        uint32_t b = *d++, run, v;
        if (b < 0x80u) {
            k += b + 1u;
            continue;
        }
        v = (b >> 3) & 15u;
        run = (b & 7u) + 1u;
        while (run-- && k < n) {
            int32_t px = x + (int32_t)(k % w), py = y + (int32_t)(k / w) + cv_oy;
            if ((uint32_t)px < cv_w && (uint32_t)py < cv_h)
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16(ramp[v]);
            k++;
        }
    }
}
static void cb_text_c(int32_t cx, int32_t y, const aafont_t *f, const char *s, uint16_t fg)   /* centrado en cx */
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, fg);
}
static void cb_num(char *b, int32_t v, const char *pre)   /* "pre" + v */
{
    char t[8];
    uint32_t n = 0, i = 0;
    int neg = v < 0;
    v = neg ? -v : v;
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) && n < 7u);
    while (*pre)
        b[i++] = *pre++;
    if (neg)
        b[i++] = '-';
    while (n)
        b[i++] = t[--n];
    b[i] = 0;
}

/* --------------------------------------------------------- arranque --- */
static void cb_splash(void)
{
    cb_theme();
    lcd_fill(0, 0, 240, 240, 0);
    cv_begin(240, 120, 0);
    cb_spr(0, 0, SPR_LOGO_ARRANQUE_240, cbu.ramp);
    cv_blit(0, 0);
    cv_begin(240, 120, 0);
    cv_oy = -120;
    cb_spr(0, 0, SPR_LOGO_ARRANQUE_240, cbu.ramp);
    cv_oy = 0;
    cv_blit(0, 120);
}

/* ------------------------------------------------------------ entrada --- */
static void cb_say2(const char *m, const char *v)   /* arriba, ~1.5 s: lo que se está moviendo y su valor */
{
    uint32_t i = 0;
    while (*m && i < 22u)
        cbu.msg[i++] = *m++;
    if (v) {
        cbu.msg[i++] = ' ';
        while (*v && i < 23u)
            cbu.msg[i++] = *v++;
    }
    cbu.msg[i] = 0;
    cbu.msg_t = 100;
    cbu.dirty |= 1;
}
static void cb_say(const char *m) { cb_say2(m, 0); }
static void cb_say_n(const char *m, int32_t v, const char *unit)
{
    char b[12];
    uint32_t i;
    cb_num(b, v, "");
    for (i = 0; b[i]; i++)
        ;
    while (*unit && i < 11u)
        b[i++] = *unit++;
    b[i] = 0;
    cb_say2(m, b);
}
static const char *const CB_MIX_NAMES[NPART] = {"ACORDEON", "BAJO", "PERCUSION", "GRITOS"};
static void cb_dice(void)
{
    cbu.songs++;
    cbu.dice = 10;
    cbu.dice_t = 0;
    cbu.dirty |= 4;
}
/* efectos push (los de Felucca, perform.c), mientras se mantiene el botón */
#define CB_LPF 0xFFu
static const struct { uint8_t btn, pf; const char *name; } CB_PUSH[4] = {
    {B_ENV, CB_LPF, "AHOGADA"},         /* un pasabajos fijo: la música ahogada */
    {B_EDIT, PF_TAPE, "FRENO"},          /* la tornamesa se apaga */
    {B_ARP, PF_R16, "REPETICION"},       /* repite el último 16avo (un octavo no cabe en el búfer abajo de 81 BPM) */
    {B_SEQ, PF_ETHR, "ECO A TODO"},      /* toda la mezcla al eco */
};
static void cb_ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), held = fm1_in.buttons;
    int32_t e;
    (void)fm1_input_note_edges();        /* (el teclado lo lee seq.c keyboard_block) */
#define CB_PR(b) ((pressed >> panel.btn[b]) & 1u)
#define CB_HELD(b) ((held >> panel.btn[b]) & 1u)
    if (CB_PR(B_PLAY)) {
        if (song.playing)
            seq_stop();
        else {
            if (cbu.fresh) {             /* la primera: una al azar (el momento en que se apretó) */
                cb_next_seed ^= fm1_ticks();
                cb_new_seed();
                cb_dice();
            }
            cbu.fresh = 0;
            seq_start();
        }
        cbu.dirty |= 7;
    }
    if (CB_PR(B_REC) || CB_PR(B_OCTDN)) {   /* el dado / la anterior: si suena, sigue sonando la nueva */
        if (CB_PR(B_OCTDN) && cbu.songs > 1u)
            cbu.songs--;
        uint32_t was = song.playing;
        if (was)
            seq_stop();
        if (CB_PR(B_REC)) {
            cb_next_seed ^= fm1_ticks();
            cb_new_seed();
            cb_dice();
        } else
            cb_back_seed();
        cbu.fresh = 0;
        if (was)
            seq_start();
        cbu.dirty |= 7;
    }
    perf_latch_on = 0;                   /* (los efectos push solo mientras se mantienen) */
    {
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            uint8_t h = (uint8_t)CB_HELD(CB_PUSH[k].btn);
            if (h == cbu.push[k])
                continue;
            cbu.push[k] = h;
            if (CB_PUSH[k].pf == CB_LPF)
                perf_k[0] = (int8_t)(h ? -80 : 0);
            else
                perf_press(CB_PUSH[k].pf, h);
            if (h)
                cb_say(CB_PUSH[k].name);
            cbu.dirty |= 2;
        }
    }
    if (cbc.cinta != CB_HELD(B_FX)) {
        cbc.cinta = (uint8_t)CB_HELD(B_FX);
        cbu.dirty |= 3;
    }
    if (CB_PR(B_SCL)) {
        cbc.bb ^= 1u;
        cb_levels();
        cb_say(cbc.bb ? "BOOM BAP" : "CUMBIA");
        cbu.dirty |= 7;
    }
    if (CB_PR(B_LFO)) {
        cbc.eco ^= 1u;
        cb_sends();
        cb_say(cbc.eco ? "ECO SI" : "ECO NO");
    }
    if (CB_PR(B_GLO)) {                  /* la mezcla: KNOB 1..4 los niveles de las pistas; GLO de nuevo, regresa */
        cbu.page ^= 1u;
        cb_say(cbu.page ? "MEZCLA" : "CANCION");
        cbu.dirty |= 1;
    }
    if (cbu.page) {
        static const uint8_t K[NPART] = {EN_K1, EN_K2, EN_K3, EN_K4};
        uint32_t i;
        for (i = 0; i < NPART; i++)
            if ((e = panel_enc(K[i])) != 0) {
                int32_t v = (int32_t)cb_mix[i] + e * 2;
                cb_mix[i] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                cb_levels();
                cb_say_n(CB_MIX_NAMES[i], (int32_t)cb_mix[i] * 100 / 127, "");
            }
    } else {
        if ((e = panel_enc(EN_K1)) != 0) {   /* a la izquierda se rebaja: la velocidad baja de 100 % a 65 % */
            int32_t v = (int32_t)cbc.reb - e;
            cbc.reb = (uint8_t)(v < 0 ? 0 : v > 35 ? 35 : v);
            cb_say_n("REBAJADA", 100 - (int32_t)cbc.reb, "%");
        }
        if ((e = panel_enc(EN_K2)) != 0) {
            int32_t v = (int32_t)cbc.rev + e;
            cbc.rev = (uint8_t)(v < 0 ? 0 : v > 20 ? 20 : v);
            cb_sends();
            cb_say_n("REVERB", cbc.rev * 5, "");
        }
        if ((e = panel_enc(EN_K3)) != 0) {
            int32_t v = cbc.bpm + e;
            cbc.bpm = (int8_t)(v < -20 ? -20 : v > 20 ? 20 : v);
            cb_say_n("TEMPO", (int32_t)cb_bpm_now(), " BPM");
        }
        if ((e = panel_enc(EN_K4)) != 0) {
            cbc.key = (uint8_t)(((int32_t)cbc.key + (e > 0 ? 1 : 5)) % 6);
            cb_tn = CB_KEYS[cbc.key] - 3;
            cb_say2("TONO", CB_KEY_NAMES[cbc.key]);
        }
    }
    if ((e = panel_enc(EN_SELECT)) != 0) {
        cbu.color = (uint8_t)(((int32_t)cbu.color + (e > 0 ? 1 : 7)) % 8);
        cb_theme();
        cb_say2("COLOR", CB_COLOR_NAMES[cbu.color]);
    }
    (void)panel_enc(EN_ALGO);
    (void)panel_enc(EN_PRESET);
    if (cb_done) {                       /* la canción terminó */
        cb_done = 0;
        seq_stop();
        cbu.dirty |= 7;
    }
#undef CB_PR
#undef CB_HELD
}

/* -------------------------------------------------------------- luces --- */
static void cb_ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    static uint8_t ready;
    uint32_t k, lit = cb_kb_lit;
    if (fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    led_put(nl, panel.btn[B_SCL], cbc.bb);
    led_put(nl, panel.btn[B_LFO], cbc.eco);
    led_put(nl, panel.btn[B_FX], cbc.cinta);
    for (k = 0; k < 4u; k++)
        led_put(nl, panel.btn[CB_PUSH[k].btn], cbu.push[k]);
    for (k = 0; k < 27u; k++) {
        led_put(nd, 14u + k, 1);
        led_put(nl, 14u + k, (int)((lit >> k) & 1u));
    }
    if (song.playing) {
        led_clear(nd, panel.btn[B_PLAY]);
        if (cb_step % 4u < 2u)           /* el verde late en cada tiempo */
            nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    }
    fm1_led_dim_level(0);
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_dim[k] = (uint8_t)(nd[k] & ~nl[k]);
        fm1_led_breath[k] = 0;
        fm1_led_mid[k] = 0;
        fm1_led[k] = nl[k];
    }
}

/* -------------------------------------------------------------- dibujo --- */
static const uint8_t CB_ROLE_ICON[4] = {SPR_PAPEL_ACORDES, SPR_PAPEL_MELODIA, SPR_PAPEL_VOZ, SPR_PAPEL_FINAL};
static uint32_t cb_sec_of(uint32_t g)
{
    uint32_t i = 0;
    while (i + 1u < cb.nsec && cb.start[i + 1u] <= g)
        i++;
    return i;
}
static void cb_card(int32_t x, int32_t y, uint32_t icon, const char *val, int hot)   /* icon SPR_N: "BPM" */
{
    cv_rrect(x, y, 57, 44, 6, hot ? cbu.dim : cb_rgb(CB_COLORS[cbu.color & 7u], 2), 0);
    if (icon < SPR_N)
        cb_spr(x + 16, y + 2, icon, cbu.ramp);
    else
        cb_text_c(x + 28, y + 6, &AF_S, "BPM", cbu.mid);
    cb_text_c(x + 28, y + 26, &AF_S, val, cbu.fg);
}
static const char *const CB_ROLE_NAME[4] = {"INTRO", "ACORDEON", "PARA LA VOZ", "FINAL"};
static void cb_draw_head(void)
{
    char b[24];
    uint32_t i;
    cv_begin(240, 76, 0);
    cb_spr(6, 2, SPR_LOGO_ENCABEZADO_20PX, cbu.ramp);
    if (cbu.msg_t > 0)                   /* lo que se está moviendo */
        cb_text_c(120, 3, &AF_M, cbu.msg, cbu.fg);
    else if (song.playing && cb.nsec)    /* la parte de la canción */
        cb_text_c(120, 3, &AF_M, CB_ROLE_NAME[cb.sec[cb_sec_of(cb_step)].role & 3u], cbu.mid);
    else {
        cb_num(b, cbu.songs ? cbu.songs : 1, "CANCION ");
        cb_text_c(120, 3, &AF_M, b, cbu.mid);
    }
    if (cbc.eco)
        cb_spr(196, 2, SPR_GRITOS_16, cbu.ramp);
    if (song.playing)                    /* reproducción: un triángulo */
        for (i = 0; i < 12u; i++)
            cv_rect(218 + (int32_t)i, 4 + (int32_t)i / 2, 1, 12 - (int32_t)i, cbu.fg);
    else
        cv_rect(218, 5, 10, 10, cbu.dim);
    if (cbu.page) {                      /* la mezcla */
        static const uint8_t IC[NPART] = {SPR_N, SPR_N, SPR_N, SPR_GRITOS_24};
        static const char *const LB[NPART] = {"ACOR", "BAJO", "PERC", "VOZ"};
        for (i = 0; i < NPART; i++) {
            int32_t x = 3 + 59 * (int32_t)i;
            cb_num(b, (int32_t)cb_mix[i] * 100 / 127, "");
            cv_rrect(x, 28, 57, 44, 6, cbu.dim, 0);
            if (IC[i] < SPR_N)
                cb_spr(x + 16, 30, IC[i], cbu.ramp);
            else
                cb_text_c(x + 28, 34, &AF_S, LB[i], cbu.fg);
            cb_text_c(x + 28, 54, &AF_S, b, cbu.fg);
        }
        cv_blit(0, 0);
        return;
    }
    cb_num(b, 100 - (int32_t)cbc.reb, "");
    for (i = 0; b[i]; i++)
        ;
    b[i++] = '%';
    b[i] = 0;
    cb_card(3, 28, SPR_FX_REBAJADA, b, cbc.reb != 0);
    cb_num(b, cbc.rev * 5, "");
    cb_card(62, 28, SPR_FX_REVERB, b, 0);
    cb_num(b, (int32_t)cb_bpm_now(), "");
    cb_card(121, 28, SPR_N, b, cbc.bpm != 0);
    cb_card(180, 28, SPR_FX_TONO, CB_KEY_NAMES[cbc.key], 0);
    cv_blit(0, 0);
}
static void cb_draw_panel(void)
{
    cv_begin(240, 122, 0);
    cb_spr(4, 33, SPR_ACORDEON_0 + cbu.card_l, cbu.ramp);
    if (cbc.bb)
        cb_spr(88, 13, SPR_DJ_0 + cbu.dancer % 4u, cbu.ramp);
    else
        cb_spr(88, 13, SPR_BAILARIN_0 + cbu.dancer % 8u, cbc.cinta ? cbu.rdim : cbu.ramp);
    cb_spr(164, 33, cbu.rkind + cbu.card_r, cbu.ramp);
    if (cbc.cinta)
        cb_spr(8, 2, SPR_FX_CINTA_ATRAS, cbu.ramp);
    cv_blit(0, 76);
}
static void cb_draw_foot(void)
{
    uint32_t i, len = cb.nsec ? cb.start[cb.nsec] : 1u, x0 = 44, w = 192;
    uint32_t pos = song.playing ? cb_step : 0u;
    cv_begin(240, 38, 0);
    cb_spr(4, 3, SPR_DADO_0 + (cbu.dice ? 10u - cbu.dice : 0u), cbu.ramp);
    for (i = 0; i < cb.nsec; i++) {
        uint32_t a = x0 + cb.start[i] * w / len, b = x0 + cb.start[i + 1u] * w / len;
        int cur = song.playing && cb_sec_of(pos) == i;
        cv_rect((int32_t)a + 1, 22, (int32_t)(b - a) - 2, 10, cur ? cbu.fg : cbu.dim);
        if (b - a >= 18u)
            cb_spr((int32_t)(a + b) / 2 - 8, 3, CB_ROLE_ICON[cb.sec[i].role & 3u], cur ? cbu.ramp : cbu.rdim);
    }
    if (song.playing)
        cv_rect((int32_t)(x0 + pos * w / len), 18, 2, 18, cbu.fg);
    cv_blit(0, 202);
}

/* lo que cambió con la música (los pasos, los golpes) */
static void cb_follow(void)
{
    uint32_t st = song.playing ? cb_step : 0u, hits = cb_hits, sec;
    uint8_t d, l, r, kind;
    cb_hits = 0;
    if (cbu.msg_t > 0 && !--cbu.msg_t)
        cbu.dirty |= 1;
    if (cbu.dice) {                      /* el dado rueda: un cuadro cada 3 refrescos */
        if (++cbu.dice_t >= 3u) {
            cbu.dice_t = 0;
            cbu.dice--;
            cbu.dirty |= 4;
        }
    }
    if (st != cbu.lstep) {
        if (!(st % 16u) || (st >> 4) != (cbu.lstep >> 4))
            cbu.dirty |= 4;
        if (cb.nsec && cb_sec_of(st) != cb_sec_of(cbu.lstep))
            cbu.dirty |= 1;
        cbu.lstep = st;
    }
    /* el bailarín: un cuadro por 16avo (al revés con la cinta, quieto si para); el DJ: uno por tiempo */
    d = cbu.dancer;
    if (song.playing && !cbu.push[1]) {  /* (con el freno se queda congelado) */
        static uint32_t last;
        if (st != last) {
            if (cbc.bb) {
                if (!(st % 4u))
                    d = (uint8_t)(d + 1u);
            } else
                d = (uint8_t)(cbc.cinta ? d + 7u : d + 1u);
            last = st;
        }
    }
    /* la tarjeta del acordeón: golpe con cada nota, acento en los acordes */
    l = cbu.card_l;
    if (cb_acc_hit) {
        l = cb_acc_hit >= 2u ? 2u : 1u;
        cb_acc_hit = 0;
        cbu.acc_t = 6;
    } else if (cbu.acc_t && !--cbu.acc_t)
        l = 0;
    /* la de la percusión: lo que manda en la sección; el micrófono con un grito */
    sec = cb_sec_of(st);
    if (cb_grito_on) {
        cb_grito_on = 0;
        cbu.grito_t = 90;
    }
    r = cbu.card_r;
    if (cbu.grito_t) {
        cbu.grito_t--;
        kind = SPR_MICRO_0;
        r = cbu.grito_t > 60u ? (cbu.grito_t / 6u % 2u ? 2u : 1u) : 0u;
    } else {
        uint32_t p = cb.nsec ? cb.sec[sec].perc : PC_NADA;
        uint32_t tim = hits & (1u << 11 | 1u << 12 | 1u << 13), gua = hits & (1u << 7 | 1u << 8 | 1u << 5 | 1u << 6),
                 con = hits & 3u, tam = hits & (1u << 9 | 1u << 10);
        kind = !song.playing || p == PC_NADA ? SPR_BAJO_0 : p == PC_GUIRO || p == PC_CAMPANA ? SPR_GUACHARACA_0
                                                                                            : SPR_CONGAS_0;
        if (cbc.bb)
            kind = SPR_CONGAS_0;
        if (tim)
            kind = SPR_TIMBAL_0;
        if (kind != cbu.rkind)
            r = 0;
        if (kind == SPR_GUACHARACA_0) {
            if (gua)
                r = (uint8_t)((r + 1u) % 4u);
        } else if (kind == SPR_BAJO_0)
            r = song.playing ? (uint8_t)(st / 4u % 2u ? 1u : 0u) : 0u;
        else if (con || tam || tim)
            r = tam || tim ? 2u : 1u;
        else if (st % 2u)
            r = 0;
    }
    if (d != cbu.dancer || l != cbu.card_l || r != cbu.card_r || kind != cbu.rkind)
        cbu.dirty |= 2;
    cbu.dancer = d;
    cbu.card_l = l;
    cbu.card_r = r;
    cbu.rkind = kind;
}
static void cb_ui_draw(void)
{
    static uint32_t last_reb = 0xFF;
    if (cbc.reb != last_reb) {
        last_reb = cbc.reb;
        cbu.dirty |= 1;
    }
    cb_follow();
    if (cbu.dirty & 1u)
        cb_draw_head();
    if (cbu.dirty & 2u)
        cb_draw_panel();
    if (cbu.dirty & 4u)
        cb_draw_foot();
    cbu.dirty = 0;
}
static void cb_ui_boot(void)             /* main.c, antes del ciclo: la primera canción y la pantalla entera */
{
    cbu.fresh = 1;
    cbu.songs = 0;
    cb_generate(1u + (fm1_ticks() & 0xFFFFu));
    cb_tn = CB_KEYS[cbc.key] - 3;
    cb_theme();
    lcd_fill(0, 0, 240, 240, 0);
    lcd_fill(0, 198, 240, 4, 0);
    cbu.dirty = 7;
}
