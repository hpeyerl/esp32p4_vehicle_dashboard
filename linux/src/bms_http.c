// =============================================================
//  bms_http.c — poll the BMW i3 BMS web API and fill g_bms
//
//  Minimal HTTP/1.0 GET over a raw socket (no libcurl dependency)
//  + cJSON parse. Bounded connect/read timeout so a missing BMS
//  never stalls the UI for long. Single-threaded: called from the
//  main loop, writes g_bms directly.
// =============================================================
#include "bms_http.h"
#include "bms_data.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

static const char *bms_host(void)
{
    const char *h = getenv("BMS_HOST");
    return (h && *h) ? h : "i3bms.beer.org";
}

// Non-blocking connect with a timeout. Returns a connected fd or -1.
static int connect_timeout(const char *host, const char *port, int ms)
{
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) { fcntl(fd, F_SETFL, fl); break; }
        if (rc < 0 && errno == EINPROGRESS) {
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
            if (select(fd + 1, NULL, &wf, NULL, &tv) > 0) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err == 0) { fcntl(fd, F_SETFL, fl); break; }
            }
        }
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

// GET path from host; returns malloc'd body (caller frees) or NULL.
static char *http_get_body(const char *host, const char *path, int ms)
{
    int fd = connect_timeout(host, "80", ms);
    if (fd < 0) return NULL;

    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    char req[256];
    int n = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (write(fd, req, n) != n) { close(fd); return NULL; }

    size_t cap = 16384, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        if (len + 2048 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); close(fd); return NULL; }
            buf = nb;
        }
        ssize_t r = read(fd, buf + len, cap - len - 1);
        if (r > 0) len += (size_t)r;
        else break;               // EOF or timeout
    }
    close(fd);
    buf[len] = 0;

    char *sep = strstr(buf, "\r\n\r\n");
    if (!sep) { free(buf); return NULL; }
    char *body = strdup(sep + 4);
    free(buf);
    return body;
}

static double jnum(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(i) ? i->valuedouble : 0.0;
}
static bool jbool(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsBool(i) ? cJSON_IsTrue(i) : false;
}

void bms_http_poll(uint32_t now_ms, bool active)
{
    static uint32_t last = 0;
    static bool     first = true;

    if (!active) return;
    if (!first && (uint32_t)(now_ms - last) < 2000) return;
    first = false;
    last  = now_ms;

    char *body = http_get_body(bms_host(), "/api/data", 500);
    if (!body) return;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return;

    BmsData t;
    memset(&t, 0, sizeof t);
    t.packV        = (float)jnum(root, "packV");
    t.soc          = (float)jnum(root, "soc");
    t.lowCell      = (float)jnum(root, "lowCell");
    t.highCell     = (float)jnum(root, "highCell");
    t.avgTemp      = (float)jnum(root, "avgTemp");
    t.currentA     = (float)jnum(root, "currentA");
    t.faulted      = jbool(root, "faulted");
    t.chargerActive= jbool(root, "chargerActive");

    const cJSON *mods = cJSON_GetObjectItemCaseSensitive(root, "modules");
    int mi = 0;
    if (cJSON_IsArray(mods)) {
        const cJSON *m;
        cJSON_ArrayForEach(m, mods) {
            if (mi >= BMS_MAX_MODULES) break;
            BmsModule *bm = &t.modules[mi];
            bm->addr    = (uint8_t)jnum(m, "addr");
            bm->voltage = (float)jnum(m, "voltage");
            bm->t1      = (int16_t)jnum(m, "t1");
            bm->t2      = (int16_t)jnum(m, "t2");
            bm->faulted = jbool(m, "faulted");
            const cJSON *cells = cJSON_GetObjectItemCaseSensitive(m, "cells");
            int ci = 0;
            if (cJSON_IsArray(cells)) {
                const cJSON *c;
                cJSON_ArrayForEach(c, cells) {
                    if (ci >= BMS_MAX_CELLS) break;
                    bm->cells[ci++] = cJSON_IsNumber(c) ? (float)c->valuedouble : 0.0f;
                }
            }
            bm->num_cells = (uint8_t)ci;
            mi++;
        }
    }
    t.numModules = (uint8_t)mi;
    cJSON_Delete(root);

    t.valid          = true;
    t.last_update_ms = now_ms;
    g_bms = t;      // single-threaded publish
}
