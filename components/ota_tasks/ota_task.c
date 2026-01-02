// ota_mqtt_example.c
// ESP-IDF v5.x compatible example (mẫu)
// Features:
// - store current firmware SHA256 in NVS
// - publish device info via MQTT
// - subscribe to device command topic
// - when server sends ota_pending + firmware_url + firmware_hash != current_hash -> download & ota
// - after success, update NVS and send ack

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common_interface.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_image_format.h"

static const char *TAG = "OTA_MQTT_EX";

// -------- CONFIG --------------------------------
#define NVS_NAMESPACE   "ota_ns"
#define NVS_KEY_HASH    "fw_hash"
#define NVS_KEY_MODE    "dev_mode" // 0 = prod, 1 = dev
// ------------------------------------------------


ota_job_t ota_job = { 0 };
// extern const uint8_t server_root_ca_pem_start[] asm("_binary_server_pem_start");
// extern const uint8_t server_root_ca_pem_end[]   asm("_binary_server_pem_end");


// ---- Ghi chuỗi ----
esp_err_t nvs_write_string(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);  // cần commit để lưu xuống flash
    }

    nvs_close(h);
    return err;
}

// ---- Đọc chuỗi ----
esp_err_t nvs_read_string(const char *key, char *out, size_t max_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t required = max_len;
    err = nvs_get_str(h, key, out, &required);

    nvs_close(h);
    return err;
}

// ---------------- compute SHA256 of running app ------------
esp_err_t compute_running_app_sha256_hex(char out_hex[65])
{
    // const esp_partition_t *running = esp_ota_get_running_partition();
    // if (!running) return ESP_FAIL;

    // esp_image_metadata_t data;
    // esp_err_t err = esp_image_get_metadata(running, &data);
    // if (err != ESP_OK) {
    //     ESP_LOGE("OTA_HASH", "esp_image_get_metadata failed: %s", esp_err_to_name(err));
    //     return err;
    // }

    // for (int i = 0; i < 32; i++) {
    //     sprintf(out_hex + i * 2, "%02x", data.image_digest[i]);
    // }
    // out_hex[64] = '\0';

    // ESP_LOGI("OTA_HASH", "Running firmware SHA-256: %s", out_hex);
    // return ESP_OK;
// ***********************************************************************

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return ESP_FAIL;

    // Tạo partition_pos từ running partition
    esp_partition_pos_t pos = {
        .offset = running->address,   // ESP-IDF 5.3.1 dùng 'offset' thay cho 'address'
        .size   = running->size
    };

    esp_image_metadata_t data;
    esp_err_t err = esp_image_get_metadata(&pos, &data);
    if (err != ESP_OK) {
        ESP_LOGE("OTA_HASH", "esp_image_get_metadata failed: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + i * 2, "%02x", data.image_digest[i]);
    }
    out_hex[64] = '\0';

    //ESP_LOGI("OTA_HASH", "Running firmware SHA-256: %s", out_hex);
    printf("Running firmware SHA-256 of func: %s", out_hex);
    return ESP_OK;

//********************************************************************** */
    // const esp_partition_t *running = esp_ota_get_running_partition();
    // if (running == NULL) {
    //     return ESP_FAIL;
    // }

    // mbedtls_sha256_context sha_ctx;
    // mbedtls_sha256_init(&sha_ctx);
    // mbedtls_sha256_starts(&sha_ctx, 0);   // 0 = SHA-256

    // const size_t chunk = 4 * 1024;
    // uint8_t *buf = malloc(chunk);
    // if (!buf) {
    //     mbedtls_sha256_free(&sha_ctx);
    //     return ESP_ERR_NO_MEM;
    // }

    // size_t off = 0;
    // while (off < running->size) {
    //     size_t to_read = chunk;
    //     if (off + to_read > running->size) {
    //         to_read = running->size - off;
    //     }

    //     esp_err_t err = esp_partition_read(running, off, buf, to_read);
    //     if (err != ESP_OK) {
    //         free(buf);
    //         mbedtls_sha256_free(&sha_ctx);
    //         return err;
    //     }

    //     mbedtls_sha256_update(&sha_ctx, buf, to_read);
    //     off += to_read;
    // }

    // uint8_t out_bin[32];
    // mbedtls_sha256_finish(&sha_ctx, out_bin);
    // mbedtls_sha256_free(&sha_ctx);
    // free(buf);

    // for (int i = 0; i < 32; ++i) {
    //     sprintf(out_hex + i * 2, "%02x", out_bin[i]);
    // }
    // out_hex[64] = '\0';

    // return ESP_OK;
}

void get_hashcode_ota(void)
{
    char full_hash[65] = {0};
    char stored_short[17] = {0};
    char current_short[17] = {0};

    // Tính hash hiện tại
    if (compute_running_app_sha256_hex(full_hash) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot compute running firmware hash");
        strcpy(gateway_data.current_hash, "unknown");
        return;
    }

    // Rút gọn 16 ký tự
    strncpy(current_short, full_hash, 16);
    current_short[16] = '\0';

    // Đọc hash đã lưu trong NVS (nếu có)
    if (nvs_read_string(NVS_KEY_HASH, stored_short, sizeof(stored_short)) != ESP_OK) {
        stored_short[0] = '\0'; // coi như chưa có
    }

    // So sánh: nếu khác thì cập nhật
    if (strcasecmp(current_short, stored_short) != 0) {
        ESP_LOGI(TAG,
            "Firmware hash changed -> update NVS (old: %s, new: %s)",
            stored_short[0] ? stored_short : "(none)",
            current_short);
        nvs_write_string(NVS_KEY_HASH, current_short);
    }

    // Lưu vào biến toàn cục
    strcpy(gateway_data.current_hash, current_short);

    // Debug
    ESP_LOGI(TAG, "Running firmware full hash : %s", full_hash);
    ESP_LOGI(TAG, "Running firmware short hash: %s", current_short);
}

// void get_hashcode_ota(void)
// {
//     char full_hash[65] = {0};
//     char short_hash[17] = {0};

//     if (nvs_read_string(NVS_KEY_HASH, short_hash, sizeof(short_hash)) != ESP_OK) {
//         ESP_LOGW(TAG, "No stored hash, computing running image hash...");
//         if (compute_running_app_sha256_hex(full_hash) == ESP_OK) {
//             ESP_LOGI(TAG, "Running firmware full hash: %s", full_hash);
//             strncpy(short_hash, full_hash, 16);
//             short_hash[16] = '\0';
//             nvs_write_string(NVS_KEY_HASH, short_hash);
//         } else {
//             strcpy(short_hash, "unknown");
//         }
//     }
//     strcpy(gateway_data.current_hash, short_hash);
//     ESP_LOGI(TAG, "Running firmware short hash: %s", short_hash);
// }

// static esp_err_t run_http_ota(const char *url)
// {
//     esp_http_client_config_t http_cfg = {
//         .url = url,
//         //.cert_pem = (const char *)server_root_ca_pem_start,    
//         .timeout_ms = 20000,
//         // If you use HTTPS you should set cert_pem here. For HTTP leave NULL.
//          .cert_pem = NULL,                 // HTTP => để NULL
//         // .skip_cert_common_name_check = true, // <== Thêm dòng này
//     };
//     esp_https_ota_config_t ota_config = {
//         .http_config = &http_cfg,
//     };
//     esp_https_ota_handle_t ota_handle = NULL;
//     esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
//         return err;
//     }

//     while (1) {
//         err = esp_https_ota_perform(ota_handle);
//         if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
//             // progress
//             size_t r = esp_https_ota_get_image_len_read(ota_handle);
//             ESP_LOGI(TAG, "OTA read %d bytes", r);
//             vTaskDelay(pdMS_TO_TICKS(200));
//             continue;
//         } else {
//             break;
//         }
//     }

//     if (esp_https_ota_is_complete_data_received(ota_handle) != true) {
//         ESP_LOGE(TAG, "Complete data was not received.");
//         esp_https_ota_abort(ota_handle);
//         return ESP_FAIL;
//     }

//     esp_err_t finish_err = esp_https_ota_finish(ota_handle);
//     if (finish_err == ESP_OK) {
//         ESP_LOGI(TAG, "OTA finished successfully");
//         return ESP_OK;
//     } else {
//         ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(finish_err));
//         esp_https_ota_abort(ota_handle);
//         return finish_err;
//     }
// }

static esp_err_t run_http_ota(const char *url)
{
    esp_http_client_config_t http_cfg = {
        .url = url,
        .cert_pem = NULL,                 // Bắt buộc NULL cho HTTP
        .transport_type = HTTP_TRANSPORT_OVER_TCP, // Chỉ định dùng TCP thuần
        .timeout_ms = 20000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA finished successfully");
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    return ret;

}

 void start_ota_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OTA task started");
    // Copy local job
    ota_job_t job;
    memcpy(&job, &ota_job, sizeof(ota_job));
    // Clear global flag to avoid multiple starts
    //ota_job.ota_pending = false;

    // verify URL and hash present
    //if (!job.ota_pending && job.firmware_url[0] == '\0') {
    if (job.firmware_url[0] == '\0' || job.firmware_hash[0] == '\0') {
        ESP_LOGW(TAG, "No OTA job info");
        gateway_data.ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    // 1) download & flash
    esp_err_t r = run_http_ota(job.firmware_url);
    if (r != ESP_OK) {
        //send_ota_result(false, esp_err_to_name(r));
        printf("download & flash fail\n");
        gateway_data.ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    // 2) after successful ota, compute new running hash and compare with expected
    // char new_hash[65] = {0};
    // if (compute_running_app_sha256_hex(new_hash) == ESP_OK) {
    //     ESP_LOGI(TAG, "New running hash: %s", new_hash);

    //     // ===== Chỉ lấy 16 ký tự đầu để so sánh & lưu =====
    //     char short_hash[17] = {0};              // +1 cho ký tự '\0'
    //     strncpy(short_hash, new_hash, 16);
    //     short_hash[16] = '\0';

    // if (job.firmware_hash[0] != '\0' &&
    //     strncasecmp(short_hash, job.firmware_hash, 16) != 0) // so sánh 16 ký tự đầu
    // {
    //     ESP_LOGE(TAG, "Hash mismatch after OTA (expected %s)", job.firmware_hash);
    //     printf("hash_mismatch fail\n");
    //     vTaskDelay(pdMS_TO_TICKS(2000));
    //     gateway_data.ota_running = false;
    //     vTaskDelete(NULL);
    //     return;
    // }
    // // ===== Lưu short hash vào NVS =====
    // nvs_write_string(NVS_KEY_HASH, short_hash);
    // ESP_LOGI(TAG, "Short hash stored in NVS: %s", short_hash);
    // } else {
    //     ESP_LOGW(TAG, "Cannot compute new hash");
    // }

    // 3) send success and restart
    //send_ota_result(true, NULL);
    printf("success and restart\n");
    gateway_data.ota_running = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    vTaskDelete(NULL);
}
