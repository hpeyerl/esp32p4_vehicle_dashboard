cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.cpp"
        "display_driver.c"
        "display_stub.c"
        "tab5_display.c"
        "waveshare_display.c"
        "ota_server.c"
        "can_parser.cpp"
        "dashboard_ui.cpp"
        "lv_font_montserrat_110.c"
        "lv_font_montserrat_72.c"
        "vss_sensor.c"
        "vss_web_handlers.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_lcd
        esp_wifi
        esp_http_server
        app_update
        mdns
        nvs_flash
        esp_netif
        esp_event
        driver
        esp_timer
        esp_psram
        lvgl__lvgl
)
