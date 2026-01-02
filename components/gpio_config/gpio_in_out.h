#ifndef GPIO_IN_OUT_H
#define GPIO_IN_OUT_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "common_interface.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "nvs_flash.h"
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Chân kết nối
#define PIN_DS   GPIO_NUM_14   // Chân dữ liệu
#define PIN_SHCP GPIO_NUM_2   // Chân xung đồng hồ
#define PIN_STCP GPIO_NUM_27   // Chân chốt dữ liệu
#define PIN_RESET GPIO_NUM_36  // Chân xoá nvs
#define PIN_SILEN GPIO_NUM_26  // Chân Silen
#define PIN_TEST GPIO_NUM_32  // Chân test
#define PIN_ADAPTER GPIO_NUM_33  // Chân AC
#define PIN_FIRE GPIO_NUM_34  // Chân alarm
#define PIN_FAULT GPIO_NUM_12  // Chân fault
#define PIN_BAT GPIO_NUM_25  // Chân DC
#define PIN_FAULT_CONNECT GPIO_NUM_22
#define PIN_FIRE_CONNECT GPIO_NUM_39
#define PIN_UPDATE_DEVICE GPIO_NUM_15
#define PIN_RESET_FACTORY GPIO_NUM_35

#define ESP_INTR_FLAG_DEFAULT 0  // Cờ mặc định
#define GPIO_OUT_PIN ((1ULL << PIN_DS) | (1ULL << PIN_SHCP) | (1ULL << PIN_STCP)) 
#define GPIO_IN_PIN ((1ULL << PIN_RESET) | (1ULL << PIN_RESET_FACTORY)  | (1ULL << PIN_ADAPTER) | (1ULL << PIN_FIRE) | (1ULL << PIN_FAULT) | (1ULL << PIN_BAT) | (1ULL << PIN_SILEN) | (1ULL << PIN_TEST) | (1ULL << PIN_FAULT_CONNECT) | (1ULL << PIN_FIRE_CONNECT) | (1ULL) << PIN_UPDATE_DEVICE) 

 #define WARING_TEMP 1900

#define R_FIXED    100000.0
#define BETA       3950.0
#define R_NTC25    100000.0
#define T0         298.15


void led_start();

#endif