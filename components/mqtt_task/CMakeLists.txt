idf_component_register(SRCS "mqtt_task.c" "json_parser.c" "uuid.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_system esp_timer nvs_flash json main )