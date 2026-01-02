#include <stdio.h> 
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_mac.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_task.h"
#include "cJSON.h"
#include "common_interface.h"
#include "uuid.h"
#include "json_parser.h"
#include "ethernet_w5500.h"
#include "ota_task.h"

#define PUB_NORMAL_MQTT_TIME 270 * 1000 * 1000 // 270s gửi bản tin normal 1 lần
#define MESSAGE_TYPE_HEARTBEAT "heart_beat"
#define MESSAGE_TYPE_RESERT "reset"
#define MESSAGE_TYPE_EVENT "event"
#define MESSAGE_TYPE_TEST "test"
#define MQTT_HEAP_SIZE 512

#define BROKER_ADDRESS "mqtt://109.237.64.101"
#define MQTT_USER "haui2025"
#define MQTT_PASSWORD "haui2025"
#define CONFIG_SSID_4G "FireAI_config/ssid/%s"
// #define BROKER_ADDRESS "mqtt://dev-emqx.sful.com.vn"
// #define MQTT_USER "FCVD-01"
// #define MQTT_PASSWORD "xtijGhP9bKLBi0S"
// #define MQTT_CLIENT_PUB "/topic/qos0"
// #define MQTT_CLIENT_SUB "/topic1/qos0"

// static const char *ota_downstream_topic = "FWT-02/downstream/OTA";
 static const char *ota_downstream_topic = "FWT-02/downstream/OTA";
#define MAX_MQTT_BUFFER 10
mqtt_msg_t mqtt_buffer[MAX_MQTT_BUFFER];
int mqtt_buffer_len = 0;

char client_id[50];
char upstream_topic[70];
char downstream_topic[70];
char config_4G[70];
bool is_disconnet = false;

uint64_t perivos_time_send = 0;
static bool log_shown = false;
static esp_mqtt_client_handle_t client;

typedef enum {
    EVENT_NONE = 0,
    EVENT_ALARM,
    EVENT_FAULT,
    EVENT_DISCONNECT
} EventType;

EventType current_event_type = EVENT_NONE;

static const char *TAG = "MQTTS_APP";
TaskHandle_t mqttTaskHandle = NULL;

void save_4G_config(const char *ssid) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("4G_config", NVS_READWRITE, &nvs_handle));
    nvs_set_str(nvs_handle, "ssid", ssid);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "4G config saved!");
}
void mqtt_buffer_resend(esp_mqtt_client_handle_t client) {
    for (int i = 0; i < mqtt_buffer_len; i++) {
        if (!mqtt_buffer[i].pub_success) {
            int msg_id = esp_mqtt_client_enqueue(client,
                                                 mqtt_buffer[i].topic,
                                                 mqtt_buffer[i].payload,
                                                 strlen(mqtt_buffer[i].payload),
                                                 mqtt_buffer[i].qos,
                                                 mqtt_buffer[i].retain,
                                                 true);
            if (msg_id != -1) {
                mqtt_buffer[i].msg_id = msg_id;
            }
        }
    }
}

void mqtt_buffer_add(const char *topic, const char *payload, int qos, int retain) {
    if (mqtt_buffer_len >= MAX_MQTT_BUFFER) return;

    mqtt_msg_t *msg = &mqtt_buffer[mqtt_buffer_len];
    strncpy(msg->topic, topic, sizeof(msg->topic));
    strncpy(msg->payload, payload, sizeof(msg->payload));
    msg->qos = qos;
    msg->retain = retain;
    msg->pub_success = false;
    msg->msg_id = -1;  // sẽ cập nhật sau khi enqueue

    mqtt_buffer_len++;
}

void mqtt_buffer_clean() {
    int j = 0;
    for (int i = 0; i < mqtt_buffer_len; i++) {
        if (!mqtt_buffer[i].pub_success) {
            mqtt_buffer[j++] = mqtt_buffer[i];
        }
    }
    mqtt_buffer_len = j;
}



static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    // int msg_id = 0;
    // int msg_id_config = 0;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        log_shown = false; // Reset flag khi kết nối lại
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED. Subscribing to topic: %s", downstream_topic);
        gateway_data.mqtt_status = true;

        ESP_LOGI(TAG, "MQTT connected, resending cached messages");
        mqtt_buffer_resend(client);
        esp_mqtt_client_subscribe(client, downstream_topic, 0);
        esp_mqtt_client_subscribe(client, config_4G, 0);

        // ==== THÊM DÒNG NÀY ====
        esp_mqtt_client_subscribe(client, ota_downstream_topic, 0);
        ESP_LOGI(TAG, "Subscribed OTA downstream: %s", ota_downstream_topic);
        // =======================

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        gateway_data.mqtt_status = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        for (int i = 0; i < mqtt_buffer_len; i++) {
            if (mqtt_buffer[i].msg_id == event->msg_id) {
                mqtt_buffer[i].pub_success = true;
                break;
            }
        }
        mqtt_buffer_clean();
        break;
        case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
        if (strncmp(event->topic, config_4G, event->topic_len) == 0 &&
            strlen(config_4G) == event->topic_len)
        {
            char ssid_4G[32] = {0};
            // Ghi data vào biến ssid_4G
            size_t len = event->data_len < sizeof(ssid_4G) - 1 ? event->data_len : sizeof(ssid_4G) - 1;
            memcpy(ssid_4G, event->data, len);
            ssid_4G[len] = '\0';  // Đảm bảo null-terminated
            save_4G_config(ssid_4G);
            ESP_LOGI(TAG, "Received SSID : %s", ssid_4G);
            ESP_LOGI(TAG, "RESETING ....");
            esp_restart();
        } 
        // ==== THÊM PHẦN OTA ====
        else if (strncmp(event->topic, ota_downstream_topic, event->topic_len) == 0 &&
                strlen(ota_downstream_topic) == event->topic_len)
            {
                ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s", event->topic_len, event->topic);

                char *payload = strndup(event->data, event->data_len);
                cJSON *root = cJSON_Parse(payload);
            if (root) {
                //cJSON *p_ota = cJSON_GetObjectItem(root, "ota_pending");
                cJSON *p_url = cJSON_GetObjectItem(root, "firmware_url");
                cJSON *p_hash = cJSON_GetObjectItem(root, "firmware_hash");
                cJSON *p_version = cJSON_GetObjectItem(root, "version");

                //if (p_ota && p_ota->type == cJSON_True && p_url && p_hash) {
                if (p_url && p_hash) {
                    char stored_hash[65] = {0};
                    if (nvs_read_string("fw_hash", stored_hash, sizeof(stored_hash)) != ESP_OK) {
                        compute_running_app_sha256_hex(stored_hash);
                    }
                    if (strcasecmp(stored_hash, p_hash->valuestring) != 0) {
                        ESP_LOGI(TAG, "OTA available: %s (new hash %s), current %s, version: %s",
                                p_url->valuestring, p_hash->valuestring, stored_hash, p_version->valuestring);
                        //ota_job.ota_pending = true;
                        strncpy(ota_job.firmware_url, p_url->valuestring, sizeof(ota_job.firmware_url)-1);
                        strncpy(ota_job.firmware_hash, p_hash->valuestring, sizeof(ota_job.firmware_hash)-1);
                        if (p_version && p_version->valuestring) {
                            strncpy(ota_job.version, p_version->valuestring, sizeof(ota_job.version)-1);
                        }
                        if(!gateway_data.ota_running){
                            gateway_data.ota_running = true;
                            xTaskCreate(start_ota_task, "ota_task", 8*1024, NULL, 5, NULL);
                        } 
                    } else {
                        ESP_LOGI(TAG, "Hashes match -> no OTA needed");
                    }
                } else {
                    ESP_LOGW(TAG, "MQTT payload missing ota fields");
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGW(TAG, "Invalid JSON payload");
            }
            free(payload);
        }
  
        // =======================        
        else {
            // Xử lý các topic khác
            bool status = json_parser_gw_cmd((const char *)event->data, event->data_len, imei);
            ESP_LOGI(TAG, "Command status = %d", status);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        gateway_data.mqtt_status = false;
        break;
    default:
        ESP_LOGI(TAG, "Other event id: %d", event->event_id);
        break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_ADDRESS,  // Địa chỉ broker MQTT
        .credentials.username = MQTT_USER,  // Tên người dùng MQTT
        .credentials.authentication.password = MQTT_PASSWORD,  // Mật khẩu MQTT
        .credentials.client_id = client_id,
        .session.disable_clean_session = true,  // GIỮ LẠI SESSION
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
    }
    
}

EventType get_current_priority_event() {
    if (gateway_data.mqtt_alarm) return EVENT_ALARM;
    if (gateway_data.mqtt_fault) return EVENT_FAULT;
    if (gateway_data.disconnect_device) return EVENT_DISCONNECT;
    return EVENT_NONE;
}

void send_event(const char* type) {
    char *message_packet = malloc(MQTT_HEAP_SIZE);
    if (!message_packet) return;

    memset(message_packet, 0x00, MQTT_HEAP_SIZE);
    json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, type);

    if (!gateway_data.mqtt_status)
        mqtt_buffer_add(upstream_topic, message_packet, 1, 0);
    else
        esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);

    write_data_to_sdcard(type);
    free(message_packet);
}

// void send_gateway_event(const char* type) {
//     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE);
//     if (!message_packet) return;

//     memset(message_packet, 0x00, MQTT_HEAP_SIZE);
//     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, type);

//     if (!gateway_data.mqtt_status) {
//         mqtt_buffer_add(upstream_topic, message_packet, 1, 0);
//     } else {
//         esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
//     }

//     write_data_to_sdcard();
//     free(message_packet);
// }

void send_gateway_heartbeat(const char* state) {
    char *message_packet = (char *)malloc(MQTT_HEAP_SIZE);
    if (!message_packet) return;

    memset(message_packet, 0x00, MQTT_HEAP_SIZE);
    json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, state);

    if (!gateway_data.mqtt_status) {
        mqtt_buffer_add(upstream_topic, message_packet, 1, 0);
    } else {
        esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
    }

    free(message_packet);
}

static void mqtt_send_task(void *pvParameters){
    gateway_data.update_sub_device = false;
    bool frist_pub = true;
    perivos_time_send = esp_timer_get_time();
    //gateway_data.update_sub_device = false;
    // bool perAlarm = true;
    // bool perFault = true;
    // bool perDis = true;
    uint8_t state_power = 0;
    
    while(1){
       
        if (!gateway_data.event_enable && !log_shown) {
            ESP_LOGI(TAG, "MQTT event handler is paused. Ignoring all MQTT events.");
            gateway_data.mqtt_status = false;
            log_shown = true;
            //esp_mqtt_client_stop(client);
            esp_mqtt_client_disconnect(client);
            is_disconnet  = true;
        }
        if (!gateway_data.mqtt_status && gateway_data.event_enable) {
            gateway_data.mqtt_status = true;
            //esp_mqtt_client_start(client);
            if(is_disconnet){
                esp_mqtt_client_reconnect(client);
                is_disconnet  = false;
            }
            ESP_LOGI(TAG, "TEST reconnect\n");
        }
        if(frist_pub){
            char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_RESERT, "normal");
            esp_mqtt_client_enqueue(client, upstream_topic, message_packet, strlen(message_packet), 0, 0, true);
            memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            if(gateway_data.mqtt_alarm){
                json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "alarm");
            } else if(gateway_data.mqtt_fault){
                json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "fault");
            } else if(gateway_data.disconnect_device){
                json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "disconnect");
            } else{
                json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "normal");
            }
            esp_mqtt_client_enqueue(client, upstream_topic, message_packet, strlen(message_packet), 0, 0, true);
            frist_pub = false;
            
            free(message_packet);
        } else{
            // if(gateway_data.mqtt_alarm && perAlarm){
            //     per_event = check_event;
            //     check_event = (gateway_data.mqtt_alarm << 2) | (gateway_data.mqtt_fault << 1) | (gateway_data.disconnect_device);
            //     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            //     memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "alarm");
            //     //esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     //esp_mqtt_client_enqueue(client, upstream_topic, message_packet, strlen(message_packet), 0, 0, true);
            //     if(!gateway_data.mqtt_status){
            //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
            //     } else{
            //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     }
            //     //printf("Ghi data do cháy\n");
            //     write_data_to_sdcard();
            //     free(message_packet);
            //     perAlarm = false;
            //     isNomal = false;
            //     //Anormal = false;
                
            // }
            // if(gateway_data.mqtt_fault && perFault){
            //     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            //     memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "fault");
            //     //esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     if(!gateway_data.mqtt_status){
            //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
            //     } else{
            //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     }
            //     //printf("Ghi data do lỗi\n");
            //     write_data_to_sdcard();
            //     free(message_packet);
            //     perFault = false;
            //     isNomal = false;
            //     //Fnormal = false;
            // }
            // if(gateway_data.disconnect_device && perDis){
            //     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            //     memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "disconnect");
            //     //esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     if(!gateway_data.mqtt_status){
            //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
            //     } else{
            //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     }
            //     //printf("Ghi data do disconnect\n");
            //     write_data_to_sdcard();
            //     free(message_packet);
            //     perDis = false;
            //     isNomal = false;
            //     //Dnormal = false;
            // }
            // if( !gateway_data.mqtt_alarm || !gateway_data.mqtt_fault || !gateway_data.disconnect_device){
            //     perAlarm = (!gateway_data.mqtt_alarm) ? true : false;
            //     perFault = (!gateway_data.mqtt_fault) ? true : false;
            //     perDis = (!gateway_data.disconnect_device) ? true : false;

            // }

            // //if((!gateway_data.mqtt_alarm || !gateway_data.mqtt_fault || !gateway_data.disconnect_device) && (!isNomal)){
            // if(!gateway_data.mqtt_alarm && !gateway_data.mqtt_fault && !isNomal && !gateway_data.disconnect_device ){
            //     printf("ALL FINE\n");
            //     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            //     memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "normal"); //2904
            //     if(!gateway_data.mqtt_status){
            //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
            //     } else{
            //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     }
            //     //esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     isNomal = true;
            //     write_data_to_sdcard();
            //     free(message_packet);
            // }
            
            // if(esp_timer_get_time() - perivos_time_send >= PUB_NORMAL_MQTT_TIME){
                
            //     char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
            //     memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
            //     if(gateway_data.mqtt_alarm){
            //         json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "alarm");
            //     } else if(gateway_data.mqtt_fault){
            //         json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "fault");
            //     } else if(gateway_data.disconnect_device){
            //         json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "disconnect");
            //     }
            //      else{
            //         json_packet_message_gateway_data(message_packet, MESSAGE_TYPE_HEARTBEAT, "normal");
            //     }
            //     //esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     if(!gateway_data.mqtt_status){
            //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
            //     } else{
            //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
            //     }
            //     perivos_time_send = esp_timer_get_time();
            //     free(message_packet);
            // } 
            EventType new_event = get_current_priority_event();
            const char* event_names[] = { "none", "alarm", "fault", "disconnect" };

            // Nếu đang có một sự kiện trước đó, nhưng bây giờ nó đã hết
            if (current_event_type != EVENT_NONE && current_event_type != new_event) {
                send_event("normal"); // gửi normal cho sự kiện cũ
                current_event_type = EVENT_NONE;  // reset trạng thái
            }

            // Nếu có một sự kiện mới xuất hiện sau khi sự kiện cũ đã kết thúc
            if (new_event != EVENT_NONE && new_event != current_event_type) {
                send_event(event_names[new_event]);
                current_event_type = new_event;
            }
            // Heartbeat định kỳ
            if (esp_timer_get_time() - perivos_time_send >= PUB_NORMAL_MQTT_TIME) {
                if (gateway_data.mqtt_alarm) send_gateway_heartbeat("alarm");
                else if (gateway_data.mqtt_fault) send_gateway_heartbeat("fault");
                else if (gateway_data.disconnect_device) send_gateway_heartbeat("disconnect");
                else send_gateway_heartbeat("normal");

                perivos_time_send = esp_timer_get_time();
            }

            if(gateway_data.event_network){
                gateway_data.event_network = false;
                if (gateway_data.mqtt_alarm) send_event("alarm");
                else if (gateway_data.mqtt_fault) send_event("fault");
                else if (gateway_data.disconnect_device) send_event("disconnect");
                else send_event("normal");
                // char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
                // memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
                // if(gateway_data.mqtt_alarm){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "alarm");
                // } else if(gateway_data.mqtt_fault){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "fault");
                // } else if(gateway_data.disconnect_device){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "disconnect");
                // }
                //  else{
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "normal");
                // }
                // write_data_to_sdcard();
                // if(!gateway_data.mqtt_status){
                // mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
                // } else{
                // esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                // }
                // free(message_packet);
            }

            if(state_power != gateway_data.device_state){ // mất nguồn thì báo server
                state_power = gateway_data.device_state;
                if (gateway_data.mqtt_alarm) send_event("alarm");
                else if (gateway_data.mqtt_fault) send_event("fault");
                else if (gateway_data.disconnect_device) send_event("disconnect");
                else send_event("normal");
                // char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
                // memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
                // if(gateway_data.mqtt_alarm ){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "alarm");
                // }
                // else if(gateway_data.mqtt_fault){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "fault");
                // }
                // else if(gateway_data.disconnect_device){
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "disconnect");
                // }
                // else{
                //     json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "normal");
                // }
                // write_data_to_sdcard();
                // //printf("Ghi data do mất nguồn\n");

                // if(!gateway_data.mqtt_status){
                //     mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
                // } else{
                //     esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                // }
                // free(message_packet);
            }

            if(gateway_data.update_sub_device){
                char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
                memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
                json_packet_message_update_subdevice(message_packet);
                if(!gateway_data.mqtt_status){
                mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
                } else{
                esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                }
                gateway_data.update_sub_device = false;
                free(message_packet);
                //frist_event = true;
            } 

            //if(gateway_data.btn_test && (!gateway_data.mqtt_alarm || !gateway_data.mqtt_fault)){
            if(gateway_data.btn_test && !gateway_data.mqtt_alarm){
                char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
                memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));
                json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "alarm");
                if(!gateway_data.mqtt_status){
                mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
                } else{
                esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                }
                bool test = false;
                printf("\nTest function: Start\n");
                for(int i = 0; i < 30; i++){
                    test = true;
                    //if(gateway_data.mqtt_alarm || gateway_data.mqtt_fault ){
                    if(gateway_data.mqtt_alarm || gateway_data.mqtt_fault ){
                        gateway_data.btn_test = false;
                        break;
                    }
                    printf("\ntime_test: %d s",i);
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                }
                printf("\nTest function: Stop\n");
                json_packet_message_gateway_event(message_packet, MESSAGE_TYPE_EVENT, "normal"); // sửa test thành alarm
                if(!gateway_data.mqtt_alarm && !gateway_data.mqtt_fault && test){
                    esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                }
                gateway_data.btn_test = false;
                free(message_packet);
            }

            if(gateway_data.factory == true){
                gateway_data.factory = false;
                printf("\nRESET FACTORY\n");
                char *message_packet = (char *)malloc(MQTT_HEAP_SIZE * sizeof(char));
                memset(message_packet, 0x00, MQTT_HEAP_SIZE * sizeof(char));    
                json_packet_message_reset_factory(message_packet);
                if(!gateway_data.mqtt_status){
                    printf("\nPush mqtt factory late!!!\n");
                    mqtt_buffer_add(upstream_topic,message_packet, 1, 0);
                } else{
                    printf("\nPush mqtt factory now!!!\n");
                    esp_mqtt_client_publish(client, upstream_topic, message_packet, strlen(message_packet), 1, 0);
                }
                gateway_data.update_sub_device = false;
                free(message_packet);
            }
        }    
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
void mqtt_start(void){
    
    snprintf(client_id, sizeof(client_id), "FireAI/FWT-02/%s", imei);
    snprintf(upstream_topic, sizeof(upstream_topic), "FireAI/FWT-02/upstream/%s", imei);
    snprintf(downstream_topic, sizeof(downstream_topic), "FireAI/FWT-02/downstream/%s", imei);
    snprintf(config_4G, sizeof(config_4G), CONFIG_SSID_4G, imei);

    mqtt_init();
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    printf("Uptopic, %s\n",upstream_topic);
    printf("Dowtopic, %s\n",downstream_topic);
    printf("ID, %s\n",client_id);
    //xTaskCreate(mqtt_send_task, "mqtt_send_task", 2048, NULL, 6, &mqttTaskHandle);
    xTaskCreatePinnedToCore(mqtt_send_task, "mqtt_send_task", 4096, NULL, 9, &mqttTaskHandle, 1);
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
 }

 


