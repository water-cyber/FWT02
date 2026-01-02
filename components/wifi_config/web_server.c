#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <ctype.h>
#include "web_server.h"
#include "common_interface.h"

#define MAX_APs 20 
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define DIR "wifi_config"
static const char *TAG = "web_server";

static EventGroupHandle_t ap_event_group;
const int WIFI_CREDENTIALS_READY = BIT0; // Bit báo hiệu SSID/PASS đã sẵn sàng
// HTML hiển thị danh sách WiFi và form nhập mật khẩu
const char *HTML_PAGE = "<!DOCTYPE html>\
<html lang='vi'>\
<head>\
<meta charset='UTF-8'>\
<meta name='viewport' content='width=device-width, initial-scale=1.0'>\
<title>Kết nối WiFi</title>\
<style>\
    body {\
        font-family: Arial, sans-serif;\
        background-color: #f0f0f0;\
        margin: 0;\
        padding: 0;\
    }\
    .container {\
        display: flex;\
        justify-content: center;\
        align-items: center;\
        height: 100vh;\
        padding: 10px;\
    }\
    .form-box {\
        background: white;\
        padding: 25px 35px;\
        border-radius: 16px;\
        box-shadow: 0 0 15px rgba(0,0,0,0.1);\
        width: 90%;\
        max-width: 400px;\
    }\
    h2 {\
        text-align: center;\
        font-size: 24px;\
        margin-bottom: 20px;\
    }\
    label {\
        display: block;\
        margin-top: 12px;\
        margin-bottom: 6px;\
        font-size: 16px;\
    }\
    input, select {\
        width: 100%;\
        padding: 10px;\
        font-size: 16px;\
        margin-bottom: 10px;\
        border: 1px solid #ccc;\
        border-radius: 6px;\
    }\
    .checkbox-label {\
        display: flex;\
        align-items: center;\
        font-size: 14px;\
        margin-bottom: 12px;\
    }\
    .checkbox-label input {\
        width: auto;\
        margin-right: 8px;\
    }\
    input[type='submit'] {\
        background-color: #007BFF;\
        color: white;\
        border: none;\
        font-size: 16px;\
        padding: 10px;\
        border-radius: 6px;\
        cursor: pointer;\
        width: 100%;\
    }\
    input[type='submit']:hover {\
        background-color: #0056b3;\
    }\
</style>\
</head>\
<body>\
<div class='container'>\
<div class='form-box'>\
<h2>Kết nối WiFi</h2>\
<form action='/connect' method='POST'>\
<label>SSID:</label>\
<select name='ssid'>%s</select>\
<label>Password:</label>\
<input type='password' id='password' name='password'>\
<div class='checkbox-label'>\
<input type='checkbox' onclick='togglePassword()'> Hiển thị mật khẩu\
</div>\
<input type='submit' value='Kết Nối'>\
</form>\
</div>\
</div>\
<script>\
function togglePassword() {\
  var x = document.getElementById('password');\
  x.type = (x.type === 'password') ? 'text' : 'password';\
}\
</script>\
</body></html>";


// Lưu SSID/PASS vào NVS
// void save_wifi_config(const char *ssid, const char *password) {
//     nvs_handle_t nvs_handle;
//     ESP_ERROR_CHECK(nvs_open("wifi_config", NVS_READWRITE, &nvs_handle));
//     nvs_set_str(nvs_handle, "ssid", ssid);
//     nvs_set_str(nvs_handle, "password", password);
//     nvs_commit(nvs_handle);
//     nvs_close(nvs_handle);
//     ESP_LOGI(TAG, "WiFi config saved!");
// }

void save_wifi_config(const char *ssid, const char *password, const char *dir) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(dir, NVS_READWRITE, &nvs_handle));
    nvs_set_str(nvs_handle, "ssid", ssid);
    nvs_set_str(nvs_handle, "password", password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi config saved!");
}

// Xử lý quét WiFi và trả về danh sách SSID
static esp_err_t scan_wifi_handler(httpd_req_t *req) {
        wifi_scan_config_t scan_config = {.show_hidden = true};
        esp_wifi_scan_start(&scan_config, true);

        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > MAX_APs) ap_count = MAX_APs;

        // wifi_ap_record_t ap_records[ap_count];
        // esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
        if (!ap_records) {
            ESP_LOGE(TAG, "Không cấp phát được bộ nhớ ap_records");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        // Cấp phát động ssid_list đủ lớn (mỗi AP khoảng 128 bytes)
        size_t ssid_buf_len = ap_count * 128;
        char *ssid_list = malloc(ssid_buf_len);
        if (!ssid_list) {
            ESP_LOGE(TAG, "Không cấp phát được bộ nhớ ssid_list");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        ssid_list[0] = '\0';

        for (int i = 0; i < ap_count; i++) {
            char option[128];
            snprintf(option, sizeof(option),
                    "<option value='%s'>%s (%d dBm)</option>",
                    ap_records[i].ssid, ap_records[i].ssid, ap_records[i].rssi);
            strlcat(ssid_list, option, ssid_buf_len);
        }

        // Cấp phát động response_html (HTML_PAGE khoảng 500 + ssid_list)
        size_t html_len = strlen(HTML_PAGE) + strlen(ssid_list) + 1;
        char *response_html = malloc(html_len);
        if (!response_html) {
            ESP_LOGE(TAG, "Không cấp phát được bộ nhớ response_html");
            free(ssid_list);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        snprintf(response_html, html_len, HTML_PAGE, ssid_list);

        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, response_html, HTTPD_RESP_USE_STRLEN);

        free(ssid_list);
        free(response_html);
        free(ap_records);
        return ESP_OK;
    }


// Hàm giải mã URL
void url_decode(const char *src, char *dest, size_t dest_size) {
    char *d = dest;
    const char *s = src;
    char hex[3] = {0};

    while (*s && (d - dest) < (dest_size - 1)) {
        if (*s == '%') {
            if (isxdigit((unsigned char) *(s + 1)) && isxdigit((unsigned char) *(s + 2))) {
                hex[0] = *(s + 1);
                hex[1] = *(s + 2);
                *d++ = (char) strtol(hex, NULL, 16);
                s += 3;
            } else {
                *d++ = *s++; // Trường hợp `%` nhưng không hợp lệ
            }
        } else if (*s == '+') {
            *d++ = ' ';
            s++;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0'; // Kết thúc chuỗi
}

// Xử lý nhận SSID/PASS từ form và lưu vào NVS
static esp_err_t connect_wifi_handler(httpd_req_t *req) {
    char content[100];  // Bộ đệm chứa query string
    int recv_len = httpd_req_recv(req, content, sizeof(content) - 1);
    if (recv_len <= 0) return ESP_FAIL;
    content[recv_len] = '\0';
    ESP_LOGI(TAG, "Nhận dữ liệu từ Web: %s", content);

    char ssid_enc[MAX_SSID_LEN] = {0}, password_enc[MAX_PASS_LEN] = {0};
    char ssid[MAX_SSID_LEN] = {0}, password[MAX_PASS_LEN] = {0};

    // Lấy giá trị SSID từ query string
    if (httpd_query_key_value(content, "ssid", ssid_enc, sizeof(ssid_enc)) == ESP_OK) {
        url_decode(ssid_enc, ssid, sizeof(ssid));  // Giải mã URL
        ESP_LOGI(TAG, "SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "Không tìm thấy SSID!");
        httpd_resp_send(req, "Missing SSID", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Lấy giá trị Password từ query string
    if (httpd_query_key_value(content, "password", password_enc, sizeof(password_enc)) == ESP_OK) {
        url_decode(password_enc, password, sizeof(password));  // Giải mã URL
        ESP_LOGI(TAG, "Password: %s", password);
    } else {
        ESP_LOGE(TAG, "Không tìm thấy Password!");
        httpd_resp_send(req, "Missing Password", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Lưu SSID/PASS vào NVS 
    save_wifi_config(ssid, password, DIR);
    // if (password[0] == '\0') {
    //     ESP_LOGW(TAG, "Password is empty — connecting in open mode!");
    //     gateway_data.skip_wifi = true;
    // //wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    // } else{
    //     gateway_data.skip_wifi = false;
    // }
        const char *html_response =
            "<!DOCTYPE html>"
            "<html>"
            "<head><meta charset='UTF-8'><title>WiFi Connected</title></head>"
            "<body style='text-align:center; background-color:#f2f2f2;'>"
            "<h1 style='color:green; font-size:32px;'>✅ Kết nối thành công!</h1>"
            "<p style='font-size:24px;'>ESP sẽ khởi động lại trong giây lát...</p>"
            "</body></html>";

        // Đặt kiểu nội dung (HTML)
        httpd_resp_set_type(req, "text/html");

        // Gửi về trình duyệt
        httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);

        //httpd_resp_send(req, "Connection successful, ESP will reboot!", HTTPD_RESP_USE_STRLEN);
        ESP_ERROR_CHECK(esp_wifi_stop()); 
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        xEventGroupSetBits(ap_event_group, WIFI_CREDENTIALS_READY); 
        esp_restart();
        return ESP_OK;
}

// Khởi động Web Server
void start_web_server() {
    ap_event_group = xEventGroupCreate(); // Tạo Event Group
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    httpd_start(&server, &config);

    httpd_uri_t scan_wifi = {.uri = "/", .method = HTTP_GET, .handler = scan_wifi_handler};
    httpd_uri_t connect_wifi = {.uri = "/connect", .method = HTTP_POST, .handler = connect_wifi_handler};

    httpd_register_uri_handler(server, &scan_wifi);
    httpd_register_uri_handler(server, &connect_wifi);
    ESP_LOGI(TAG, "Web server started");
    ESP_LOGI(TAG, "[APP] Free memory start_web_server: %" PRIu32 " bytes", esp_get_free_heap_size());
}
void wait_for_wifi_credentials() {
    ESP_LOGI(TAG, "Waiting SSID/PASS from Web...");
    xEventGroupWaitBits(ap_event_group, WIFI_CREDENTIALS_READY, pdTRUE, pdFALSE, portMAX_DELAY);
}