#ifndef ETHERNET_W5500_H
#define ETHERNET_W5500_H
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_eth.h" 
void ethernet_w5500_init(void);
void sd_card_init(void);
// void write_to_sd_card(const char *filename, const char *data);
// void mount_sd_card(void);
// void log_time_to_file();
void write_data_to_sdcard(const char *type);
void delete_old_logs();
void obtain_time();
extern esp_netif_t *eth_netif;
extern esp_eth_handle_t eth_handle;
//extern bool eth_running;
//extern bool got_ip_eth;
#endif // ETHERNET_W5500_H
