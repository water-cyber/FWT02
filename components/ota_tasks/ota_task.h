#ifndef OTA_TASK_H
#define OTA_TASK_H
void start_ota_task(void *pvParameters);
void get_hashcode_ota();
esp_err_t nvs_write_string(const char *key, const char *value);
esp_err_t nvs_read_string(const char *key, char *out, size_t max_len);
esp_err_t compute_running_app_sha256_hex(char out_hex[65]);
#endif