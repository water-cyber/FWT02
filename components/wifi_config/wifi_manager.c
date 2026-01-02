#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"

#include "esp_event.h"
#include "wifi_manager.h"
#include "common_interface.h"

//#define WIFI_SSID "ESP32_Setup"
char wifi_ssid[32];  // đủ dài để chứa SSID
#define WIFI_PASS "12345678"
#define MAX_STA_CONN 4

// #define DNS_PORT 53

static const char *TAG = "wifi_manager";

void wifi_init_softap(void) {
    ESP_LOGI(TAG, "Initializing WiFi in SoftAP mode...");
    const char* suffix = imei + strlen(imei) - 6;
    snprintf(wifi_ssid, sizeof(wifi_ssid), "FireAI_%s", suffix);
    // Tạo network interface cho SoftAP
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(wifi_ssid),
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    strncpy((char *)wifi_config.ap.ssid, wifi_ssid, sizeof(wifi_config.ap.ssid));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started with SSID: %s", wifi_ssid);
}
