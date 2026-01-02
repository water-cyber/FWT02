#include "gpio_in_out.h"

#define KEY_SCAN_TIME 20*1000
#define USER_RESET_TIME 3000*1000
#define BTN_TIME 10*1000
#define GW_BUSY_BLINK_TIME_SHORT 500*1000
#define GW_BUSY_BLINK_TIME_CHARGE 2*1000*1000
#define GW_BUSY_BLINK_TIME_LONG 10*1000*1000
#define GW_BUSY_BLINK_TIME_MIDDLE 2*1000*1000
#define DEBOUNCE_TIME_MS 1000
#define STABLE_TIME_MS 3000  // Thời gian yêu cầu giữ mức cao
typedef enum {
    STATE_IDLE,          // Trạng thái chờ
    STATE_WAIT_STABLE    // Đang chờ tín hiệu ổn định
} gpio_check_state_t;

gpio_check_state_t gpio_state = STATE_IDLE;  // Quản lý chung cho cả 2 pin
static gpio_check_state_t fire_state = STATE_IDLE;
static gpio_check_state_t fault_state = STATE_IDLE;
static int64_t fire_start_time = 0;
static int64_t fault_start_time = 0;
int64_t start_time = 0;  // Thời điểm bắt đầu đo thời gian
uint16_t led_pattern = 0x0000;
uint64_t prev_key_scan= 0;
uint8_t button_state = 0;
uint8_t flag = 0;
uint64_t perivos_time_button = 0;

uint8_t cur_btn_state = 0;
uint8_t last_btn_state = 1;
//uint64_t charger_time = 0;
uint8_t gw_mode_e = 0;
uint8_t gw_mode_d = 0;
uint8_t gw_mode_h = 0;
uint8_t gw_mode_m = 0;
uint64_t gw_busy_time_e = 0;
uint64_t gw_busy_time_d = 0;
uint64_t gw_busy_time_h = 0;
uint64_t gw_busy_time_m = 0;

uint8_t p_mode = 0;
uint64_t p_time = 0;
uint8_t btn_update = 0;
int64_t last_btn_update = 0;

bool fault_power = false;
static const char *TAG = "GPIO";  // Define the TAG for logging


void button_update() {
    int button_level = gpio_get_level(PIN_UPDATE_DEVICE); 
    switch (btn_update) {
        case 0: // Chờ nút nhấn
            if (button_level == 0) {
                btn_update = 1;
                last_btn_update = esp_timer_get_time(); // Lưu thời điểm nhấn
            }
            break;

        case 1: // Kiểm tra debounce
            if (esp_timer_get_time() - last_btn_update > 50000) { // 50ms debounce
                if (gpio_get_level(PIN_UPDATE_DEVICE) == 0) { 
                    gateway_data.update_sub_device = true;
                    btn_update = 2; // Chuyển sang trạng thái giữ
                } else {
                    btn_update = 0; // Nếu bị rung, quay về trạng thái chờ
                }
            }
            break;

        case 2: // Chờ nhả nút
            if (button_level == 1) {
                last_btn_update = esp_timer_get_time();
                btn_update = 3; // Kiểm tra debounce khi thả nút
            }
            break;

        case 3: // Kiểm tra debounce khi thả
            if (esp_timer_get_time() - last_btn_update > 50000) { // 50ms debounce
                if (gpio_get_level(PIN_UPDATE_DEVICE) == 1) { 
                    btn_update = 0; // Quay về trạng thái ban đầu
                }
            }
            break;
    }
}

static void pins_init(){
    esp_err_t error;
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUT_PIN,
        .pull_up_en = GPIO_PULLUP_DISABLE,    // Không kích hoạt pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,// Không kích hoạt pull-down
        .intr_type = GPIO_INTR_DISABLE        // Không kích hoạt ngắt
    };
    error = gpio_config(&io_conf);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "error configuring outputs");
    }
     // Cấu hình chân INPUT 
    io_conf.intr_type = GPIO_INTR_DISABLE; 
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_IN_PIN;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    error = gpio_config(&io_conf);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "error configuring outputs");
    }

}

// Gửi 1 bit đến 74HC595
static void shift_register_send_bit(uint8_t bit) {
    gpio_set_level(PIN_DS, bit);
    gpio_set_level(PIN_SHCP, 1);  // Tạo xung đồng hồ
    gpio_set_level(PIN_SHCP, 0);
}

// Gửi 1 byte dữ liệu
static void shift_register_send_byte(uint8_t data) {
    data = ~data;  // Đảo ngược tất cả các bit (bit 0 sẽ trở thành 1 và ngược lại)
    for (int i = 7; i >= 0; i--) {
        shift_register_send_bit((data >> i) & 0x01);  // Gửi từng bit (MSB trước)
    }
}

static void shift_register_send_data(uint16_t data) {
    // Gửi dữ liệu cho cả 2 IC
    uint8_t data_low = (uint8_t)(data & 0xFF);       // 8 bit đầu tiên
    uint8_t data_high = (uint8_t)((data >> 8) & 0xFF); // 8 bit tiếp theo

    shift_register_send_byte(data_high);  // Gửi byte cao
    shift_register_send_byte(data_low); // Gửi byte thấp

    gpio_set_level(PIN_STCP, 1);  // Chốt dữ liệu vào các IC
    gpio_set_level(PIN_STCP, 0);
}

static void button_handler(){

    button_update();
    if (gpio_get_level(PIN_RESET) == 0)
    {
        switch (button_state)
        {
        case 0:
            button_state = 1;
            perivos_time_button = esp_timer_get_time();
            break;
        case 1:
            if (esp_timer_get_time() - perivos_time_button > USER_RESET_TIME)
            {
                // ESP_LOGI(TAG, "Resetting WiFi configuration...");            
                // ESP_ERROR_CHECK(esp_wifi_stop());  // Dừng Wi-Fi hiện tại
                // ESP_ERROR_CHECK(nvs_flash_init());  // Khởi tạo NVS
                // ESP_ERROR_CHECK(nvs_flash_erase());  // Xóa cấu hình Wi-Fi
                // ESP_LOGI(TAG, "WiFi configuration reset done.");
                // esp_restart();  // Khởi động lại thiết bị
                ESP_LOGI(TAG, "Resetting WiFi configuration...");            
                ESP_ERROR_CHECK(esp_wifi_stop());  // Dừng Wi-Fi hiện tại
                nvs_handle_t nvs_handle;
                if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                    nvs_erase_all(nvs_handle);  // hoặc chỉ xóa ssid/password
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                    ESP_LOGI(TAG, "WiFi config reset successfully.");
                } else {
                    ESP_LOGE(TAG, "Failed to open wifi_config namespace.");
                }
                esp_restart();  // Khởi động lại thiết bị
            }
            break;
        default:
            button_state = 0;
            break;
        }
    }
    
    if((gpio_get_level(PIN_RESET_FACTORY) == 0)){
        switch (button_state)
        {
        case 0:
            button_state = 1;
            perivos_time_button = esp_timer_get_time();
            
            break;
        case 1:
            if (esp_timer_get_time() - perivos_time_button > USER_RESET_TIME)
            {
                gateway_data.factory = true;
                perivos_time_button = esp_timer_get_time();
                // gateway_data.update_sub_device = true;
                //uart_send_data(clear_zigbee_cmd, sizeof(clear_zigbee_cmd));
                
                nvs_handle_t nvs_handle;
                if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                    nvs_erase_all(nvs_handle);  // hoặc chỉ xóa ssid/password
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                    ESP_LOGI(TAG, "WiFi config reset successfully.");
                } else {
                    ESP_LOGE(TAG, "Failed to open wifi_config namespace.");
                }
                //ESP_ERROR_CHECK(esp_wifi_stop());  // Dừng Wi-Fi hiện tại
                vTaskDelay(500 / portTICK_PERIOD_MS);
                esp_restart();
                button_state = 2;
            }
            break;
        default:
            button_state = 0;
            break;
        } 
    }

    if((gpio_get_level(PIN_TEST) == 0)){
        
        switch (button_state)
        {
        case 0:
            button_state = 1;
            perivos_time_button = esp_timer_get_time();
            break;
        case 1:
            if (esp_timer_get_time() - perivos_time_button > BTN_TIME)
            {
                gateway_data.btn_test = true;
            }
            break;
        default:
            button_state = 0;
            break;
        } 
    }

    cur_btn_state = gpio_get_level(PIN_SILEN);
    if(cur_btn_state != last_btn_state){
        perivos_time_button = esp_timer_get_time();
    }
    if((esp_timer_get_time() - perivos_time_button) > BTN_TIME){
        if(cur_btn_state == 0 && flag == 0){
            // buzzer = ! buzzer;
            gateway_data.btn_silen = ! gateway_data.btn_silen;
            flag = 1;
        }
        else if(cur_btn_state == 1){
            flag = 0;
        }
    }
    // Cập nhật trạng thái trước đó của nút
    last_btn_state = cur_btn_state;       
}

static void turn_on_led(uint16_t *led_pattern, uint8_t led_index) {
    *led_pattern |= (1 << led_index); // Bật LED
    shift_register_send_data(*led_pattern);
}

static void turn_off_led(uint16_t *led_pattern, uint8_t led_index) {
    *led_pattern &= ~(1 << led_index); // Tắt LED
    shift_register_send_data(*led_pattern);
}

static void handle_led_blink(int led_num, uint8_t  *gw_mode, uint64_t  *gw_busy_time, int short_time, int long_time){
    switch (*gw_mode) {
    case 0:
        *gw_busy_time = esp_timer_get_time();
        *gw_mode = 1;
        turn_on_led(&led_pattern, led_num);
        break;
    case 1:
        if ((esp_timer_get_time() - *gw_busy_time > short_time)) {
            *gw_busy_time = esp_timer_get_time();
            turn_off_led(&led_pattern, led_num);
            *gw_mode = 2;
        }
        break;
    case 2:
        if ((esp_timer_get_time() - *gw_busy_time > long_time)) {
            *gw_mode = 0;
        }
        break;
    default:
        *gw_mode = 0;
        break;
    }
}

static void power_gateway() {
    switch (p_mode) {
        case 0:
            p_mode = 1;
            p_time = esp_timer_get_time();
            break;

        case 1:
            turn_on_led(&led_pattern, 12);
            if ((esp_timer_get_time() - p_time) > GW_BUSY_BLINK_TIME_CHARGE) {
                p_time = esp_timer_get_time();
                printf("Check battery\n");

                if (gpio_get_level(PIN_ADAPTER) == 1 && gpio_get_level(PIN_BAT) == 1) {
                    gateway_data.device_state = STATE_NORMAL;
                    turn_off_led(&led_pattern, 2); 
                    turn_off_led(&led_pattern, 3); 
                    fault_power = false;
                } else if (gpio_get_level(PIN_ADAPTER) == 1) {
                    gateway_data.device_state = DC; //Báo mất DC
                    turn_off_led(&led_pattern, 2); 
                    turn_on_led(&led_pattern, 3);
                    fault_power = true;
                } else if (gpio_get_level(PIN_BAT) == 1) {
                    gateway_data.device_state = AC; //Báo mất AC
                    turn_off_led(&led_pattern, 3);
                    turn_on_led(&led_pattern, 2);
                    fault_power = true;
                } else {
                    //gateway_data.device_state = STATE_UNKNOWN; // Mất cả hai nguồn
                    printf("UNKOWN\n");
                }
                p_mode = 2;
            }
            break;

        case 2:
            if ((esp_timer_get_time() - p_time) > GW_BUSY_BLINK_TIME_LONG) {
                p_mode = 0;
            } 
            turn_off_led(&led_pattern, 12);
            break;
        default:
            p_mode = 0;
            break;
    }
}

void check_fire_and_fault(void)
{
    int64_t current_time = esp_timer_get_time() / 1000; // Lấy thời gian hiện tại (ms)

    // Xử lý debounce PIN_FIRE
    switch (fire_state) {
        case STATE_IDLE:
            if (gpio_get_level(PIN_FIRE) == 1) {
                fire_start_time = current_time;
                fire_state = STATE_WAIT_STABLE ;
            }
            break;
        case STATE_WAIT_STABLE :
            if (gpio_get_level(PIN_FIRE) == 0) {
                fire_state = STATE_IDLE;  // Nếu tụt về 0 thì huỷ
            } else if ((current_time - fire_start_time) >= DEBOUNCE_TIME_MS) {
                // Đã ổn định, xử lý FIRE
                gateway_data.mqtt_alarm = true;
                // gateway_data.btn_test = false;
                gateway_data.device_status = ALARM;
                // if (!gateway_data.btn_silen) {
                //     turn_off_led(&led_pattern, 13); // mạch ngược
                // } else {
                //     turn_on_led(&led_pattern, 13);
                // }
                turn_on_led(&led_pattern, 0); // bật LED FIRE
                fire_state = STATE_IDLE;
            }
            break;
    }

    // Nếu mất tín hiệu FIRE
    if (fire_state == STATE_IDLE && gpio_get_level(PIN_FIRE) == 0 && gateway_data.mqtt_alarm) {
        gateway_data.mqtt_alarm = false;
        turn_off_led(&led_pattern, 0); // Tắt LED Fire
        // turn_on_led(&led_pattern, 13); // tắt out24V (mạch ngược)
        // gateway_data.btn_silen = false;
    }

    // Xử lý debounce PIN_FAULT
    switch (fault_state) {
        case STATE_IDLE:
            if (gpio_get_level(PIN_FAULT) == 1) {
                fault_start_time = current_time;
                fault_state = STATE_WAIT_STABLE ;
            }
            break;
        case STATE_WAIT_STABLE :
            if (gpio_get_level(PIN_FAULT) == 0) {
                fault_state = STATE_IDLE;
            } else if ((current_time - fault_start_time) >= DEBOUNCE_TIME_MS) {
                // Đã ổn định, xử lý FAULT
                gateway_data.mqtt_fault = true;
                gateway_data.device_status = FAULT;
                turn_on_led(&led_pattern, 1); // bật LED Fault
                fault_state = STATE_IDLE;
            }
            break;
    }

    // Nếu mất tín hiệu FAULT
    if (fault_state == STATE_IDLE && gpio_get_level(PIN_FAULT) == 0 && gateway_data.mqtt_fault) {
        gateway_data.mqtt_fault = false;
        //turn_off_led(&led_pattern, 1); // Tắt LED Fault
    } 

    // Nếu cả hai đều tắt, reset trạng thái
    if (!gateway_data.mqtt_alarm && !gateway_data.mqtt_fault && !gateway_data.disconnect_device) {
        gateway_data.device_status = STATUS_NORMAL;
    } else if(!gateway_data.mqtt_alarm && !gateway_data.mqtt_fault && gateway_data.disconnect_device){
        gateway_data.device_status = DISCONNECT;
    }
}

void check_fault_and_fire_connect() {
    int64_t current_time = esp_timer_get_time() / 1000;  // Lấy thời gian hiện tại (ms)

    switch (gpio_state) {
        case STATE_IDLE:
            if (gpio_get_level(PIN_FAULT_CONNECT) == 1 || gpio_get_level(PIN_FIRE_CONNECT) == 1) {
                start_time = current_time;  // Ghi nhận thời gian bắt đầu
                gpio_state = STATE_WAIT_STABLE;
            }
            break;

        case STATE_WAIT_STABLE:
            if (gpio_get_level(PIN_FAULT_CONNECT) == 0 && gpio_get_level(PIN_FIRE_CONNECT) == 0) {
                gpio_state = STATE_IDLE;  // Nếu cả hai pin về thấp trước khi hết thời gian thì huỷ kiểm tra
            } else if ((current_time - start_time) >= STABLE_TIME_MS) {
                turn_on_led(&led_pattern, 14);
                gateway_data.disconnect_device = true;
                gateway_data.device_status = DISCONNECT;
                gpio_state = STATE_IDLE;  // Reset trạng thái sau khi báo lỗi
            }
            break;
    }

    // Nếu cả 2 GPIO đều không kích hoạt thì tắt báo lỗi
    if (gpio_state == STATE_IDLE &&
        gpio_get_level(PIN_FAULT_CONNECT) == 0 && gpio_get_level(PIN_FIRE_CONNECT) == 0 && 
        !gateway_data.mqtt_alarm && !gateway_data.mqtt_fault) {
        turn_off_led(&led_pattern, 14);
        gateway_data.disconnect_device = false;
        gateway_data.device_status = STATE_NORMAL;
    } else if(gpio_state == STATE_IDLE &&
        gpio_get_level(PIN_FAULT_CONNECT) == 0 && gpio_get_level(PIN_FIRE_CONNECT) == 0 && 
        !gateway_data.mqtt_alarm && gateway_data.mqtt_fault) {
        turn_off_led(&led_pattern, 14);
        gateway_data.disconnect_device = false;
        gateway_data.device_status = FAULT;
    } else if(gpio_state == STATE_IDLE &&
        gpio_get_level(PIN_FAULT_CONNECT) == 0 && gpio_get_level(PIN_FIRE_CONNECT) == 0 && 
        gateway_data.mqtt_alarm && !gateway_data.mqtt_fault) {
        turn_off_led(&led_pattern, 14);
        gateway_data.disconnect_device = false;
        gateway_data.device_status = ALARM;
    }
}

static void led_task(void *pvParameters) {
    pins_init();
    led_pattern = 0x0000; // Reset tất cả LED mỗi vòng lặp
    while (1) {

        // Xử lý trạng thái WiFi
        switch (gateway_data.mode) {
        case AP:
            turn_on_led(&led_pattern, 8);
            break;
        case STA:
            turn_off_led(&led_pattern, 8);
            if (gateway_data.ethernet) {
                handle_led_blink(6, &gw_mode_e, &gw_busy_time_e, GW_BUSY_BLINK_TIME_SHORT, GW_BUSY_BLINK_TIME_LONG);
            } else {
                turn_on_led(&led_pattern, 6);
            }

            if (gateway_data.default_wifi) {
                handle_led_blink(5, &gw_mode_d, &gw_busy_time_d, GW_BUSY_BLINK_TIME_SHORT, GW_BUSY_BLINK_TIME_LONG);
            } else {
                turn_on_led(&led_pattern, 5);
            }

            if (gateway_data.home_wifi) {
                handle_led_blink(4, &gw_mode_h, &gw_busy_time_h, GW_BUSY_BLINK_TIME_SHORT, GW_BUSY_BLINK_TIME_LONG);
            } else {
                turn_on_led(&led_pattern, 4);
            }
            break;
        default:
            //led_pattern = 0x0000;           
            break;
        }

        if( gateway_data.btn_silen ){
            turn_on_led(&led_pattern, 7);
            
        } else if(gateway_data.btn_silen == false ){
           turn_off_led(&led_pattern, 7);
        }
        if(gateway_data.mqtt_alarm || gateway_data.btn_test){
            if(!gateway_data.btn_silen){
                turn_off_led(&led_pattern, 13); // chỗ này mạch bị ngược 13
            } else{
                turn_on_led(&led_pattern, 13);// chỗ này mạch bị ngược 13
            }
        } else{
            turn_on_led(&led_pattern, 13); // tắt out24V  // chỗ này mạch bị ngược 13
            gateway_data.btn_silen = false;
        }


        if(gateway_data.network_connected == false){
            turn_on_led(&led_pattern, 9);
        } else{
            handle_led_blink(9, &gw_mode_m, &gw_busy_time_m, GW_BUSY_BLINK_TIME_SHORT, GW_BUSY_BLINK_TIME_LONG);
        }
        
        // Xử lý trạng thái FIRE
        check_fire_and_fault();

        power_gateway();

        //check btn test
        if(gateway_data.btn_test && !gateway_data.mqtt_alarm){
            turn_on_led(&led_pattern, 11);
            turn_on_led(&led_pattern, 0);
        } else if(!gateway_data.mqtt_alarm && !gateway_data.btn_test) {
            turn_off_led(&led_pattern, 0);
            turn_off_led(&led_pattern, 11);
        } else if(!gateway_data.btn_test){
            turn_off_led(&led_pattern, 11);
        }

        // Kiểm tra lỗi connect fire/fault
        check_fault_and_fire_connect();
        if(gateway_data.disconnect_device || fault_power || gateway_data.mqtt_fault){
            //gateway_data.mqtt_fault = true;
            turn_on_led(&led_pattern, 1); // bật LED Fault
        } else{
            turn_off_led(&led_pattern, 1); // tắt LED Fault
        }

        // Quét nút bấm
        if (esp_timer_get_time() - prev_key_scan > KEY_SCAN_TIME) {
            prev_key_scan = esp_timer_get_time();
            button_handler();
        }
        //led_pattern = 0x0000; // Reset tất cả LED mỗi vòng lặp
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
void led_start(){
   
    //xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
    xTaskCreatePinnedToCore(led_task, "led_task", 4096 , NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
}
