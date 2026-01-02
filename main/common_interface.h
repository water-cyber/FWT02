#ifndef COMMON_INTERFACE_H
#define COMMON_INTERFACE_H

typedef enum {
    WIFI_HOME,
    WIFI_DEFAULT,
    ETHERNET,
} network_state_t;

typedef enum {
    AP,
    STA
} mode_wifi_t;

typedef enum{
	STATUS_NORMAL,
	ALARM,
	FAULT,
	DISCONNECT
} gateway_status_t;

typedef enum{
	STATE_NORMAL,
	AC,
	DC
} gateway_state_t;

typedef struct {
    char topic[128];
    char payload[512];
    int qos;
    int retain;
    bool pub_success;
    int msg_id;  // ID trả về từ enqueue để xác định khi gửi xong
} mqtt_msg_t;

typedef struct {
    //bool ota_pending;
    char firmware_url[512];
    char firmware_hash[65]; // sha256 hex + null
    char version[32];
} ota_job_t;

typedef struct
{
	bool cmd_server;
	bool update_sub_device;
	bool btn_silen;
	bool btn_test;
	bool mqtt_status;
	bool mqtt_alarm;
	bool mqtt_fault;
	bool home_wifi;
	bool default_wifi;
    bool ethernet;
	bool internet_status;
	bool event_enable;
	bool event_network;
	bool network_connected;
	bool disconnect_device;
	bool reset_4G;
	bool factory;
	bool ota_running;
	bool skip_wifi;
	gateway_state_t device_state;
	gateway_status_t device_status;
	network_state_t connect_info;
	mode_wifi_t mode;
	char connect_avail[20];
	char current_hash[65];
} gateway_data_t;

extern char imei[20];

// Khai báo biến toàn cục cho trạng thái mạng và chế độ Wi-Fi
//extern network_state_t currentNetworkState;
//extern mode_wifi_t currentWiFiMode;
extern gateway_data_t gateway_data;
extern ota_job_t ota_job;
#endif // COMMON_INTERFACE_H