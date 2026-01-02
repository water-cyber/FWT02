idf_component_register(SRCS "ethernet_w5500.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_eth driver esp_netif fatfs main)
