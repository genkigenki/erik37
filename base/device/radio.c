/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl) */
/* The radio (FM1B_BLE, FM1B_WIFI): the platform JieLi's libraries run on (the shim in ble/shim asks for interrupts,
 * a clock, delays, a log, a store), a radio's start (its log, the render held while it calibrates), and Bluetooth
 * MIDI (FM1B_BLE): JieLi's BT stack (btctrler.a, btstack.a, wl_rf_common.a: Apache-2.0, binary only), started at
 * every boot, as the stock firmware does, unless the last radio run ended in a crash or a watchdog reset (then by
 * hand: plat_ble_start). WiFi's own part: wifi.c. Part of the unity build (unity.c). A short log and a crash note
 * survive a reset (plat_ble_log_line). */
#include "shim.h"

/* ---- the platform the shim asks for */
/* milliseconds since boot, from TIMER4 (independent of the timer-5 tick). TIMER4 wraps every 2^32 / 24 MHz = 179 s,
 * and the stack's timers and our timeouts must not see time jump back: its deltas are summed (called far more often
 * than every 179 s; fm1-radio's) */
uint32_t shim_irq_save(void);
void shim_irq_restore(uint32_t f);
uint32_t shim_ms(void)
{
    static uint32_t last, acc, ms;
    uint32_t f = shim_irq_save(), now = fm1_ticks(), r;     /* nests: called inside the shim's critical sections */
    acc += now - last;
    last = now;
    if (acc >= 1000u * FM1_TICKS_PER_US) {
        ms += acc / (1000u * FM1_TICKS_PER_US);
        acc %= 1000u * FM1_TICKS_PER_US;
    }
    r = ms;
    shim_irq_restore(f);
    return r;
}

static volatile uint32_t shim_depth;
uint32_t shim_off_max, shim_off_from;            /* the longest the stack kept interrupts off (us), and from where
                                                  * (main_fm1.c's log reads and clears them) */
static uint32_t shim_off_t0, shim_off_ra;
uint32_t shim_irq_save(void)                     /* nests, unlike irq_save(): the shim calls itself */
{
    fm1_irq_off();
    if (shim_depth == 0) {
        shim_off_t0 = fm1_ticks();
        shim_off_ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
    }
    return shim_depth++;
}
void shim_irq_restore(uint32_t f)
{
    (void)f;
    if (shim_depth && --shim_depth == 0) {
        uint32_t us = (fm1_ticks() - shim_off_t0) / FM1_TICKS_PER_US;
        if (us > shim_off_max) {
            shim_off_max = us;
            shim_off_from = shim_off_ra;
        }
        fm1_irq_on();
    }
}
#if SHIM_WIFI
/* the nesting is each task's own, as FreeRTOS keeps it: a task that waits inside a critical section must not leave
 * the interrupts off for the others (os.c switch_to; fm1-radio's) */
uint32_t shim_irq_depth(void) { return shim_depth; }
uint32_t shim_irq_hold(void)
{
    fm1_irq_off();
    return shim_depth;
}
void shim_irq_resume(uint32_t depth)
{
    shim_depth = depth;
    if (!depth)
        fm1_irq_on();
}
int shim_in_irq(void) { return fm1_sp_in_isr(); }   /* a wait asked for there is refused (os.c) */
#else
int shim_in_irq(void) { return 0; }
#endif
volatile uint32_t shim_isr_blocks;               /* waits refused in an interrupt (os.c; with SHIM_WIFI only) */
/* every task waits: run what is due (a timer may be what they wait for) and keep the watchdog fed,
 * whichever context we are in (btstack_init waits in the main loop's) */
static void radio_stuck_check(void);
static void (*radio_idle_hook)(void);             /* while every task waits: what must still be answered (USB) */
void shim_idle(void)
{
    static uint8_t in;
    fm1_wdt_feed();
    if (!in) {
        in = 1;
#if SHIM_WIFI
        shim_timers_post();                       /* timers to their owners: never a callback inside a task's wait */
#else
        shim_timers();
#endif
        radio_stuck_check();
        if (radio_idle_hook)
            radio_idle_hook();
        in = 0;
    }
}
void shim_reboot(void) { fm1_reboot(); }
/* the shim's delays (JieLi's delay_us and friends): from RAM, timed by TIMER4, nothing called */
__attribute__((section(".ram_text"), noinline, used)) void shim_delay_us(uint32_t us) { fm1_busy_us_inl(us); }


/* the radio's interrupts sit above the input timer (4): JieLi asks for 0..2, mapped to 5..7 */
static void hex8(char *b, uint32_t v);
void shim_request_irq(int index, int prio, void (*handler)(void))
{
    uint32_t f;
    {   /* on the log: which source, at what priority, which handler (it goes in the vector table as it is) */
        char b[32] = "irq ";
        b[4] = (char)('0' + index / 100 % 10);
        b[5] = (char)('0' + index / 10 % 10);
        b[6] = (char)('0' + index % 10);
        b[7] = ' ';
        b[8] = (char)('0' + (prio & 7));
        b[9] = ' ';
        hex8(b + 10, (uint32_t)(uintptr_t)handler);
        b[18] = 0;
        shim_log(b);
    }
    f = irq_save();
    fm1_guard_unlock_top();
    fm1_irq_attach((uint32_t)index, handler, (uint32_t)(prio + 5 > 7 ? 7 : prio + 5));
    fm1_guard_lock_top();
    irq_restore(f);
}

/* a short log for the screen, kept through a reset (.noinit): after a crash the app can show
 * what the last run said, and a crash note */
#define BLE_LOG_N 32
#define BLE_LOG_MAGIC 0x424C4532u                 /* "BLE2" */
static struct {
    uint32_t magic, w, crash_seen, running, repeat, t0_up;   /* running: a BT run was on (cleared at boot) */
    char line[BLE_LOG_N][40];
} blog __attribute__((section(".noinit")));
void shim_wdt(void) { fm1_wdt_feed(); }
#if FM1B_WIFI
static void wifi_log_watch(const char *s);
#endif
void shim_log(const char *s)
{
#if FM1B_WIFI
    wifi_log_watch(s);
#endif
    char *d = blog.line[blog.w % BLE_LOG_N];
    uint32_t i;
    if (blog.magic != BLE_LOG_MAGIC)
        return;
    if (blog.w) {                                 /* the same start as the last line: fold it, "xN" */
        char *p = blog.line[(blog.w - 1u) % BLE_LOG_N];
        for (i = 0; i < 5u && s[i] && s[i] == p[i]; i++)
            ;
        if (i == 5u || (!s[i] && !p[i])) {
            uint32_t n = ++blog.repeat + 1u, k = 0;
            char num[12];
            for (i = 0; i < 26u && s[i]; i++)
                p[i] = s[i] >= 32 && s[i] < 127 ? s[i] : ' ';
            p[i++] = ' ';
            p[i++] = 'x';
            do {
                num[k++] = (char)('0' + n % 10u);
                n /= 10u;
            } while (n && k < 10u);
            while (k)
                p[i++] = num[--k];
            p[i] = 0;
            return;
        }
    }
    blog.repeat = 0;
    for (i = 0; i < 39u && s[i]; i++)
        d[i] = s[i] >= 32 && s[i] < 127 ? s[i] : ' ';
    d[i] = 0;
    blog.w++;
}
static void hex8(char *b, uint32_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15u];
}
static char *put_dec(char *b, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

/* at boot: what ended the last run. The crash note: the fault's pc, vector and debug register, its
 * return address and how long Bluetooth had run, and the core's last four branches (ETM). */
static uint8_t radio_last_bad;                      /* the last BT run ended in a crash or a watchdog reset */
static void radio_boot_note(void)
{
    char b[40];
    int crashed = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != blog.crash_seen;
    if (blog.magic != BLE_LOG_MAGIC || blog.w > 100000u) {
        if (!crashed)
            return;
        blog.magic = BLE_LOG_MAGIC;               /* a crash with Bluetooth never started: note it too */
        blog.w = blog.running = blog.t0_up = 0;
    }
    if (crashed) {
        char *q;
        blog.crash_seen = fm1_crash.count;
        q = put_dec(b + 6, fm1_crash.vec);
        b[0] = 'c'; b[1] = 'r'; b[2] = 'a'; b[3] = 's'; b[4] = 'h'; b[5] = ' ';
        q[0] = ' '; q[1] = 'p'; q[2] = 'c'; q[3] = ' ';
        hex8(q + 4, fm1_crash.pc);
        q[12] = ' '; q[13] = 'd'; q[14] = ' ';
        hex8(q + 15, fm1_crash.dbg);
        q[23] = 0;
        shim_log(b);
        b[0] = 'r'; b[1] = 'e'; b[2] = 't'; b[3] = ' ';
        hex8(b + 4, fm1_crash.rets);
        b[12] = ' '; b[13] = 'm'; b[14] = 's'; b[15] = ' ';
        put_dec(b + 16, blog.running ? fm1_crash.uptime_ms - blog.t0_up : 0u);
        shim_log(b);
        b[0] = 'e'; b[1] = ' ';
        hex8(b + 2, fm1_crash.etm[0]);
        b[10] = ' ';
        hex8(b + 11, fm1_crash.etm[1]);
        b[19] = ' ';
        hex8(b + 20, fm1_crash.etm[2]);
        b[28] = ' ';
        hex8(b + 29, fm1_crash.etm[3]);
        b[37] = 0;
        shim_log(b);
    } else if (blog.running && (fm1_boot.p3_rst & 4u)) {
        extern uint32_t shim_task_now;
        static const char *const T[4] = {"main", "1", "2", "3"};
        char *q = b;
        const char *t = "reset by the watchdog: task ";
        while (*t)
            *q++ = *t++;
        t = T[shim_task_now & 3u];
        while (*t)
            *q++ = *t++;
        *q = 0;
        shim_log(b);
    }
    radio_last_bad = (uint8_t)(blog.running && (crashed || (fm1_boot.p3_rst & 4u)));
    blog.running = 0;
}

/* an intentional restart (an update, the chip's update mode): the next boot does not take it for a crash */
static void radio_clean_exit(void) { blog.running = 0; }

/* the radio's address and pairing: the app's store object OBJ_BLE (app.h) */
int shim_store_load(void *dst, uint32_t max) { return plat_store_load(OBJ_BLE, dst, max); }
int shim_store_save(const void *src, uint32_t len) { return plat_store_save(OBJ_BLE, src, len); }

extern int syscfg_read(uint16_t id, void *buf, uint16_t len);
extern int syscfg_write(uint16_t id, void *buf, uint16_t len);
/* the BT address: the one kept, or one made from the chip's unique data (the SDK's own fallback is
 * a random one, kept) */
#define CFG_BT_MAC_ADDR 1                         /* include_lib/utils/syscfg/syscfg_id.h */
const uint8_t *bt_get_mac_addr(void)
{
    static uint8_t mac[6];
    static const uint8_t none[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t i, h = 2166136261u;
    if (syscfg_read(CFG_BT_MAC_ADDR, mac, 6) == 6 && memcmp(mac, none, 6))
        return mac;
    h ^= fm1_ticks();
    for (i = 0; i < 6u; i++) {
        h = (h ^ (h >> 13)) * 16777619u + fm1_ticks();
        mac[i] = (uint8_t)(h >> 8);
    }
    mac[0] = (uint8_t)((mac[0] & 0xFC) | 0xC0);   /* a static random address */
    syscfg_write(CFG_BT_MAC_ADDR, mac, 6);
    return mac;
}


/* ---- a radio's start (Bluetooth's, WiFi's): its log, the render held while it calibrates */
static uint32_t radio_t0;
static uint8_t radio_starting;                    /* started, not ready yet (radio_settle) */
/* BLE_MASK_AT_START: mask the input timer while the radio calibrates. fm1-ble needed it while the RF
 * trim ran from the flash (the real cause of its crashes, notes/06); with the trim in RAM it should
 * not be, and without it keys, USB and the screen do not pause when Bluetooth starts. */
#ifndef BLE_MASK_AT_START
#define BLE_MASK_AT_START 0
#endif
static uint8_t irqs_masked;
/* radio_quiet: the render holds (app_quiet, or silence) for the first second of the start. The radio's trim
 * draws ~500 ms of high current, and what is read from the flash meanwhile can come back corrupted (notes/06
 * of fm1-ble): the trim itself runs from RAM, but CTL-1's GM synth read its code and 200 KB of samples from
 * the flash in the audio interrupt, ~30% of the time, and crashed there 0.2 s into the start. */
static volatile uint8_t radio_quiet;
#if SHIM_WIFI
extern int os_taskq_pend(const char *fmt, int *argv, int argc);
/* the SDK's "sys_timer" task: timers added to it (and the WiFi library's posts) arrive as [Q_CALLBACK, fn, args],
 * which os_taskq_pend runs */
static void sys_timer_task(void *p)
{
    int m[8];
    (void)p;
    for (;;)
        os_taskq_pend("taskq", m, 8);
}
#endif
static void radio_begin(const char *what)
{
    static uint8_t os_up;
    if (fm1_crash.magic != FM1_CRASH_MAGIC)
        fm1_crash.count = 0;
    if (blog.magic != BLE_LOG_MAGIC || blog.w > 100000u)
        blog.w = 0;                               /* (else the log goes on: the last run's crash note stays readable) */
    blog.magic = BLE_LOG_MAGIC;
    blog.crash_seen = fm1_crash.count;
    blog.running = 1;
    blog.t0_up = fm1_uptime_ms();                 /* (the crash note's clock) */
    radio_t0 = shim_ms();
    radio_starting = 1;
    shim_log(what);
    radio_quiet = 1;
#if BLE_MASK_AT_START
    {
        uint32_t f = irq_save();
        fm1_irq_mask(FM1_IRQ_TIMER5);
        irq_restore(f);
        irqs_masked = 1;
    }
#endif
    if (!os_up) {                                 /* the shim, once (both radios share it) */
        os_up = 1;
        shim_os_init();
        fm1_guard_stack_floor(shim_stacks_lo());  /* the tasks' stacks are in the pool */
#if SHIM_WIFI
        task_create(sys_timer_task, 0, "sys_timer");
#endif
    }
}
/* the main loop: the render back a second into the start once the radio is ready (or after 15 s), and the input
 * timer if it was masked */
static void radio_settle(int ready)
{
    uint32_t f;
    if (ready)
        radio_starting = 0;
    else if (shim_ms() - radio_t0 <= 15000u)
        return;
    if (radio_quiet && shim_ms() - radio_t0 > 1000u) {
        radio_quiet = 0;
        shim_log(ready ? "sound back: ready" : "sound back: 15 s, not ready");
    }
    if (!irqs_masked)
        return;
    irqs_masked = 0;
    f = irq_save();
    fm1_irq_unmask(FM1_IRQ_TIMER5);
    irq_restore(f);
    shim_log(ready ? "irqs back: ready" : "irqs back: 15 s, not ready");
}
extern void shim_os_dump(void);
static void radio_stuck_check(void)               /* still starting after 3 s: say what each task waits for */
{
    static uint32_t said;
    if (radio_starting && shim_ms() - radio_t0 > 3000u && said != radio_t0) {
        said = radio_t0;
        shim_log("still starting after 3 s:");
        shim_os_dump();
    }
}

#if FM1B_BLE
/* ---- Bluetooth MIDI */
static volatile uint8_t ble_state;

/* ---- MIDI both ways (ble/midi/le_midi.c); everything here runs in the main loop */
extern int ble_midi_send_many(const uint32_t *pkts, const uint16_t *ms, int n);
extern int ble_midi_connected(void);
static uint32_t ble_rx_count, ble_tx_count;
static uint8_t ble_conn;                          /* ble_midi_connected(), as of the last poll */
#define BQ 64u
static uint32_t ble_in_q[BQ];
static uint32_t bi_w, bi_r;
void ble_midi_receive(uint32_t pkt)               /* from the stack (in ble_poll): for plat_midi_in */
{
    if (bi_w - bi_r < BQ) {
        ble_in_q[bi_w % BQ] = pkt & ~0xF0u;
        bi_w++;
        ble_rx_count++;
    }
}
static int ble_midi_in(uint32_t *pkt)
{
    if (bi_r == bi_w)
        return 0;
    *pkt = ble_in_q[bi_r % BQ];
    bi_r++;
    return 1;
}
/* out: every message with the millisecond it was queued (fm1_ms), which goes with it as its BLE MIDI
 * timestamp. MIDI clock waits in its own short ring (a few ticks at most: later than that they are no
 * use) so a stream of clock never holds up notes; the drain merges the two in time order. */
static uint32_t ble_out_q[BQ];
static uint16_t ble_out_ms[BQ];
static uint32_t bo_w, bo_r;
#define BCQ 4u
static uint16_t ble_clk_ms[BCQ];
static volatile uint32_t bc_w, bc_r;
static void ble_midi_tap(uint32_t pkt)            /* main loop (locked) or the tick; nothing queues for a central not there */
{
    uint16_t ms = (uint16_t)(fm1_ms & 0x1FFFu);
    if (ble_state != BLE_READY || !ble_conn)
        return;
    if (((pkt >> 8) & 0xFFu) == 0xF8u) {
        if (bc_w - bc_r < BCQ) {
            ble_clk_ms[bc_w % BCQ] = ms;
            RING_PUBLISH();
            bc_w++;
        }
        return;
    }
    if (bo_w - bo_r < BQ) {
        ble_out_q[bo_w % BQ] = pkt;
        ble_out_ms[bo_w % BQ] = ms;
        RING_PUBLISH();
        bo_w++;
    }
}
static int ms_before(uint16_t a, uint16_t b) { return ((uint32_t)(b - a) & 0x1FFFu) < 4096u && a != b; }
static void ble_midi_drain(void)                  /* main loop: what waits, as many notifications as the stack takes */
{
    int round;
    for (round = 0; round < 4; round++) {
        uint32_t pkts[16], n = 0, ni = 0, ci, nw, cw, sent, k;
        uint16_t ms[16];
        uint8_t is_clk[16];
        uint32_t f = plat_lock();                 /* a snapshot: the tick may add meanwhile */
        nw = bo_w;
        cw = bc_w;
        plat_unlock(f);
        ci = bc_r;
        ni = bo_r;
        while (n < 16u && (ni != nw || ci != cw)) {
            int take_clk = ci != cw && (ni == nw || ms_before(ble_clk_ms[ci % BCQ], ble_out_ms[ni % BQ]));
            if (take_clk) {
                pkts[n] = 0xFu | 0xF8u << 8;
                ms[n] = ble_clk_ms[ci % BCQ];
                is_clk[n++] = 1;
                ci++;
            } else {
                pkts[n] = ble_out_q[ni % BQ];
                ms[n] = ble_out_ms[ni % BQ];
                is_clk[n++] = 0;
                ni++;
            }
        }
        if (!n)
            return;
        sent = (uint32_t)ble_midi_send_many(pkts, ms, (int)n);
        if (!sent)
            return;                               /* the stack is full: what did not go stays for the next poll */
        for (k = 0; k < sent; k++) {
            if (is_clk[k])
                bc_r++;
            else
                bo_r++;
        }
        RING_PUBLISH();
        ble_tx_count += sent;
    }
}

/* ---- the stack */
static volatile uint32_t ble_events;
extern void btstack_init(void);
extern void bt_ble_init(void);
extern void lmp_set_sniff_disable(void);
extern void bt_max_pwr_set(uint8_t pwr, uint8_t pg_pwr, uint8_t iq_pwr, uint8_t ble_pwr);
#define BT_TX_POWER 8                             /* the SDK's default (classic, unused) */
#define BLE_TX_POWER 11                           /* the top: -64 dBm at 6 where the stock firmware gives -39 */
extern void lib_make_ble_address(uint8_t *ble, uint8_t *edr);
extern int le_controller_set_mac(void *addr);

struct bt_event_s { uint8_t event; uint8_t args[7]; uint32_t value; };
void shim_bt_event(int from, const void *event, int len)
{
    const struct bt_event_s *e = event;
    (void)len;
    ble_events++;
    if (from == 3 && e && e->event == 3 && ble_state == BLE_STARTING) {   /* BT_EVENT_FROM_CON, BT_STATUS_INIT_OK */
        shim_log("BT ready: advertising as FM-1");
        {
            extern uint32_t shim_heap_peak;
            char b[32] = "heap peak ", *q = put_dec(b + 10, shim_heap_peak);
            q[0] = ' '; q[1] = 'o'; q[2] = 'f'; q[3] = ' ';
            put_dec(q + 4, 24u * 1024u);
            shim_log(b);
        }
        bt_ble_init();
        ble_state = BLE_READY;
    }
}

static void ble_start(void)
{
    uint8_t addr[6];
    if (ble_state != BLE_OFF)
        return;
    ble_state = BLE_STARTING;
    radio_begin("BT: starting");
    lmp_set_sniff_disable();
    /* the radio's transmit power, 0..11: the SDK sets it in its config start-up (apps/common/config/
     * user_cfg.c, cfg_file_parse: classic 8, BLE 6), which the shim does not run; left alone the
     * library transmits at its lowest (measured: -79 dBm where the stock firmware gives -39) */
    bt_max_pwr_set(BT_TX_POWER, 6, 6, BLE_TX_POWER);
    lib_make_ble_address(addr, (uint8_t *)bt_get_mac_addr());
    le_controller_set_mac(addr);
    btstack_init();
}

/* the main loop's turn: timers, then every BT task that can run */
static void ble_poll(void)
{
    if (ble_state == BLE_OFF)
        return;
    shim_timers();
    shim_run();
    ble_conn = (uint8_t)(ble_state == BLE_READY && ble_midi_connected());
    if (!ble_conn) {
        bo_r = bo_w;                              /* a central gone: nothing stale for the next one */
        bc_r = bc_w;
    }
    ble_midi_drain();
#if FM1B_BLE_CENTRAL
    {
        extern void ble_midi_poll(void);
        ble_midi_poll();                          /* a connection that never answers, given up */
    }
#endif
    radio_stuck_check();
    radio_settle(ble_state == BLE_READY);
    {   /* ten seconds after it is ready, once: the tasks' deepest stack so far (sizing; 8 KiB each) */
        static uint8_t said;
        extern int shim_stack_depth(int task);
        if (ble_state == BLE_READY && !said && shim_ms() - radio_t0 > 10000u) {
            char b[40] = "stacks ", *q = b + 7;
            int k;
            said = 1;
            for (k = 1; k <= 3; k++) {
                q = put_dec(q, (uint32_t)shim_stack_depth(k) * 256u);
                *q++ = k < 3 ? ' ' : 0;
            }
            q[-1] = 0;
            shim_log(b);
        }
    }
}

/* ---- plat.h */
extern int ble_midi_peripheral_connected(void);
extern int ble_midi_peripheral_linked(void);
extern void ble_midi_enable(int on);
static uint8_t ble_off;                           /* switched off by the player (plat_ble_enable) */
int plat_ble_state(void) { return ble_off ? BLE_OFF : ble_state; }
int plat_ble_connected(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_connected(); }
int plat_ble_linked(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_linked(); }
void plat_ble_enable(int on)
{
    ble_off = (uint8_t)!on;
    if (on && ble_state == BLE_OFF)
        ble_start();                              /* (off at power-on, or after a crash) */
    else if (ble_state == BLE_READY)
        ble_midi_enable(on);
    if (!on)
        shim_log("switched off");
}
#if FM1B_BLE_CENTRAL
extern void ble_midi_scan(int on);
extern int ble_midi_found(int i, char *name, int *rssi);
extern int ble_midi_connect(int i);
extern void ble_midi_disconnect(void);
extern int ble_midi_central_state(void);
void plat_ble_scan(int on)
{
    if (ble_state == BLE_READY)
        ble_midi_scan(on);
}
int plat_ble_found(int i, char *name, int *rssi) { return ble_state == BLE_READY && ble_midi_found(i, name, rssi); }
int plat_ble_connect(int i) { return ble_state == BLE_READY ? ble_midi_connect(i) : -1; }
void plat_ble_disconnect(void)
{
    if (ble_state == BLE_READY)
        ble_midi_disconnect();
}
int plat_ble_device(void) { return ble_state == BLE_READY ? ble_midi_central_state() : BLE_DEV_OFF; }
#else
void plat_ble_scan(int on) { (void)on; }
int plat_ble_found(int i, char *name, int *rssi) { (void)i, (void)name, (void)rssi; return 0; }
int plat_ble_connect(int i) { (void)i; return -1; }
void plat_ble_disconnect(void) {}
int plat_ble_device(void) { return BLE_DEV_OFF; }
#endif
void plat_ble_start(void) { ble_start(); }
#else
#include "ble_none.c"
#endif

/* ---- plat.h: the radio's log */
void plat_log(const char *line) { shim_log(line); }
const char *plat_ble_log_line(int back)
{
    if (blog.magic != BLE_LOG_MAGIC || back < 0 || (uint32_t)back >= blog.w || back >= BLE_LOG_N)
        return "";
    return blog.line[(blog.w - 1u - (uint32_t)back) % BLE_LOG_N];
}
uint32_t plat_ble_log_total(void) { return blog.magic == BLE_LOG_MAGIC && blog.w <= 100000u ? blog.w : 0u; }
