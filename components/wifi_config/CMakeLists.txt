idf_component_register(SRCS "web_server.c" "wifi_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_http_server nvs_flash main)