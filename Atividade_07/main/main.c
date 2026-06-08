#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define LED_PIN 2 
#define TX2_PIN 4 
#define RX2_PIN 5  
#define UART_NUM  UART_NUM_2

//Tx
void tx_task(void *arg) {
    bool estado = true;
    while (1) {
        const char *mensagem = estado ? "LIGAR\n" : "DESLIGAR\n";
        uart_write_bytes(UART_NUM, mensagem, strlen(mensagem));

        if(estado) printf("LIGAR\n");
        else printf("DESLIGAR\n");
        estado = !estado;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//Rx
void rx_task(void *arg) {
    uint8_t *buffer = (uint8_t *)malloc(128 * sizeof(uint8_t));
    if (buffer == NULL) {
        vTaskDelete(NULL);
    }
    int pos = 0;
    while (1) {
        // config  UART
        int tamanho = uart_read_bytes(UART_NUM, &buffer[pos], 1, pdMS_TO_TICKS(100));

        if (tamanho > 0) {
            if (buffer[pos] == '\n') {
                buffer[pos] = '\0';
                if (strcmp((char *)buffer, "DESLIGAR") == 0) {
                    gpio_set_level(LED_PIN, 0);
                      }
                else if (strcmp((char *)buffer, "LIGAR") == 0) {
                    gpio_set_level(LED_PIN, 1);
                }
                 pos = 0;
            }
             else {
              pos++;
              if (pos >= 127) {
              pos = 0;
                }
            }
         }
     }
     free(buffer);
}

void app_main(void) {
      //LED
     gpio_config_t led_conf = {
         .pin_bit_mask = (1ULL << LED_PIN),
         .mode = GPIO_MODE_OUTPUT,
         .pull_up_en = GPIO_PULLUP_DISABLE,
         .pull_down_en = GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE
     };
     gpio_config(&led_conf);
     gpio_set_level(LED_PIN, 0);

      //UART2
     uart_config_t uart_config = {
         .baud_rate = 115200,
         .data_bits = UART_DATA_8_BITS,
         .parity = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
         .source_clk = UART_SCLK_DEFAULT,
     };
     uart_param_config(UART_NUM, &uart_config);
     uart_set_pin(UART_NUM, TX2_PIN, RX2_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
     uart_driver_install(UART_NUM, 1024 * 2, 1024 * 2, 0, NULL, 0);
     uart_set_loop_back(UART_NUM, false);
     gpio_pullup_en(RX2_PIN);
     xTaskCreate(tx_task, "tx", 3072, NULL, 10, NULL);
     xTaskCreate(rx_task, "rx", 3072, NULL, 10, NULL);
}