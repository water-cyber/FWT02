#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include "common_interface.h"
#include "uuid.h"
#include "mqtt_task.h"




#define DEVICE_TYPE "FWT-02"
#define CONNECT_AVAILABLE "ethernet, wifi, 4G"
void json_parser_gw_test( char *message_packet);
void json_packet_message_gateway_data(char *message_packet, char *message_type, const char *device_status);
void json_packet_message_update_subdevice(char *message_packet);
void handle_command(const char *cmd);
bool json_parser_gw_cmd(const char *message, uint16_t length, const char *imei);
void json_parser_gw_cmd_server(char *message_packet);
void json_packet_message_gateway_event(char *message_packet, char *message_type, const char *device_status);
void json_packet_message_reset_factory(char *message_packet);
#endif /* MAIN_JSON_PARSER_JSON_PARSER_H_ */