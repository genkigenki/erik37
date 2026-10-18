/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Host test of device/wifi.c's scans against a stand-in for JieLi's library (no hardware): every list
 * wifi_get_scan_result hands over (a copy from the heap, the caller's to free, as the SDK's demos do) is freed; scan
 * after scan still finds the networks; a scan asked while the library is busy (wifi_scan_req -1: scanning or joining)
 * is asked again; a list the library could not copy (no memory) leaves the last one.
 *   cc -Itests/wifi_stub -o wifi_scan_test tests/wifi_scan_test.c && ./wifi_scan_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the platform as wifi.c sees it */
enum { OBJ_PANEL, OBJ_WIFI, OBJ_NOBJ };
enum { WIFI_OFF, WIFI_STARTING, WIFI_ON, WIFI_JOINING, WIFI_ADDRESS, WIFI_CONNECTED, WIFI_FAILED };
static uint32_t now_ms;
uint32_t shim_ms(void) { return now_ms; }
void shim_log(const char *s) { (void)s; }
void shim_timers(void) {}
void shim_run(void) {}
static void radio_stuck_check(void) {}
static void radio_settle(int on) { (void)on; }
static void radio_begin(const char *s) { (void)s; }
static void radio_clean_exit(void) {}
static uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t *bt_get_mac_addr(void) { return mac; }
int task_create(void (*fn)(void *), void *p, const char *name) { (void)fn, (void)p, (void)name; return 0; }
static int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj, (void)dst, (void)max; return -1; }
static int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj, (void)src, (void)len; return 0; }

/* ---- the library's stand-in */
static int (*ev_cb)(void *, int);
static int lib_busy, lib_nomem, lib_reqs, lib_live, lib_allocs;
struct wifi_scan_ssid_info;
static void *lib_list(unsigned *cnt);
int wifi_on(void) { return 0; }
int wifi_off(void) { return 0; }
int wifi_is_on(void) { return 1; }
int wifi_enter_sta_mode(char *a, char *b) { (void)a, (void)b; return 0; }
void wifi_set_event_callback(int (*cb)(void *, int)) { ev_cb = cb; }
void wifi_set_store_ssid_cnt(uint32_t c) { (void)c; }
void wifi_set_sta_connect_timeout(int s) { (void)s; }
struct wifi_store_info;
int wifi_set_default_mode(struct wifi_store_info *p, char f, char s) { (void)p, (void)f, (void)s; return 0; }
char wifi_get_rssi(void) { return -50; }
int wifi_scan_req(void) { lib_reqs++; return lib_busy ? -1 : 0; }
void wifi_clear_scan_result(void) {}
void wifi_set_tx_rate_control_tab(uint32_t t) { (void)t; }
struct netif_info;
void lwip_get_netif_info(uint8_t w, struct netif_info *i) { (void)w, (void)i; }
void wifi_set_frame_cb(void (*cb)(void *, void *, void *, uint32_t, void *), void *p) { (void)cb, (void)p; }
struct wifi_scan_ssid_info *wifi_get_scan_result(uint32_t *cnt) { return lib_list(cnt); }
static void test_free(void *p)                    /* wifi.c's free: count the lists given back */
{
    if (p)
        lib_live--;
    free(p);
}
#define free test_free
#include "../device/wifi.c"
#undef free
static void *lib_list(unsigned *cnt)              /* three networks, a fresh copy each time (as the library's) */
{
    static const char *const N[3] = {"Home", "Cafe", "Office"};
    struct wifi_scan_ssid_info *r;
    int i;
    if (lib_nomem) {
        *cnt = 0;
        return 0;
    }
    r = calloc(32, sizeof *r);
    lib_allocs++;
    lib_live++;
    for (i = 0; i < 3; i++) {
        memcpy(r[i].ssid, N[i], strlen(N[i]));
        r[i].ssid_len = (unsigned)strlen(N[i]);
        r[i].rssi = (char)(-40 - 10 * i);
        r[i].enc = (char)(i != 1);
    }
    *cnt = 3;
    return r;
}

static int bad;
static void check(const char *what, int ok) { printf("wifi scan: %-74s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static void run(uint32_t ms)
{
    while (ms--) {
        now_ms++;
        wifi_poll();
    }
}
static void lib_scan_done(void) { ev_cb(0, WIFI_EVENT_STA_SCAN_COMPLETED); }

int main(void)
{
    char ssid[33];
    int i, n;
    uint32_t seen;
    plat_wifi_on(1);
    ev_cb(0, WIFI_EVENT_MODULE_INIT);
    ev_cb(0, WIFI_EVENT_STA_START);
    check("on, no network kept: WIFI_ON", plat_wifi_state() == WIFI_ON);
    for (i = 0; i < 10; i++) {                     /* scan after scan */
        seen = plat_wifi_scans();
        plat_wifi_scan();
        lib_scan_done();
        run(10);
        n = 0;
        while (plat_wifi_found(n, ssid, 0, 0))
            n++;
        if (n != 3 || plat_wifi_scans() == seen)
            break;
    }
    check("ten scans in a row: each finds the three networks", i == 10);
    check("every list the library copied is freed (no heap lost per scan)", lib_allocs == 10 && lib_live == 0);
    plat_wifi_found(0, ssid, 0, 0);
    check("the strongest first", !strcmp(ssid, "Home"));
    /* the library busy (joining, or its own scan): the scan is asked again until it is taken */
    lib_busy = 1;
    lib_reqs = 0;
    check("a scan asked while the library is busy: taken (0), not lost", plat_wifi_scan() == 0);
    run(2500);
    check("... asked again while it stays busy", lib_reqs >= 3);
    lib_busy = 0;
    n = lib_reqs;
    run(1100);
    check("... and once more when it is free, then no more", lib_reqs == n + 1);
    seen = plat_wifi_scans();
    lib_scan_done();
    run(1500);
    check("... its results arrive", plat_wifi_scans() == seen + 1 && plat_wifi_found(2, ssid, 0, 0) && lib_reqs == n + 1);
    /* the library cannot copy its list (no memory): the last list stays, and the scan is asked again */
    lib_nomem = 1;
    seen = plat_wifi_scans();
    lib_scan_done();
    run(10);
    check("no list (no memory): the last one stays, not an empty one",
          plat_wifi_found(2, ssid, 0, 0) && plat_wifi_scans() == seen);
    lib_nomem = 0;
    n = lib_reqs;
    run(1100);
    check("... the scan is asked again", lib_reqs == n + 1);
    lib_scan_done();
    run(10);
    check("... and then found", plat_wifi_scans() == seen + 1 && lib_live == 0);
    printf("%s\n", bad ? "WIFI SCAN TEST FAILED" : "wifi scan test passed");
    return bad != 0;
}
