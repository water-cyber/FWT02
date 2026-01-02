idf_component_register(SRCS "gpio_in_out.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer nvs_flash esp_wifi main)