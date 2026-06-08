#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#define PINO_POTENCIOMETRO  4
#define CANAL_ADC           ADC_CHANNEL_3  

#define PINO_LED            42         
#define CANAL_PWM           LEDC_CHANNEL_0
#define MODO_PWM            LEDC_LOW_SPEED_MODE

#define PINO_BOTAO          41             

void app_main(void) {

    gpio_config_t config_botao = {
        .pin_bit_mask = (1ULL << PINO_BOTAO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, 
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&config_botao);

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t config_init_adc = {
        .unit_id = ADC_UNIT_1, 
    };
    
    // bitwidth
    adc_oneshot_new_unit(&config_init_adc, &adc1_handle);
    adc_oneshot_chan_cfg_t config_canal_adc = {
        .bitwidth = ADC_BITWIDTH_12, 
        .atten = ADC_ATTEN_DB_12     
    };
    adc_oneshot_config_channel(adc1_handle, CANAL_ADC, &config_canal_adc);

    // timer PWM
    ledc_timer_config_t config_timer_pwm = {
        .speed_mode       = MODO_PWM,
        .duty_resolution  = LEDC_TIMER_12_BIT, // reso 12 bits 
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,              // freq 5000
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&config_timer_pwm);

    // LED
    ledc_channel_config_t config_canal_pwm = {
        .speed_mode     = MODO_PWM,
        .channel        = CANAL_PWM,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PINO_LED,
        .duty           = 0, 
        .hpoint         = 0
    };
    ledc_channel_config(&config_canal_pwm);

    // sistem botao e terminal
    bool congelado = false;
    int ultimo_estado_botao = 0;
    int valor_adc = 0;
    uint32_t ultimo_tempo_print = 0;

    while (1) {
        int estado_botao = gpio_get_level(PINO_BOTAO);
        
        if (estado_botao == 1 && ultimo_estado_botao == 0) {
            congelado = !congelado;
            // debounce
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
        ultimo_estado_botao = estado_botao;
        
        if (!congelado) {
            adc_oneshot_read(adc1_handle, CANAL_ADC, &valor_adc);
            ledc_set_duty(MODO_PWM, CANAL_PWM, valor_adc);
            ledc_update_duty(MODO_PWM, CANAL_PWM);
        }
        
        uint32_t tempo_atual = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // print terminal
        if ((tempo_atual - ultimo_tempo_print) >= 500) {
            // formula convers 
            float tensao_mv = (valor_adc / 4095.0) * 3300.0;
            
            printf("Estado: %s | V. ADC: %04d | tensão: %.0f mV \n", 
                   congelado ? "HOLD" : "LIVE", 
                   valor_adc, 
                   tensao_mv);

            ultimo_tempo_print = tempo_atual;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}