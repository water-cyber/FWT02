#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_eth.h"
#include "esp_timer.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_mac.h"

#include "ethernet_w5500.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "gpio_in_out.h"
#include "json_parser.h"
#include "mqtt_task.h"

#include "ping/ping_sock.h"
#include "common_interface.h"
#include "ota_task.h"

#define TAG "MAIN"

#define DELAY_CHECK 180*1000*1000 //180 giây
#define DELAY_RETRY 5*1000
//#define ssid_4G "LTE-D8DC" // Wifi 4G
#define WIFI_4G_PASS "fireai826" // PASS Wifi 4G

esp_netif_t *wifi_netif = NULL;
gateway_data_t gateway_data;
const char *connect_options[] = {
    "", "wifi", "ethernet", "ethernet, wifi",
    "4G", "4G, wifi", "4G, ethernet", "4G, ethernet, wifi"
}; // 000, 001,010,011,100,101,110,111

char ssid_home[32] = {0}, password_home[64] = {0};
char ssid_4G[32] = {0};
char imei[20];
static bool ping_success = false;

uint64_t perivos_time_delay = 0;
bool connected_4G = false;
bool connected_eth = false;
bool connected_router = false;
bool eth_running  = false;
bool is_phycal_eth = false;
bool wait_ip = false;
bool is_ip_eth = false;

static void log_event(const char *msg, uint8_t *mac_addr) {
    if (mac_addr) {
        ESP_LOGI(TAG, "%s, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 msg, mac_addr[0], mac_addr[1], mac_addr[2], 
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        ESP_LOGI(TAG, "%s", msg);
    }
}

static void network_event_handler(void *arg, esp_event_base_t event_base, 
                                  int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        uint8_t mac_addr[6] = {0};
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
                log_event("Ethernet connected", mac_addr);
                is_phycal_eth = true;
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                log_event("Ethernet disconnected", NULL);
                is_phycal_eth = false;
                break;
            case ETHERNET_EVENT_START:
                log_event("Ethernet started", NULL);
                eth_running = true;
                break;
            case ETHERNET_EVENT_STOP:
                log_event("Ethernet stopped", NULL);
                eth_running = false;
                break;
        }

    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                log_event("START WIFI", NULL);
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                log_event("STA_DISCONNECTED", NULL);
                break;
        }

    } else if (event_base == IP_EVENT) {
        // IP_EVENT dùng chung cho Wi-Fi và Ethernet
        if (event_id == IP_EVENT_STA_GOT_IP || event_id == IP_EVENT_ETH_GOT_IP) {
            if(event_id == IP_EVENT_ETH_GOT_IP){
                is_ip_eth = true;
            }
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            // Có thể đặt thêm logic: bật LED, set event group bit...
            wait_ip = true;
        }
    }
}

static void get_mac_as_imei(char *imei_buffer, size_t buffer_size) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(imei_buffer, buffer_size, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);      
}

void wifi_init_sta() {
    wifi_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());  
    ESP_LOGI(TAG, "WiFi initialized.");
    // Thiết lập TX Power là 18 dBm
    esp_err_t ret = esp_wifi_set_max_tx_power(60);  // 60 * 0.25 = 18 dBm
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set TX power: %s", esp_err_to_name(ret));
    }

    // Đọc lại TX Power
    int8_t power = 0;
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK) {
        printf("TX Power hiện tại: %.2f dBm\n", power * 0.25);
    }
}

static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args) {
    uint32_t elapsed_time;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &elapsed_time, sizeof(elapsed_time));
    ESP_LOGI(TAG, "Ping success, time: %lu ms", elapsed_time);
    ping_success = true;
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args) {
    ESP_LOGW(TAG, "Ping timeout");
}
// Hàm thực hiện ping
static int do_ping_cmd(void) {
    ping_success = false;  // Reset trạng thái

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr.type = IPADDR_TYPE_V4;
    config.count = 1;  // Chỉ ping 1 lần

    if (ipaddr_aton("8.8.8.8", &config.target_addr) == 0) {
        ESP_LOGE(TAG, "Failed to parse IP address");
        return -1;
    }

    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = cmd_ping_on_ping_success,
        .on_ping_timeout = cmd_ping_on_ping_timeout,
        .on_ping_end = NULL,
    };

    esp_ping_handle_t ping;
    esp_err_t ret = esp_ping_new_session(&config, &cbs, &ping);
    if (ret != ESP_OK || ping == NULL) {
        ESP_LOGE(TAG, "Failed to create ping session, error: %d", ret);
        return -1;
    }

    if (esp_ping_start(ping) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ping session");
        esp_ping_delete_session(ping);
        return -1;
    }
    int timeout = 500;  // 5 giây
    while (timeout > 0 && !ping_success) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Chờ 500ms
        timeout -= 100;
    }
    esp_ping_delete_session(ping);  // Xóa session sau khi ping xong
    return ping_success ? 1 : 0;
}

static bool connect_ethernet(){
    if(!is_phycal_eth){
        printf("No ethernet interface\n");
        return false;
    }
    if(!is_ip_eth){
        esp_netif_dhcpc_start(eth_netif);  // Bắt đầu lại DHCP client
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Đợi lấy IP
    for (int i = 0; i < 3; i++) {  // Ping 3 lần
        if (do_ping_cmd()) {  // Nếu ping thành công trong 1 lần
            ESP_LOGI("CHECK NETWORK", "Ethernet is active");
            connected_eth = true;
            return true;
        }
    }
    ESP_LOGI("CHECK NETWORK", "Ethernet is ping fail");
    connected_eth = false;
    return false;
}

void disconnect_ethernet() {
    if (is_ip_eth || is_phycal_eth) {
        is_ip_eth = false;
        esp_netif_dhcpc_stop(eth_netif);  // Dừng DHCP client
        esp_netif_set_ip_info(eth_netif, &(esp_netif_ip_info_t){0});  // Xóa IP
        connected_eth = false;
        ESP_LOGI("CHECK NETWORK", "Ethernet disconnected");
    }
}

static bool connect_wifi(const char *ssid, const char *password) {
    
    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);

    // Reset trạng thái IP và kết nối
    wait_ip = false;
    connected_router = false;
    connected_4G = false;

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    //vTaskDelay(2000 / portTICK_PERIOD_MS); // Đợi lấy IP

    // 🕐 Đợi IP tối đa 5 giây
    int wait_time = 0;
    const int WAIT_TIMEOUT_MS = 5000;
    while (!wait_ip && wait_time < WAIT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100)); // chờ 100ms mỗi vòng
        wait_time += 100;
    }

    if (!wait_ip) {
        ESP_LOGW(TAG, "Timeout waiting for IP");
        // Nếu chưa có IP thì coi như thất bại
        if (strcmp(ssid, ssid_home) == 0) {
            ESP_LOGI("CHECK NETWORK", "ROUTER is fail (no IP)");
            connected_router = false;
        } else {
            ESP_LOGI("CHECK NETWORK", "4G is fail (no IP)");
            connected_4G = false;
        }
        return false;
    }

    ESP_LOGI(TAG, "Got IP, start ping check...");

    for (int i = 0; i < 3; i++) {  // Ping 3 lần
        if (do_ping_cmd()) {  // Nếu ping thành công trong 1 lần
            if (strcmp(ssid, ssid_home) == 0) {
                ESP_LOGI("CHECK NETWORK", "ROUTER is active");
                connected_router = true;
            } else {
                ESP_LOGI("CHECK NETWORK", "4G is active");
                connected_4G = true;
            }
            return true;  // Chỉ cần 1 lần thành công là kết thúc luôn
        }
    }
    if (strcmp(ssid, ssid_home) == 0){
        ESP_LOGI("CHECK NETWORK", "ROUTER is fail");
        connected_router = false;
    } else{
        ESP_LOGI("CHECK NETWORK", "4G is fail");
        connected_4G = false;
    }
    return false;
}

bool is_wifi_configured() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;  // Không tìm thấy dữ liệu
    }

    size_t len;
    err = nvs_get_str(nvs_handle, "ssid", NULL, &len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    // Đọc dữ liệu từ NVS
    size_t ssid_len = sizeof(ssid_home);
    size_t pass_len = sizeof(password_home);
    nvs_get_str(nvs_handle, "ssid", ssid_home, &ssid_len);
    nvs_get_str(nvs_handle, "password", password_home, &pass_len);
    nvs_close(nvs_handle);
    return true;  // SSID đã được lưu
}

bool is_4G_configured(){
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("4G_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        printf("No find data \n");
        return false;  // Không tìm thấy dữ liệu
    }
    size_t ssid_len = sizeof(ssid_4G);
    nvs_get_str(nvs_handle, "ssid", ssid_4G, &ssid_len);
    printf("SSID 4G: %s \n", ssid_4G);
    nvs_close(nvs_handle);
    return true;
}

static void disconnect_for_case4(int network_type, int option){
    switch (option)
    {
    case 0:
        if (network_type == WIFI_HOME) {
            esp_wifi_disconnect(); 
            gateway_data.home_wifi = false; 
            // //connected_router = false;
        } else if (network_type == WIFI_DEFAULT) {
            esp_wifi_disconnect(); 
            gateway_data.default_wifi = false; 
            //connected_4G = false;
        } else if (network_type == ETHERNET) {
            disconnect_ethernet(); 
            gateway_data.ethernet = false; 
            //connected_eth = false;
        }
        break;
    case 1:
        if (network_type == WIFI_HOME) {
            esp_wifi_disconnect(); 
        } else if (network_type == WIFI_DEFAULT) {
            esp_wifi_disconnect(); 
        } else if (network_type == ETHERNET) {
            disconnect_ethernet(); 
        }
    default:
        break;
    }
}

// Từ case 0 - case 2 kiểm tra mạng
// case 3 chọn mạng kết nối theo thứ tự ưu tiên 4G - ethernet - wifi
// case 4 loop sau các case trước cho đến khi quá 180s hoặc mất kết nối
void task1_network_management(void *arg) {
    static uint8_t check_internet = 0; // biến kiểm tra internet
    static uint8_t ping_fail_count = 0; // biến kiểm tra lỗi ping quá 3 lần 
    static uint8_t fail_connect_infor = -1; // biến kiểm tra mạng lỗi trước đó để bỏ check
    static uint8_t per_connected_infor = -1;
    static uint8_t check_list = 0; // check in4
    static bool first_event = false;
    static char per_list[20];
    while (1) {
        switch (check_internet)
        {
        case 0:
            printf("CASE 0\n");
            disconnect_ethernet();
            if(per_connected_infor == WIFI_DEFAULT && fail_connect_infor != WIFI_DEFAULT){
                ESP_LOGI("TASK", "4G worked fine before!");
                gateway_data.default_wifi = true;
            }
            else if (fail_connect_infor != WIFI_DEFAULT && connect_wifi(ssid_4G, WIFI_4G_PASS)) {
                ESP_LOGI("TASK", "4G works fine!");
                gateway_data.default_wifi = true;
                esp_wifi_disconnect();
            } else {
                gateway_data.default_wifi = false;
                
                esp_wifi_disconnect();
            }
            check_internet = 1;
            break;
        case 1:
            printf("CASE 1\n");
            if(per_connected_infor == ETHERNET && fail_connect_infor != ETHERNET){
                ESP_LOGI("TASK", "Ethernet works fine before!");
                gateway_data.ethernet = true;
            }
            else if (fail_connect_infor != ETHERNET && connect_ethernet()) {
                ESP_LOGI("TASK", "Ethernet works fine!");
                gateway_data.ethernet = true;
            } else {
                gateway_data.ethernet = false;
            }
            disconnect_ethernet();
            check_internet = 2;
            break;
        case 2:
            printf("CASE 2\n");
            if(gateway_data.skip_wifi){
                ESP_LOGI("TASK", "SKIP wifi home!");
                check_internet = 3;
            } else if(fail_connect_infor == WIFI_HOME){
                check_internet = 3; 
            } else{
                if(per_connected_infor == WIFI_HOME && (gateway_data.default_wifi || gateway_data.ethernet)){
                    gateway_data.home_wifi = true;
                } else if(connect_wifi(ssid_home,password_home)){
                    ESP_LOGI("TASK", "ROUTER works fine!"); 
                    gateway_data.home_wifi = true;
                } else{
                    gateway_data.home_wifi = false;
                }
                if(!gateway_data.default_wifi && !gateway_data.ethernet && gateway_data.home_wifi){
                    ESP_LOGI("TASK", "Connected to ROUTER!");
                    gateway_data.connect_info = WIFI_HOME;
                    gateway_data.home_wifi = true;
                    //perivos_time_delay = esp_timer_get_time();
                    //gateway_data.event_enable = true;
                    check_internet = 4;
                } else{
                    esp_wifi_disconnect();
                    check_internet = 3;
                }
            }
            break;
        case 3:
            printf("CASE 3\n");
            if (gateway_data.default_wifi) {
                connect_wifi(ssid_4G, WIFI_4G_PASS);
                ESP_LOGI("TASK", "Connected to 4G!");
                gateway_data.connect_info = WIFI_DEFAULT;
                check_internet = 4;
            } else if (gateway_data.ethernet) {
                connect_ethernet();
                ESP_LOGI("TASK", "Connected to ETHERNET!");
                gateway_data.connect_info = ETHERNET;
                check_internet = 4;
            } else{
                ESP_LOGI("TASK", "NO CONNECTION AVAILABLE!!!");
                check_internet = 0;
                fail_connect_infor = -1;
                per_connected_infor = -1;
                //gateway_data.event_network = false;
                gateway_data.network_connected = false;
                break;
            }
            break;
        case 4:
            printf("CASE 4\n");
            perivos_time_delay = esp_timer_get_time();
            gateway_data.event_enable = true;
            
            printf("event_enable : TRUE\n");
            if(!first_event){ // chỉ nhảy vào lầu đầu tiên
                first_event =  true;
                check_list = (gateway_data.default_wifi << 2) | (gateway_data.ethernet << 1) | (gateway_data.home_wifi);
                strcpy(gateway_data.connect_avail, connect_options[check_list]);
            }
            strcpy(per_list,gateway_data.connect_avail);
            check_list = (gateway_data.default_wifi << 2) | (gateway_data.ethernet << 1) | (gateway_data.home_wifi);
            strcpy(gateway_data.connect_avail, connect_options[check_list]);
            if (strcmp(per_list, connect_options[check_list]) == 0){
                gateway_data.event_network = false;
            } else{
                gateway_data.event_network = true;
            }
            
            check_internet = 5;
            break;
        case 5:
            printf("CASE 5\n");
            gateway_data.network_connected = true;
            if (esp_timer_get_time() - perivos_time_delay > DELAY_CHECK) {
                ping_fail_count = 0; // Reset lại bộ đếm ping fail
                disconnect_for_case4(gateway_data.connect_info, 1);
                gateway_data.event_enable = false;
                per_connected_infor = gateway_data.connect_info;
                printf("event_enable : FALSE\n");
                check_internet = 0;
                break;
            } else if (!do_ping_cmd()) {
                ping_fail_count++;
                if (ping_fail_count >= 3) { // Nếu ping fail 3 lần liên tiếp
                    ESP_LOGW("TASK", "PING FAIL 3 time, reset connect...");
                    disconnect_for_case4(gateway_data.connect_info, 0);
                    gateway_data.event_enable = false;
                    printf("event_enable : FALSE\n");
                    fail_connect_infor = gateway_data.connect_info;
                    per_connected_infor = -1;
                    check_internet = 0;
                    ping_fail_count = 0; // Reset lại bộ đếm
                    break;
                }
            } else {
                ping_fail_count = 0; // Nếu ping thành công thì reset bộ đếm
                fail_connect_infor = -1;
                
            }
            vTaskDelay(pdMS_TO_TICKS(10000)); 
            break;
        default:
            break;
        }
    }   
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Đăng ký handler cho sự kiện Ethernet
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL, NULL));
    // Đăng ký handler cho sự kiện WiFi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL, NULL));
    // Đăng ký handler cho sự kiện IP (khi WiFi hoặc Ethernet có IP)
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL, NULL));
    led_start();
    ethernet_w5500_init();
    sd_card_init();
    printf("Start Ethernet\n");
    ESP_ERROR_CHECK(esp_eth_start(eth_handle)); 
    get_mac_as_imei(imei, sizeof(imei));
    ESP_LOGI(TAG, " ESP MAC Addr: %s",imei);
    get_hashcode_ota();
    is_4G_configured();
    if (!is_wifi_configured()){
        wifi_init_softap();
        // Khởi động Web Server
        start_web_server();
        // Đợi đến khi nhận được SSID/PASS
        wait_for_wifi_credentials();
        gateway_data.mode = AP;
    }
    
    wifi_init_sta();
    gateway_data.mode = STA;
    gateway_data.default_wifi = false;
    gateway_data.home_wifi = false;
    gateway_data.ethernet = false;
    gateway_data.event_enable = false;
    gateway_data.ota_running = false;
    ESP_LOGI(TAG, "WIFI_HOME: %s", ssid_home);
    ESP_LOGI(TAG, "PASS_WIFI: %s", password_home);
    if (password_home[0] == '\0') {
        ESP_LOGW(TAG, "Password is empty — skipp connect wifi home/n");
        gateway_data.skip_wifi = true;
    //wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else{
        gateway_data.skip_wifi = false;
    }
    xTaskCreatePinnedToCore(task1_network_management, "task1_network_management", 8192, NULL, 8, NULL, 1);
    while (!gateway_data.event_enable){
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    obtain_time();
    vTaskDelay(pdMS_TO_TICKS(5000));
    mqtt_start();

    printf("FWT02! \n");
}
