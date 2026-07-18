// =============================================================
//  main_linux.c — EV Dashboard, Linux/Pi5 prototype entry point
//
//  Reuses the portable LVGL UI (../src/dashboard_ui.cpp) unchanged.
//  Replaces the ESP-IDF FreeRTOS/TWAI main with:
//    - DRM/KMS display (the DSI panel is already a DRM device)
//    - evdev touch (the Goodix GT911 shows up as a Linux input dev)
//    - the same SIM_DATA sinf() generator the ESP build uses
//
//  Usage:
//    ./dashboard [drm_device] [touch_event_device]
//    e.g. ./dashboard /dev/dri/card0 /dev/input/event4
//  Both args optional; drm defaults to /dev/dri/card0, touch is
//  skipped if not given.
// =============================================================
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "lvgl.h"
#include "dashboard_ui.h"
#include "can_parser.h"      // DashData
#include "gear_shifter.h"    // gear_shifter_current()
#include "bms_http.h"        // bms_http_poll()
#include "http_api.h"        // http_api_start()

// Auto-detect the touchscreen's evdev node by scanning input device names.
// Returns a static "/dev/input/eventN" path, or NULL if none found.
static const char *find_touch_device(void)
{
    static char path[64];
    DIR *d = opendir("/sys/class/input");
    if (!d) return NULL;
    struct dirent *e;
    const char *found = NULL;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char namep[160], name[128] = {0};
        snprintf(namep, sizeof namep, "/sys/class/input/%s/device/name", e->d_name);
        FILE *f = fopen(namep, "r");
        if (!f) continue;
        if (fgets(name, sizeof name, f) &&
            (strstr(name, "Goodix") || strstr(name, "Touch") || strstr(name, "touch"))) {
            snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
            found = path;
        }
        fclose(f);
        if (found) break;
    }
    closedir(d);
    return found;
}

// Open a non-blocking SocketCAN RAW socket bound to `iface`; -1 if unavailable.
static int open_can_socket(const char *iface)
{
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) { close(s); return -1; }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) < 0) { close(s); return -1; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    return s;
}

// LVGL tick source: milliseconds from a monotonic clock.
static uint32_t millis_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

int main(int argc, char **argv)
{
    const char *fb_dev    = (argc > 1) ? argv[1] : "/dev/fb0";
    const char *touch_dev = (argc > 2) ? argv[2] : NULL;
    if (!touch_dev) touch_dev = find_touch_device();   // auto-detect GT911

    setvbuf(stdout, NULL, _IONBF, 0);   // unbuffered so logs show over ssh
    lv_init();
    lv_tick_set_cb(millis_cb);

    // --- display: fbdev ---
    // We use fbdev (not DRM) because LVGL's DRM driver renders DIRECT into the
    // framebuffer and cannot do software rotation, whereas the fbdev driver
    // rotates in software in its flush. The panel is 720x1920 native; we rotate
    // to 1920x720 landscape below.
    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp) { fprintf(stderr, "lv_linux_fbdev_create failed\n"); return 1; }
    lv_linux_fbdev_set_file(disp, fb_dev);

    // Panel is 720x1920 portrait; the dashboard UI is 1920x720 landscape.
    // 270 is the correct upright orientation for this panel (90 is upside down);
    // override with LV_ROTATE=0|90|180|270.
    int rot = 270;
    const char *renv = getenv("LV_ROTATE");
    if (renv) rot = atoi(renv);
    lv_display_rotation_t r = LV_DISPLAY_ROTATION_0;
    if      (rot == 90)  r = LV_DISPLAY_ROTATION_90;
    else if (rot == 180) r = LV_DISPLAY_ROTATION_180;
    else if (rot == 270) r = LV_DISPLAY_ROTATION_270;
    lv_display_set_rotation(disp, r);
    printf("rotation: %d\n", rot);

    printf("fbdev display %s  logical %dx%d\n", fb_dev,
           (int)lv_display_get_horizontal_resolution(disp),
           (int)lv_display_get_vertical_resolution(disp));

    // --- touch: evdev (Goodix GT911) ---
    if (touch_dev) {
        lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_dev);
        if (touch) { lv_indev_set_display(touch, disp); printf("touch: %s\n", touch_dev); }
        else fprintf(stderr, "warning: lv_evdev_create(%s) failed — running without touch\n", touch_dev);
    } else {
        printf("touch: (none — pass event device as arg 2 to enable)\n");
    }

    // --- build the dashboard UI ---
    dashboard_ui_create(disp);

    // --- HTTP control/status API (remote nav + live state) ---
    http_api_start(8080);

    // --- data source: real vehicle CAN (can0) if available, else simulated ---
    const char *can_iface = getenv("CAN_IFACE");
    if (!can_iface) can_iface = "can0";
    int can_fd = open_can_socket(can_iface);
    const bool use_can = (can_fd >= 0);
    printf("data source: %s\n", use_can ? can_iface : "SIM (no CAN — sine-wave demo)");

    // g_dash is defined + initialized in can_parser.cpp (dir_confirmed=INT8_MIN);
    // do NOT memset it here. In CAN mode parse_can_frame() fills it from frames.
    float t = 0.0f;
    uint64_t canload_bits = 0;               // CAN bus-load accumulator
    uint32_t canload_t0   = millis_cb();
    for (;;) {
        if (use_can) {
            // drain all pending frames -> the shared parser updates g_dash
            struct can_frame fr;
            while (read(can_fd, &fr, sizeof fr) == (ssize_t)sizeof fr) {
                parse_can_frame(fr.can_id & CAN_EFF_MASK, fr.data, millis_cb());
                // bus-load tally: overhead + data bits, +~15% avg bit-stuffing
                int ov = (fr.can_id & CAN_EFF_FLAG) ? 67 : 47;
                canload_bits += (uint64_t)((ov + 8 * fr.can_dlc) * 1.15f);
            }
        } else {
            t += 0.05f;
            g_dash.soc_pct         = 50.0f + 45.0f * sinf(t * 0.3f);
            g_dash.speed           = 60.0f + 50.0f * sinf(t * 0.7f);
            g_dash.power_kw        = 150.0f * sinf(t * 1.1f);
            g_dash.pack_volts      = 390.0f + 10.0f * sinf(t * 0.2f);
            g_dash.pack_amps       = g_dash.power_kw * 1000.0f / g_dash.pack_volts;
            g_dash.inverter_temp_c = 40.0f + 20.0f * sinf(t * 0.15f);
            g_dash.motor_temp_c    = 60.0f + 30.0f * sinf(t * 0.2f);
            g_dash.batt_temp_c     = 25.0f + 10.0f * sinf(t * 0.1f);
            g_dash.aux_volts       = 13.5f + 0.5f * sinf(t * 0.4f);
            g_dash.range_dist      = g_dash.soc_pct * 2.5f;
            g_dash.gear            = 3;
            g_dash.odo_total_miles = 12345.0f;
            g_dash.trip_miles      = 42.0f;
        }

        // CAN bus load %, refreshed ~1 Hz (500 kbps bus)
        uint32_t cl_now = millis_cb();
        if (cl_now - canload_t0 >= 1000) {
            float secs = (cl_now - canload_t0) / 1000.0f;
            float load = (float)canload_bits / (500000.0f * secs) * 100.0f;
            g_dash.can_load_pct = load > 100.0f ? 100.0f : load;
            canload_bits = 0;
            canload_t0   = cl_now;
        }

        // let a touched gear button override immediately (UI feedback)
        int8_t req = gear_shifter_current();
        if (req >= 0) g_dash.gear = (uint8_t)req;

        // poll the BMW i3 BMS web API (only while the BMS screen is showing)
        bms_http_poll(millis_cb(), dashboard_ui_get_screen() == DASH_SCREEN_BMS);

        dashboard_ui_update(&g_dash);
        lv_timer_handler();
        usleep(30 * 1000);   // ~33 fps
    }
    return 0;
}
