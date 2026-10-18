/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The WiFi example's network code: an HTTP GET in a task of its own (plat_net_task), on lwIP's sockets. Built as
 * the SDK builds code that talks lwIP (fm1.toml net), apart from the app's unity build. */
#include <stdint.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "plat.h"

static volatile int busy;
static volatile uint32_t got;
static char host_[64], path_[96], result[40];

static void set_result(const char *s)
{
    unsigned i;
    for (i = 0; i + 1u < sizeof result && s[i] && s[i] != '\r' && s[i] != '\n'; i++)
        result[i] = s[i];
    result[i] = 0;
}

static void fetch(void *p)
{
    static char buf[512];
    struct hostent *he;
    struct sockaddr_in a;
    int s, n, first = 1;
    (void)p;
    he = gethostbyname(host_);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        set_result("no address for the host");
        busy = 0;
        return;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        set_result("no socket");
        busy = 0;
        return;
    }
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(80);
    memcpy(&a.sin_addr, he->h_addr_list[0], 4);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        closesocket(s);
        set_result("could not connect");
        busy = 0;
        return;
    }
    {
        int ms = 5000;                          /* a silent server ends it after 5 s */
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    }
    n = 0;
    {
        const char *parts[] = {"GET ", path_, " HTTP/1.0\r\nHost: ", host_, "\r\nConnection: close\r\n\r\n"};
        unsigned i;
        for (i = 0; i < 5u; i++) {
            size_t l = strlen(parts[i]);
            memcpy(buf + n, parts[i], l);
            n += (int)l;
        }
    }
    send(s, buf, (size_t)n, 0);
    while ((n = recv(s, buf, sizeof buf - 1u, 0)) > 0) {
        if (first) {
            buf[n] = 0;
            set_result(buf);                    /* "HTTP/1.0 200 OK" */
            first = 0;
        }
        got += (uint32_t)n;
    }
    closesocket(s);
    if (first)
        set_result("no answer");
    busy = 0;
}

int net_fetch(const char *host, const char *path)
{
    unsigned i;
    if (busy)
        return -1;
    for (i = 0; i + 1u < sizeof host_ && host[i]; i++)
        host_[i] = host[i];
    host_[i] = 0;
    for (i = 0; i + 1u < sizeof path_ && path[i]; i++)
        path_[i] = path[i];
    path_[i] = 0;
    got = 0;
    set_result("fetching");
    busy = 1;
    if (plat_net_task(fetch, 0, "fetch")) {
        busy = 0;
        set_result("no task");
        return -1;
    }
    return 0;
}
int net_busy(void) { return busy; }
const char *net_result(void) { return result; }
uint32_t net_bytes(void) { return got; }
