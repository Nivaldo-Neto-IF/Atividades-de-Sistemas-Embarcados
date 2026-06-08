#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Definições de Hardware
#define LED_PIN    1   // GPIO 1 (Alimentação do LED)
#define BUTTON_PIN 39  // GPIO 39 (Entrada com Pull-up externo)

static bool led_state = false;
esp_timer_handle_t safety_timer;

// Callback: Desliga o LED após o tempo esgotado
void timer_callback(void* arg) {
    led_state = false;
    gpio_set_level(LED_PIN, 0);
}

void app_main(void) {
    // Configuração do GPIO 1 como Saída
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    // Configuração do GPIO 39 como Entrada
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    // Criação do timer de 10 segundos
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "timer_led"
    };
    esp_timer_create(&timer_args, &safety_timer);

    int last_btn_state = 1; // Pull-up externo mantém em High (1)

    while (1) {
        int current_btn_state = gpio_get_level(BUTTON_PIN);

        // Lógica de Toggle no pressionamento (Borda de descida: 1 -> 0)
        if (last_btn_state == 1 && current_btn_state == 0) {
            led_state = !led_state;
            gpio_set_level(LED_PIN, led_state);

            if (led_state) {
                // Se ligou, inicia/reinicia o timer de 10s
                esp_timer_stop(safety_timer);
                esp_timer_start_once(safety_timer, 10000000); // 10s em microsegundos
            } else {
                // Se desligou manualmente, para o timer
                esp_timer_stop(safety_timer);
            }
            
            // Debounce para evitar leituras falsas
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        last_btn_state = current_btn_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}