#include "ethernet_w5500.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"

#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <time.h>

#include "esp_system.h"
#include "esp_sntp.h"
#include "common_interface.h"


static const char *TAG = "ETH_SD_W5500";
#define MAX_FILE_AGE 30 * 86400  // 30 ngày (số giây)
#define MOUNT_POINT  "/sdcard"   // Thư mục mount thẻ nhớ
#define PIN_MISO    19
#define PIN_MOSI    23
#define PIN_SCLK    18
#define PIN_CS_ETH  21  // CS của W5500
#define PIN_CS_SD   13  // CS của microSD

esp_eth_handle_t eth_handle = NULL;
esp_netif_t *eth_netif = NULL;
spi_device_handle_t eth_spi_handle = NULL;
spi_device_handle_t sd_spi_handle = NULL;
spi_device_interface_config_t eth_devcfg;
static SemaphoreHandle_t spi_mutex; // Semaphore để bảo vệ SPI

// Hàm khóa SPI
void spi_lock() {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
}

// Hàm mở khóa SPI
void spi_unlock() {
    xSemaphoreGive(spi_mutex);
}

// Cấu hình bus SPI
void configure_spi(void) {
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

     // Cấu hình SPI cho W5500
     eth_devcfg = (spi_device_interface_config_t){
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 4 * 1000 * 1000,
        .spics_io_num = PIN_CS_ETH,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &eth_devcfg, &eth_spi_handle));

    // Cấu hình SPI cho microSD
    spi_device_interface_config_t sd_devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .mode = 0,  // Đảm bảo cùng mode với W5500
        .clock_speed_hz = 4 * 1000 * 1000, // microSD có thể chạy nhanh hơn
        .spics_io_num = PIN_CS_SD,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &sd_devcfg, &sd_spi_handle));
    spi_mutex = xSemaphoreCreateMutex();
}

// Cấu hình Ethernet W5500
esp_eth_handle_t configure_ethernet(void) {
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.sw_reset_timeout_ms = 10;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = -1;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &eth_devcfg);
    w5500_config.int_gpio_num = -1;
    w5500_config.poll_period_ms = 10;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;

    spi_lock();  // 🔒 Khóa SPI trước khi cấu hình W5500
    //ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
        esp_err_t err = esp_eth_driver_install(&config, &eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install Ethernet driver: %s", esp_err_to_name(err));
        // Tùy xử lý: retry, báo lỗi, hoặc tiếp tục chạy các phần khác
        return NULL;
    }
    spi_unlock();  // 🔓 Mở khóa SPI sau khi cấu hình xong

    // Thiết lập địa chỉ MAC
    spi_lock();
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, (uint8_t[]){
        0x02, 0xAB, 0xCD, 0xEF, 0x12, 0x34
    }));
    spi_unlock();

    return eth_handle;
}

// Khởi tạo SD Card
void sd_card_init(void) {
    ESP_LOGI(TAG, "Initializing SD card...");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    spi_lock();  // 🔒 Khóa SPI trước khi thao tác với SD card

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 1000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_CS_SD;
    slot_config.host_id = SPI2_HOST; 

    sdmmc_card_t *card;
    //esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &card);
    esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
    }

    spi_unlock();  // 🔓 Mở khóa SPI sau khi thao tác xong
}


// Khởi tạo Ethernet và microSD
void ethernet_w5500_init(void) {
    ESP_LOGI(TAG, "Initializing W5500 Ethernet...");

    esp_netif_config_t eth_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_cfg);

    configure_spi();
    eth_handle = configure_ethernet();

    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    if (!eth_netif) {
        ESP_LOGW(TAG, "Ethernet interface not found!");
        return;
    }
}

// void obtain_time() {
//     ESP_LOGI(TAG, "Start time synchronization from SNTP...");

//     esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
//     esp_sntp_setservername(0, "pool.ntp.org");
//     esp_sntp_init();

//     time_t now;
//     struct tm timeinfo;
//     int retry = 0;
//     const int max_retries = 10;

//     do {
//         vTaskDelay(2000 / portTICK_PERIOD_MS);
//         time(&now);
//         setenv("TZ", "ICT-7", 1);
//         tzset();
//         localtime_r(&now, &timeinfo);
//         retry++;
//     } while (timeinfo.tm_year < (2020 - 1900) && retry < max_retries);

//     if (retry < max_retries) {
//         ESP_LOGI(TAG, "Time NOW: %s", asctime(&timeinfo));
//     } else {
//         ESP_LOGE(TAG, "Time sync error after %d attempts!", retry);
//     }
// }

static void time_sync_callback(struct timeval *tv)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "SNTP time synced: %s", asctime(&timeinfo));
}

void obtain_time(void)
{
    ESP_LOGI(TAG, "Start time synchronization from SNTP...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    //esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(0, "216.239.35.0");
    
    // Sync nhanh lúc khởi động, sau đó 30 phút/lần
    esp_sntp_set_sync_interval(30 * 60 * 1000); 

    esp_sntp_set_time_sync_notification_cb(time_sync_callback);

    esp_sntp_init();

    esp_sntp_restart();   // ép sync ngay
    // Thiết lập timezone
    setenv("TZ", "ICT-7", 1);
    tzset();
}


void write_data_to_sdcard(const char *type) {
    char filename[64];
    // get_filename_for_today(filename, sizeof(filename));
    char log_entry[128];  // Độ dài đủ để chứa toàn bộ dữ liệu
    time_t now;
    struct tm timeinfo;
    char timestamp[32];

    time(&now);
    localtime_r(&now, &timeinfo);
    // snprintf(filename, sizeof(filename), "/sdcard/%02d_%02d_%02d.txt",
    // (timeinfo.tm_year + 1900) % 100, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    snprintf(filename, sizeof(filename), "%s/%02d_%02d_%02d.txt",
         MOUNT_POINT, (timeinfo.tm_year + 1900) % 100, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    // strcpy(filename,"/sdcard/03_22.txt");
    // snprintf(filename, sizeof(filename), "/sdcard/%02d_%02d.txt",
    //      timeinfo.tm_mon + 1, timeinfo.tm_mday);
    // strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
    // ESP_LOGI(TAG, "📂 FILE NAME: %s", filename);
    
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "FILE NAME: %s\n",filename);
    char *device_state[] = {"normal", "ac", "dc"};
    //char *device_status[] = {"normal", "alarm", "fault", "disconnect"};
    char *gw_connect_info[] = {"wifi", "4G", "ethernet"};
    snprintf(log_entry, sizeof(log_entry), "[%s] State: %s, Status: %s, Connect: %s, Avail: %s",
    timestamp,
    device_state[gateway_data.device_state],
    //device_status[gateway_data.device_status],
    type, 
    gw_connect_info[gateway_data.connect_info],
    gateway_data.connect_avail);

    ESP_LOGI(TAG, "Logging: %s", log_entry);

    // Kiểm tra thẻ nhớ đã mount chưa
    struct stat st;
    if (stat(MOUNT_POINT, &st) != 0) {
        ESP_LOGE(TAG, "Memory card not mounted!");
        return;
    }
    spi_lock();  // 🔒 Khóa SPI trước khi ghi dữ liệu vào thẻ nhớ
    delete_old_logs();
    FILE *f = fopen(filename, "a");
    if (!f) {
        ESP_LOGW(TAG, "File does not exist, create new...");
        f = fopen(filename, "w");
    }

    if (f) {
        fprintf(f, " %s\n", log_entry);
        ESP_LOGI(TAG, "Save data successfully");
        fclose(f);
    } else{
        ESP_LOGE(TAG, "Failed to open file: %s, errno: %d (%s)", filename, errno, strerror(errno));
    }

    spi_unlock();  // 🔓 Mở khóa SPI sau khi ghi xong
}
void delete_old_logs(){
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open memory card folder!");
        return;
    }
    time_t now = time(NULL);
    struct dirent *entry;
    int deleted_files = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!entry) {
            ESP_LOGE(TAG, "Error reading directory!");
            break;
        }
        if (!strstr(entry->d_name, ".TXT")) continue;
        // ESP_LOGI(TAG, "Checking file: %s", entry->d_name);
        struct tm file_date = {0};
        if (sscanf(entry->d_name, "%2d_%2d_%2d.TXT", &file_date.tm_year, &file_date.tm_mon, &file_date.tm_mday) != 3) {
            printf("%02d_%02d_%02d\n", file_date.tm_year, file_date.tm_mon, file_date.tm_mday);
            continue;  // Bỏ qua file không đúng định dạng
        }
        
        // ESP_LOGI(TAG, "Checking file x2: %s", entry->d_name);
        // Chuyển đổi năm từ YY -> YYYY
        file_date.tm_year = (file_date.tm_year >= 70) ? (file_date.tm_year) : (file_date.tm_year + 100);
        file_date.tm_mon -= 1; // Tháng bắt đầu từ 0
        time_t file_time = mktime(&file_date);
        // ESP_LOGI(TAG, "File time (timestamp): %lld", file_time);
        // // Chuyển timestamp về dạng ngày giờ đọc được
        // struct tm *check_time = localtime(&file_time);
        // ESP_LOGI(TAG, "File date: %04d-%02d-%02d %02d:%02d:%02d",
        //         check_time->tm_year + 1900, check_time->tm_mon + 1, check_time->tm_mday,
        //         check_time->tm_hour, check_time->tm_min, check_time->tm_sec);
        if (difftime(now, file_time) > MAX_FILE_AGE) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), MOUNT_POINT "/%s", entry->d_name);
            if (remove(filepath) == 0) {
                ESP_LOGI(TAG, "Deleted old file: %s", entry->d_name);
                deleted_files++;
            } else {
                ESP_LOGE(TAG, "Error when deleting file: %s", entry->d_name);
            }
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Total number of deleted files: %d", deleted_files);
}
// void log_time_to_file() {
//     char filename[64];
//     // get_filename_for_today(filename, sizeof(filename));
    
//     time_t now;
//     struct tm timeinfo;
//     char timestamp[32];

//     time(&now);
//     localtime_r(&now, &timeinfo);
//     snprintf(filename, sizeof(filename), "/sdcard/log%04d%02d%02d.txt",
//         timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        
//     strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
//     ESP_LOGI(TAG, "FILE NAME:  %s\n", filename);
    
//     // Kiểm tra thẻ nhớ đã mount chưa
//     struct stat st;
//     if (stat("/sdcard", &st) != 0) {
//         ESP_LOGE(TAG, "Thẻ nhớ chưa được mount!");
//         return;
//     }

//     // Ghi thời gian vào file
//     //write_data_to_sdcard(filename, timestamp);
    
// }
