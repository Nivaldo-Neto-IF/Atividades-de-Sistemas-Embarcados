#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"

#define LED_PIN    18   
#define BUTTON_PIN 4 

SemaphoreHandle_t btn_semaforo;

//função de interupção
static void IRAM_ATTR ISR_extern(void* arg) {
// semaforo
    xSemaphoreGiveFromISR(btn_semaforo, NULL);
}

void app_main(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE 
    };
    gpio_config(&btn_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, ISR_extern, NULL);

    // gera o semáforo 
    btn_semaforo = xSemaphoreCreateBinary();

    bool led_state = false;
    TickType_t tempo_espera = portMAX_DELAY; 

    while (1) {

        if (xSemaphoreTake(btn_semaforo, tempo_espera) == pdTRUE) {
            //debounce
            vTaskDelay(pdMS_TO_TICKS(50)); 
            
            if (gpio_get_level(BUTTON_PIN) == 0) { 
                
                // ler o valor da porta e colocar em uma var temporária
                uint32_t var_temporaria = REG_READ(GPIO_OUT_REG);
                
                // máscara para o bit 
                uint32_t mascara = (1 << LED_PIN);
                
                //operação bitwise entre a mascara e a var temporária
                var_temporaria = var_temporaria | mascara;
                
                // salvar o valor da var temporária 
                REG_WRITE(GPIO_OUT_REG, var_temporaria);
                
                led_state = true;
                
                TickType_t inicio_pressao = xTaskGetTickCount();
                bool desligamento_forcado = false;

                // monitora para ver se o botão continua precionado por 2 segundos
                while (gpio_get_level(BUTTON_PIN) == 0) {
                    if ((xTaskGetTickCount() - inicio_pressao) >= pdMS_TO_TICKS(2000)) {
                        
                        gpio_set_level(LED_PIN, 0);
                        led_state = false;
                        tempo_espera = portMAX_DELAY; 
                        desligamento_forcado = true;
                        
                        while(gpio_get_level(BUTTON_PIN) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(20));
                        }
                        break; 
                    }
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                
                if (!desligamento_forcado) {
                    tempo_espera = pdMS_TO_TICKS(10000); 
                }
            }
        } 
        else {
            gpio_set_level(LED_PIN, 0);
            led_state = false;
            tempo_espera = portMAX_DELAY; 
        }
    }
}