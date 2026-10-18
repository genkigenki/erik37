/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base on the FM-1: plat.h over felucca-bsp's chip and board layers, the panel map, the USB rings and storage
 * (and on the M-Vave SMK-37, BOARD_SMK37, over boards/smk37: the M-Vave SMK-37 parts are Copyright (C) 2026
 * M. Geisler (genkigenki/makkuro)). Part of the unity build (unity.c), after usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

/* remote control over USB-MIDI (main_fm1.c remote_frame, tools/fm1_remote.py): buttons and keys held, turns and
 * MASTER, added to the panel's own, so a program can drive the app as a player would */
static struct {
    volatile uint32_t btn, keys;
    volatile int32_t enc[NE];
    volatile int32_t master;                     /* -1: the pot's own */
} remote = {0, 0, {0}, -1};

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out | remote.btn;
}

#if BOARD_SMK37
/* the keybed's F3..G5 and the pads as the white keys (the HAL keeps the key each pad pressed); a pad tapped and let go
 * between two calls counts as held for one (its latched press). SMK_PADS_OWN: the keybed only */
static uint8_t smk_pads_own;
uint32_t plat_keys(void)
{
    uint32_t n = fm1_in.knotes;
    if (!smk_pads_own)
        n |= fm1_in.pnotes | smk_pad_edges();
    return (n | remote.keys) & ((1u << NKEYS) - 1u);
}
#else
uint32_t plat_keys(void) { return (fm1_in.notes | remote.keys) & ((1u << NKEYS) - 1u); }
#endif
static uint32_t plat_remote_keys(void) { return remote.keys; }

int32_t plat_enc(int role)
{
    int32_t r;
    if (role < 0 || role >= NE)
        return 0;
    r = remote.enc[role];
    remote.enc[role] = 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role] + r;
}

static volatile uint32_t master_pos = 2048;   /* MASTER's position, 0..4096 (main_fm1.c reads the pot) */
uint32_t plat_master(void) { return master_pos; }

#if BOARD_SMK37
/* ---- the SMK-37 (boards/smk37/fm1_input.h): button LEDs one flag per button id (no dim level: no glow); the keys
 * have no LEDs: the pads light in colour (lcd.c padled_*). SEQ PLAY's LED has two colours: 16 green, 18 red */
#define SMK_LED_PLAY_RED 18u
static void led_pos_init(void) {}
static uint32_t smk_xled;
static uint8_t smk_pad_app;
void plat_smk_leds(uint32_t lit) { smk_xled = lit; }
void plat_pad_lights(int app) { smk_pad_app = (uint8_t)(app != 0); }
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
    static const uint8_t W[SMK_NPADS] = PLAT_WHITE_KEYS;
    uint8_t nl[SMK_NLED] = {0};
    uint32_t i;
    (void)dim_buttons;
    for (i = 0; i < NB; i++)
        if ((buttons >> i) & 1u)
            nl[i == B_PLAY && panel.btn[B_PLAY] == 16u ? SMK_LED_PLAY_RED : panel.btn[i]] = 1;
    if (buttons & LED_PLAY_GREEN)
        nl[panel.btn[B_PLAY]] = 1;
    for (i = 0; i < NSB; i++)                    /* KNOB BANK, FADER BANK, <, >: button ids 0..3 */
        if ((smk_xled >> i) & 1u)
            nl[i] = 1;
    for (i = 0; i < SMK_NLED; i++)
        fm1_led[i] = nl[i];
    if (smk_pad_app)
        return;
    for (i = 0; i < SMK_NPADS; i++) {            /* white key i on its pad, as the layout lays them out */
        uint32_t pad = smk_pad_grid ? i : SMK_PAD_OF_WHITE(i);
        if ((keys >> W[i]) & 1u)
            padled_set(pad, 60, 150, 255);
        else if (glow > 0 && ((dim_keys >> W[i]) & 1u))
            padled_set(pad, 110, 48, 0);
        else
            padled_set(pad, 0, 0, 0);
    }
}
static uint8_t glow_on = 1, play_red;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)    /* PLAY green, unless red (recording) */
{
    uint32_t play = 1u << B_PLAY, b = buttons;
    if (!play_red)
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
    plat_lights(b, keys, 0, 0, 0);
}

uint32_t plat_smk_buttons(void)
{
    return (fm1_in.buttons >> SMK_BTN_KNOB_BANK & 1u) << SB_KNOB_BANK |
           (fm1_in.buttons >> SMK_BTN_FADER_BANK & 1u) << SB_FADER_BANK |
           (fm1_in.buttons >> SMK_BTN_LEFT & 1u) << SB_LEFT | (fm1_in.buttons >> SMK_BTN_RIGHT & 1u) << SB_RIGHT;
}
uint64_t plat_keybed(void)
{
    uint32_t k = fm1__lock();                    /* (64 bits, written by the scan ISR) */
    uint64_t v = fm1_in.keys;
    fm1__unlock(k);
    return v;
}
uint64_t plat_keybed_hits(void) { return smk_keybed_edges(); }
int plat_keybed_vel(int k) { return k >= 0 && k < (int)FM1_NKEY ? fm1_in.key_vel[k] : 0; }
int plat_pedal_raw(void) { return fm1_in.pedal; }
void plat_smk_pads(int layout)
{
    smk_pad_grid = (uint8_t)(layout != SMK_PADS_PIANO);
    if (smk_pads_own != (layout == SMK_PADS_OWN)) {
        smk_pad_edges();                         /* (presses from the other layout: dropped) */
        smk_pads_own = (uint8_t)(layout == SMK_PADS_OWN);
    }
}
/* SMK_PADS_OWN: the presses, as hardware pads (the HAL reports a pad as its white key, here in the grid layout: white
 * key p is pad p) */
uint32_t plat_pad_hits(void)
{
    static const uint8_t W[SMK_NPADS] = PLAT_WHITE_KEYS;
    uint32_t n, out = 0, p;
    if (!smk_pads_own)
        return 0;
    n = smk_pad_edges();
    for (p = 0; p < SMK_NPADS; p++)
        if (n >> W[p] & 1u)
            out |= 1u << p;
    return out;
}
uint32_t plat_pads_held(void)
{
    uint32_t p, out = 0;
    for (p = 0; p < SMK_NPADS; p++)
        if (fm1_in.pad_st[p] == 0x80u)
            out |= 1u << p;
    return out;
}
int plat_fader(int i) { return fm1_in.frames < 200u || (uint32_t)i > 3u ? -1 : (int)fm1_in.fader[i]; }
void plat_pad_rgb(int pad, uint8_t r, uint8_t g, uint8_t b)
{
    if ((uint32_t)pad < SMK_NPADS)
        padled_set((uint32_t)pad, r, g, b);
}

/* the wheels (mux C address 4 = pitch, 5 = mod), self-calibrating: the pitch wheel's rest is its centre (taken once
 * the readings settle, followed slowly while it rests), each wheel's range grows to what it has reached (the travel
 * is short: raw ~445..640 pitch, ~474..629 mod on a unit). Pitch: -127..127 with a dead band around the centre; mod:
 * 0..127, up (lower raw) = more. */
int plat_wheels(int32_t *pitch, int32_t *modw)
{
    enum { DEAD = 12, SPAN0 = 80, MOD_LO = 485, MOD_HI = 615, PMAX = 127 };
    static int32_t pc, plo, phi, mlo, mhi, rest, last_b = -1, last_m = -1;
    int32_t p = fm1_in.wheel[0], m = fm1_in.wheel[1], d, b, mw;
    if (fm1_in.frames < 300u)                    /* (the readings settle) */
        return 0;
    if (!pc) {
        pc = p;
        plo = p - SPAN0;
        phi = p + SPAN0;
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
    }
    if (p < plo)
        plo = p;
    if (p > phi)
        phi = p;
    if (m < mlo)
        mlo = m;
    if (m > mhi)
        mhi = m;
    d = p - pc;
    if (d > -DEAD && d < DEAD) {
        b = 0;
        if (++rest > 200) {                      /* resting: the centre follows a slow drift */
            pc += d > 0 ? 1 : d < 0 ? -1 : 0;
            rest = 0;
        }
    } else {
        rest = 0;
        b = d > 0 ? (d - DEAD) * PMAX / (phi - pc - DEAD > 1 ? phi - pc - DEAD : 1)
                  : (d + DEAD) * PMAX / (pc - plo - DEAD > 1 ? pc - plo - DEAD : 1);
        b = clamp(b, -PMAX, PMAX);
    }
    mw = mhi - m <= DEAD ? 0 : (mhi - m - DEAD) * 127 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1);
    mw = clamp(mw, 0, 127);
    if (b == last_b && mw == last_m)
        return 0;
    last_b = b;
    last_m = mw;
    *pitch = b;
    *modw = mw;
    return 1;
}

/* the factory velocity curves of the keys and pads: USRTRIM (flash 0xC1000, "trimdat" at +0x224), read only, in place
 * (the unencrypted window from 0x9D000 up); smk_trim_load ignores a record that does not check out */
static void smk_trim_init(void)
{
    if (flash_ok)
        smk_trim_load(fm1_xip_ptr(SMK_TRIM_FLASH));
}
#else
/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* the dim ones: felucca-bsp's glow, a short pulse on every scan frame, in two strengths (fm1_led_dim_level) */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
    static int level = -1;
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    if (glow > 0 && glow != level) {
        fm1_led_dim_level(glow == 1 ? 1u : 0u);     /* 1: DIM LO, 2: DIM HI */
        level = glow;
    }
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        int dim = glow > 0 && (i < NB ? (dim_buttons >> i) & 1u : (dim_keys >> (i - NB)) & 1u);
        uint8_t q = led_pos[id];
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (dim)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    /* PLAY's second, green LED: the keyless matrix position column 8, row PA5 (found on the device) */
    if (buttons & LED_PLAY_GREEN)
        nl[8] |= 1u << 1;
    else if (glow > 0 && (dim_buttons & LED_PLAY_GREEN))
        dl[8] |= 1u << 1;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
}
static uint8_t glow_on = 1, play_red;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)    /* PLAY green, unless red (recording); the unlit buttons glow */
{
    uint32_t play = 1u << B_PLAY, b = buttons, dim = glow_on ? ((1u << NB) - 1u) & ~buttons : 0u;
    if (!play_red) {
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
        dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    }
    plat_lights(b, keys, dim, 0, glow_on ? 2 : 0);
}
#endif

/* the 1 kHz tick (app_tick) runs in the timer interrupt: the main loop locks it out around what they share. The lock
 * nests; MIDI sent from the tick itself needs none (nothing that sends interrupts it) */
static volatile uint8_t in_tick;
static uint32_t lock_depth;
uint32_t lock_max, lock_from;                     /* the longest the app kept interrupts off (us), and from where */
static uint32_t lock_t0, lock_ra;
uint32_t plat_lock(void)
{
    fm1_irq_off();
    if (lock_depth == 0) {
        lock_t0 = fm1_ticks();
        lock_ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
    }
    return lock_depth++;
}
void plat_unlock(uint32_t f)
{
    (void)f;
    if (lock_depth && --lock_depth == 0) {
        uint32_t us = (fm1_ticks() - lock_t0) / FM1_TICKS_PER_US;
        if (us > lock_max) {
            lock_max = us;
            lock_from = lock_ra;
        }
        fm1_irq_on();
    }
}
void plat_sysex_send(const uint8_t *frame, uint32_t n) { ota_wire_send(frame, n); }

/* the data area (plat.h): read through the unencrypted XIP window (main_fm1.c fl_plain_window_init) */
const uint8_t *plat_data(uint32_t off)
{
    return flash_ok && FL_IN(off, 1u, PLAT_DATA_LO, PLAT_DATA_HI) ? (const uint8_t *)FL_XIP(off) : 0;
}
#if BOARD_SMK37
/* the SMK-37's data area is the stock USRFLASH: its end marker goes first (storage.c st_stock_release), once */
_Static_assert(PLAT_DATA_LO == FL_BIG_LO && PLAT_DATA_HI == FL_BIG_HI, "SMK-37: plat_data is FL_BIG");
static void plat_data_claim(void)
{
    static uint8_t done;
    if (!done) {
        st_stock_release();
        done = 1;
    }
}
int plat_data_erase(uint32_t off)
{
    if (!flash_ok || (off & 0xFFFu) || !FL_IN(off, 0x1000u, PLAT_DATA_LO, PLAT_DATA_HI))
        return -1;
    plat_data_claim();
    return st_erase(off);
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    if (!flash_ok || !FL_IN(off, n, PLAT_DATA_LO, PLAT_DATA_HI))
        return -1;
    plat_data_claim();
    return st_prog(off, src, n);
}
#else
int plat_data_erase(uint32_t off)
{
    return flash_ok && !(off & 0xFFFu) && FL_IN(off, 0x1000u, PLAT_DATA_LO, PLAT_DATA_HI) ? st_erase(off) : -1;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    return flash_ok && FL_IN(off, n, PLAT_DATA_LO, PLAT_DATA_HI) ? st_prog(off, src, n) : -1;
}
#endif
void plat_yield(void)
{
    ble_poll();                                   /* Bluetooth: its notifications go out while the screen draws */
#if FM1B_WIFI
    wifi_poll();                                  /* WiFi's tasks, once on */
#endif
#ifdef APP_YIELD
    app_yield();
#endif
}

/* MIDI: the rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN. USB and the TRS jack share midi_in_q
 * (both from TIMER5); the jack's packets carry cable 1 (midi_uart.c), which the single-jack USB descriptor never
 * does. Bluetooth has its own queue (radio.c, main loop). */
/* each port's messages, in and out (plat_midi_count): for the app's MIDI lights and counters */
static uint32_t midi_n[NPORTS][2], midi_t[NPORTS][2];
static void midi_counted(int port, int out, uint32_t pkt)
{
    if ((pkt >> 8 & 0xFFu) >= 0xF8u)             /* clock, start, stop, active sensing: not counted, no light */
        return;
    midi_n[port][out]++;
    midi_t[port][out] = plat_ms() | 1u;          /* (0: never) */
}
uint32_t plat_midi_count(int port, int out) { return port >= 0 && port < NPORTS ? midi_n[port][!!out] : 0; }
uint32_t plat_midi_ago(int port, int out)
{
    return port < 0 || port >= NPORTS || !midi_t[port][!!out] ? 0xFFFFFFFFu : plat_ms() - midi_t[port][!!out];
}
int plat_midi_in(uint32_t *pkt, int *port)
{
    int from;
    if (mi_r != mi_w) {
        uint32_t p = midi_in_q[mi_r % MQ];
        RING_PUBLISH();
        mi_r++;
        from = (p >> 4 & 0xFu) == 1u ? PORT_TRS : PORT_USB;
        *pkt = p & ~0xF0u;
    }
    else if (ble_midi_in(pkt))
        from = PORT_BLE;
    else
        return 0;
    if (port)
        *port = from;
    midi_counted(from, 0, *pkt);
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)    /* the main loop's and the tick's rings are shared */
{
    uint32_t f = 0;
    if (!in_tick)
        f = plat_lock();
    if (to & TO_USB) {
        midi_out_event(pkt);
        midi_counted(PORT_USB, 1, pkt);
    }
    if (to & TO_BLE) {
        ble_midi_tap(pkt);
        midi_counted(PORT_BLE, 1, pkt);
    }
    if (!in_tick)
        plat_unlock(f);
}
void plat_midi_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }

/* the keys' velocity and pressure (APP_KEY_SENSE): felucca-bsp's analog rows, as fm1-ctl and fm1-monk read them (the
 * converter's clock, a held key driven, the top row probed). Off, the converter is the main loop's (MASTER). */
#if BOARD_SMK37
void plat_sense(int on) { (void)on; }           /* (the SMK-37's keys have two contacts, timed in the scan) */
uint32_t plat_analog_mask(void) { return 0; }
#else
void plat_sense(int on)
{
    if (!on) {
        fm1_an.con = 0;
        return;
    }
    if (!fm1_an.con) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);  /* the pot's slot comes round in a frame: no dip meanwhile */
        if (a >= 0)
            fm1_an.master = (uint16_t)a;
    }
    fm1_an.drive = 1;
    fm1_an.probe = 1;
    fm1_an.con = (uint16_t)FM1_ADC_CON(7, 2);
}
uint32_t plat_analog_mask(void)
{
    uint32_t m = 0, c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 3; r <= 4u; r++)
            if (FM1_KEYMAP[r][c] >= 14)
                m |= 1u << (FM1_KEYMAP[r][c] - 14);
    return m;
}
#endif

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

static volatile uint32_t audio_cpu_pct, audio_xruns;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }
