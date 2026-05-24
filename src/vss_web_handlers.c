// =============================================================
//  vss_web_handlers.c — VSS calibration web UI handlers
//
//  Add these to ota_server.c (or include this file from it).
//
//  Registers two additional URI handlers on the existing httpd:
//    GET  /vss          — calibration UI page
//    POST /vss/set      — update calibration from form
//
//  Call vss_register_handlers(server) after httpd_start() in
//  prv_httpd_start() in ota_server.c.
// =============================================================

#include "vss_sensor.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG_VSS_WEB = "vss_web";

// ── VSS calibration page ──────────────────────────────────────────────────────
static esp_err_t prv_get_vss(httpd_req_t *req)
{
    float tire, diff;
    int   ppr;
    vss_get_cal(&tire, &diff, &ppr);

    float ppm  = vss_get_pulses_per_mile();
    float uspp = vss_get_usec_per_pulse_at_1mph();
    float mph  = vss_get_mph();

    char buf[2048];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>VSS Calibration</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:480px;margin:40px auto;"
        "padding:0 16px;background:#111;color:#eee}"
        "h1{color:#4af}h2{color:#aaa;font-size:1em;font-weight:normal}"
        "label{display:block;margin-top:12px;color:#aaa;font-size:.9em}"
        "input{width:100%%;box-sizing:border-box;padding:8px;background:#222;"
        "border:1px solid #444;color:#eee;border-radius:4px;font-size:1em;margin-top:4px}"
        "button{background:#4af;color:#000;border:none;padding:10px 24px;"
        "border-radius:4px;font-size:1em;cursor:pointer;margin-top:16px;width:100%%}"
        ".stat{background:#1a1a2a;border-radius:6px;padding:12px;margin-top:16px}"
        ".stat p{margin:4px 0;font-size:.9em;color:#aaa}"
        ".stat b{color:#eee}"
        "#msg{margin-top:12px;padding:10px;border-radius:4px;display:none}"
        ".ok{background:#1a3a1a;color:#4f4}.err{background:#3a1a1a;color:#f44}"
        "a{color:#4af}"
        "</style></head><body>"
        "<h1>VSS Calibration</h1>"
        "<h2><a href='/'>&#8592; Back to OTA</a></h2>"
        "<div class='stat'>"
        "<p>Current speed: <b>%.1f mph</b></p>"
        "<p>Pulses/mile: <b>%.1f</b></p>"
        "<p>&#956;s/pulse @ 1 mph: <b>%.1f</b></p>"
        "</div>"
        "<form method='POST' action='/vss/set'>"
        "<label>Tire circumference (inches)"
        "  <input name='tire' type='number' step='0.01' min='10' max='500' value='%.2f'>"
        "</label>"
        "<label>Differential ratio"
        "  <input name='diff' type='number' step='0.001' min='0.5' max='20' value='%.3f'>"
        "</label>"
        "<label>Pulses per driveshaft revolution"
        "  <input name='ppr' type='number' step='1' min='1' max='64' value='%d'>"
        "</label>"
        "<button type='submit'>Save &amp; Apply</button>"
        "</form>"
        "<div id='msg'></div>"
        "<script>"
        "var p=new URLSearchParams(window.location.search);"
        "if(p.get('saved')==='1'){"
        "var m=document.getElementById('msg');"
        "m.className='ok';m.textContent='Saved!';m.style.display='block';}"
        "if(p.get('err')==='1'){"
        "var m=document.getElementById('msg');"
        "m.className='err';m.textContent='Invalid values — not saved.';"
        "m.style.display='block';}"
        "</script>"
        "<p style='color:#555;font-size:.8em;margin-top:24px'>"
        "Tire circ = &pi; &times; diameter. "
        "Common: 33\" tyre = 103.7\", 31\" = 97.4\"</p>"
        "</body></html>",
        mph, ppm, uspp, tire, diff, ppr);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ── VSS calibration POST handler ──────────────────────────────────────────────
// Parses application/x-www-form-urlencoded body: tire=N&diff=N&ppr=N
static esp_err_t prv_post_vss_set(httpd_req_t *req)
{
    char body[256] = {};
    int  len = httpd_req_recv(req, body,
                              sizeof(body) - 1 < (size_t)req->content_len
                              ? sizeof(body) - 1 : (size_t)req->content_len);
    if (len <= 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/vss?err=1");
        return httpd_resp_send(req, NULL, 0);
    }
    body[len] = '\0';

    // Parse tire=, diff=, ppr= from URL-encoded body
    float tire = 0, diff = 0;
    int   ppr  = 0;

    char *p;
    if ((p = strstr(body, "tire="))) tire = strtof(p + 5, NULL);
    if ((p = strstr(body, "diff="))) diff = strtof(p + 5, NULL);
    if ((p = strstr(body, "ppr=")))  ppr  = (int)strtol(p + 4, NULL, 10);

    ESP_LOGI(TAG_VSS_WEB, "POST /vss/set  tire=%.2f diff=%.3f ppr=%d",
             tire, diff, ppr);

    const char *redirect;
    if (vss_set_cal(tire, diff, ppr) == ESP_OK) {
        redirect = "/vss?saved=1";
    } else {
        ESP_LOGW(TAG_VSS_WEB, "Invalid calibration values");
        redirect = "/vss?err=1";
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", redirect);
    return httpd_resp_send(req, NULL, 0);
}

// ── Register with existing httpd instance ────────────────────────────────────
void vss_register_handlers(httpd_handle_t server)
{
    httpd_uri_t vss_page = {
        .uri     = "/vss",
        .method  = HTTP_GET,
        .handler = prv_get_vss,
    };
    httpd_uri_t vss_set = {
        .uri     = "/vss/set",
        .method  = HTTP_POST,
        .handler = prv_post_vss_set,
    };
    httpd_register_uri_handler(server, &vss_page);
    httpd_register_uri_handler(server, &vss_set);
    ESP_LOGI(TAG_VSS_WEB, "VSS web handlers registered at /vss");
}
