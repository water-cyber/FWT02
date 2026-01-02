#include "json_parser.h"
#define TAG "JSON"

char command[32];
char sub_device_id[24];


void json_packet_message_gateway_event(char *message_packet, char *message_type, const char *device_status)
{   
    char *str1 = NULL;
    snprintf(sub_device_id, sizeof(sub_device_id), "%s01", imei);
    //cJSON *root = NULL;
    cJSON *device_info = NULL;
    uuid_t request_id;
    char uu_str[UUID_STR_LEN];

    uuid_generate(request_id);
    uuid_unparse(request_id, uu_str);

    char *device_state[] = {"normal", "ac", "dc"};
    //char *device_status[] = {"normal", "alarm", "fault"};
    char *gw_connect_info[] = {"wifi", "4G", "ethernet"};

    device_info = cJSON_CreateObject();
    //cJSON_AddItemToObject(root, "request_id", cJSON_CreateString(uu_str));
    //cJSON_AddItemToObject(root, "states", device_info = cJSON_CreateObject());
    //cJSON_AddItemToObject(root, device_info = cJSON_CreateObject());
    cJSON_AddStringToObject(device_info, "message_type", message_type);
    cJSON_AddStringToObject(device_info, "device_id", imei);
    //cJSON_AddStringToObject(device_info, "device_type", DEVICE_TYPE);
    
    if (strcmp(device_status, "disconnect") == 0) {
        cJSON_AddStringToObject(device_info, "device_status", "fault");
    }else{
        cJSON_AddStringToObject(device_info, "device_status", device_status);
    }
    
    cJSON_AddStringToObject(device_info, "device_state", device_state[gateway_data.device_state]);
    cJSON_AddStringToObject(device_info, "connect_type", gw_connect_info[gateway_data.connect_info]);
    cJSON_AddStringToObject(device_info, "connection_available", gateway_data.connect_avail);

    cJSON_AddStringToObject(device_info, "sub_device_id", sub_device_id);
    cJSON_AddStringToObject(device_info, "sub_device_status", device_status);
    str1 = cJSON_PrintUnformatted(device_info);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(device_info);
}

void json_packet_message_gateway_data(char *message_packet, char *message_type, const char *device_status)
{
    char *str1 = NULL;
    
    //cJSON *root = NULL;
    cJSON *device_info = NULL;
    uuid_t request_id;
    char uu_str[UUID_STR_LEN];

    uuid_generate(request_id);
    uuid_unparse(request_id, uu_str);

    char *device_state[] = {"normal", "ac", "dc"};
    //char *device_status[] = {"normal", "alarm", "fault"};
    char *gw_connect_info[] = {"wifi", "4G", "ethernet"};

    device_info = cJSON_CreateObject();
    //cJSON_AddItemToObject(root, "request_id", cJSON_CreateString(uu_str));
    //cJSON_AddItemToObject(root, "states", device_info = cJSON_CreateObject());
    //cJSON_AddItemToObject(root, device_info = cJSON_CreateObject());
    cJSON_AddStringToObject(device_info, "device_id", imei);
    cJSON_AddStringToObject(device_info, "device_type", DEVICE_TYPE);
    
    cJSON_AddStringToObject(device_info, "device_status", device_status);
    cJSON_AddStringToObject(device_info, "device_state", device_state[gateway_data.device_state]);

    cJSON_AddStringToObject(device_info, "message_type", message_type);
    cJSON_AddStringToObject(device_info, "connect_type", gw_connect_info[gateway_data.connect_info]);
    cJSON_AddStringToObject(device_info, "connection_available", gateway_data.connect_avail);
    cJSON_AddStringToObject(device_info, "hash_code", gateway_data.current_hash);
    str1 = cJSON_PrintUnformatted(device_info);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(device_info);
}

void json_packet_message_update_subdevice(char *message_packet){

    char *str1 = NULL;
    snprintf(sub_device_id, sizeof(sub_device_id), "%s01", imei);
    cJSON *root = NULL;
    //cJSON *sub_device_info = NULL;
    uuid_t request_id;
    char uu_str[UUID_STR_LEN];
    //char device_id[]
    uuid_generate(request_id);
    uuid_unparse(request_id, uu_str);

    root = cJSON_CreateObject();
    // cJSON_AddItemToObject(root, "request_id", cJSON_CreateString(uu_str));
    // cJSON_AddItemToObject(root, "states", device_info = cJSON_CreateObject());   
    // cJSON_AddItemToObject(root, "device_id", cJSON_CreateString("AX_00"));
    // cJSON_AddItemToObject(root, "message_type", sub_device_info = cJSON_CreateObject());  
    // cJSON_AddStringToObject(sub_device_info, "sub_device_id", "001");
    // cJSON_AddStringToObject(sub_device_info, "sub_device_type", "camera");
    cJSON_AddStringToObject(root, "device_id", imei); // Thêm key "device_id"
    cJSON_AddStringToObject(root, "message_type", "update_sub_device"); // Thêm key "message_type"
    // Tạo đối tượng con "sub_device_info"
    //sub_device_info = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "sub_device_id", sub_device_id); // Thêm key "sub_device_id"
    cJSON_AddStringToObject(root, "sub_device_type", "FCB"); // Thêm key "sub_device_type"
    // Gắn "sub_device_info" vào root
    //cJSON_AddItemToObject(root, "sub_device_info", sub_device_info);
    str1 = cJSON_PrintUnformatted(root);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(root);
}

// Hàm xử lý từng lệnh cụ thể
void handle_command(const char *cmd)
{
    gateway_data.cmd_server = true;
    if (strcmp(cmd, "slo") == 0) {
        ESP_LOGI(TAG, "Silen ON received.");
        // Thêm logic xử lý lệnh "slo" ở đây
        gateway_data.btn_silen = true;
    } else if (strcmp(cmd, "slf") == 0) {
        ESP_LOGI(TAG, "Silen OFF received");
        // Thêm logic xử lý lệnh "slf" ở đây
        gateway_data.btn_silen = false;
    } else if (strcmp(cmd, "rst") == 0) {
        ESP_LOGI(TAG, "Reset Device Command received.");
        send_event("normal");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();  // Khởi động lại thiết bị
    } else if (strcmp(cmd, "ota") == 0) {
        ESP_LOGI(TAG, "OTA Update Command received.");
        // Thêm logic xử lý lệnh "ota" ở đây
    } else if (strcmp(cmd, "usd") == 0) {
        ESP_LOGI(TAG, "Update Settings/Device Command received.");
        // Thêm logic xử lý lệnh "usd" ở đây
        gateway_data.update_sub_device = true;
    } else {
        ESP_LOGW(TAG, "Unknown Command: %s", cmd);
        //gateway_data.update_sub_device = true;
    }
}
void json_parser_gw_test(char *message_packet){
    cJSON *root = NULL;
    char *str1 = NULL;
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", imei); // Thêm key "device_id"
    cJSON_AddStringToObject(root, "message_type", "test"); // Thêm key "message_type"
    str1 = cJSON_PrintUnformatted(root);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(root);
}

void json_packet_message_reset_factory(char *message_packet){
    char *str1 = NULL;
    cJSON *root = NULL;
    // cJSON *sub_device_info = NULL;
    uuid_t request_id;
    char uu_str[UUID_STR_LEN];
    //char device_id[]
    uuid_generate(request_id);
    uuid_unparse(request_id, uu_str);

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", imei); // Thêm key "device_id"
    cJSON_AddStringToObject(root, "cmd", "factory");
    str1 = cJSON_PrintUnformatted(root);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(root);
}

void json_parser_gw_cmd_server(char *message_packet){
    cJSON *root = NULL;
    char *str1 = NULL;
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", imei); // Thêm key "device_id"
    cJSON_AddStringToObject(root, "cmd", command); // Thêm key "message_type"
    str1 = cJSON_PrintUnformatted(root);
    strcpy(message_packet, str1);
    free(str1);
    cJSON_Delete(root);
}

bool json_parser_gw_cmd(const char *message, uint16_t length, const char *imei){
    bool status = true;
    
    cJSON *root2 = cJSON_ParseWithLength(message, length);
    if (!root2) {
        ESP_LOGE(TAG, "Invalid JSON message");
        return false;
    }

    // Lấy device_id từ JSON
    cJSON *device_id = cJSON_GetObjectItem(root2, "device_id");
    if (!cJSON_IsString(device_id) || (device_id->valuestring == NULL)) {
        ESP_LOGE(TAG, "Invalid or missing device_id");
        status = false;
        goto end;
    }

    ESP_LOGI(TAG, "Received Device ID: %s", device_id->valuestring);

    // So sánh device_id với ime
    if (strcmp(device_id->valuestring, imei) != 0) {
        ESP_LOGW(TAG, "Device ID does not match. Ignoring command.");
        status = false;
        goto end;
    }

    // Lấy cmd từ JSON
    cJSON *cmd = cJSON_GetObjectItem(root2, "cmd");
    if (!cJSON_IsString(cmd) || (cmd->valuestring == NULL)) {
        ESP_LOGE(TAG, "Invalid or missing cmd");
        status = false;
        goto end;
    }

    ESP_LOGI(TAG, "Executing Command: %s", cmd->valuestring);

    // Thực hiện lệnh
    handle_command(cmd->valuestring);

end:
    cJSON_Delete(root2); // Giải phóng bộ nhớ JSON
    ESP_LOGI(TAG, "End processing message.");
    return status;

// ESP_LOGD(TAG, "Processing command...");

//     // Đảm bảo chuỗi đầu vào không rỗng
//     if (message == NULL || length == 0) {
//         ESP_LOGE(TAG, "Invalid message or empty command.");
//         return false;
//     }

//   // Đảm bảo chuỗi kết thúc bằng ký tự null và sao chép vào biến toàn cục
//     if (length >= sizeof(command)) {
//         ESP_LOGE(TAG, "Command length exceeds buffer size.");
//         return false;
//     }
//     strncpy(command, message, length);
//     command[length] = '\0';

//     ESP_LOGI(TAG, "Received Command: %s", command);

//     // Gọi hàm xử lý lệnh
//     handle_command(command);

//     ESP_LOGI(TAG, "Finished processing command.");
//     return true;
}