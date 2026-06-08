#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"

#define PINO_POTENCIOMETRO  4
#define CANAL_ADC           ADC_CHANNEL_3  

#define PINO_LED            42         
#define CANAL_PWM           LEDC_CHANNEL_0
#define MODO_PWM            LEDC_LOW_SPEED_MODE

#define PINO_BOTAO          41             

// pinos i2c
#define I2C_SDA             8   
#define I2C_SCL             9   
#define I2C_PORT            I2C_NUM_0
#define MPU6050_ADDR        0x68

// handles rtos
QueueHandle_t fila_pot_led;
SemaphoreHandle_t semaforo_botao;
SemaphoreHandle_t mutex_imu;

adc_oneshot_unit_handle_t adc1_handle;
bool modo_hold = false;

// struct imu
typedef struct {
    float x;
    float y;
    float z;
} imu_data_t;

imu_data_t dados_imu = {0, 0, 0};

// variaveis terminal
int adc_global_console = 0;
float tensao_global_console = 0;
int percentual_led_global = 0;

// config mpu6050
void mpu6050_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, 
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    // acorda sensor
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x6B, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
}

// task botao
void task_button(void *pvParameters) {
    int ultimo_estado = 1; 
    while (1) {
        int estado = gpio_get_level(PINO_BOTAO);
        
        // borda de descida 
        if (estado == 0 && ultimo_estado == 1) {
            xSemaphoreGive(semaforo_botao);
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
        ultimo_estado = estado;
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

// task pot
void task_potenciometro(void *pvParameters) {
    int valor_adc;
    while (1) {
        adc_oneshot_read(adc1_handle, CANAL_ADC, &valor_adc);
        
        // ajusta pra 13 bits
        int valor_pwm = valor_adc * 2; 

        // vars console 
        adc_global_console = valor_adc;
        tensao_global_console = (valor_adc / 4095.0) * 3300.0;

        // envia fila
        xQueueSend(fila_pot_led, &valor_pwm, portMAX_DELAY);
        
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

// task led
void task_led(void *pvParameters) {
    int valor_recebido_fila;
    
    while (1) {
        // checa semaforo
        if (xSemaphoreTake(semaforo_botao, pdMS_TO_TICKS(20)) == pdTRUE) {
            modo_hold = !modo_hold; 
            
            // limpa a fila velha 
            if (!modo_hold) {
                xQueueReset(fila_pot_led);
            }
        }

        // consome a fila se n em HOLD
        if (!modo_hold) {
            if (xQueueReceive(fila_pot_led, &valor_recebido_fila, 0) == pdTRUE) {
                ledc_set_duty(MODO_PWM, CANAL_PWM, valor_recebido_fila);
                ledc_update_duty(MODO_PWM, CANAL_PWM);
                
                // percentual led
                percentual_led_global = (valor_recebido_fila * 100) / 8191;
            }
        }
    }
}

// task imu
void task_imu(void *pvParameters) {
    uint8_t dados_brutos[6];
    while (1) {
        // leitura i2c
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, 0x3B, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, dados_brutos, 5, I2C_MASTER_ACK);
        i2c_master_read_byte(cmd, dados_brutos + 5, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        
        if (i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100)) == ESP_OK) {
            int16_t accel_x = (dados_brutos[0] << 8) | dados_brutos[1];
            int16_t accel_y = (dados_brutos[2] << 8) | dados_brutos[3];
            int16_t accel_z = (dados_brutos[4] << 8) | dados_brutos[5];

            // trava 
            xSemaphoreTake(mutex_imu, portMAX_DELAY);
            dados_imu.x = accel_x / 16384.0;
            dados_imu.y = accel_y / 16384.0;
            dados_imu.z = accel_z / 16384.0;
            // solta 
            xSemaphoreGive(mutex_imu);
        }
        i2c_cmd_link_delete(cmd);

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

// task console
void task_console(void *pvParameters) {
    float x_local, y_local, z_local;
    
    while (1) {
        // trava mutex leitura
        xSemaphoreTake(mutex_imu, portMAX_DELAY);
        x_local = dados_imu.x;
        y_local = dados_imu.y;
        z_local = dados_imu.z;
        xSemaphoreGive(mutex_imu);

        // print terminal 
        printf("STATUS: [%s] | POT: %04d (%.0f mV) | LED: %d%%\n", 
               modo_hold ? "HOLD" : "LIVE", 
               adc_global_console, 
               tensao_global_console, 
               percentual_led_global);
        printf("IMU ACCEL (g): X: %.2f | Y: %.2f | Z: %.2f\n", 
               x_local, 
               y_local, 
               z_local);
        printf("========================================================\n");

        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

void app_main(void) {
    // config botao (PULL-UP INTERNO LIGADO!)
    gpio_config_t config_botao = {
        .pin_bit_mask = (1ULL << PINO_BOTAO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       // Habilita o resistor interno de 45k
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  
    };
    gpio_config(&config_botao);

    // config adc
    adc_oneshot_unit_init_cfg_t config_init_adc = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&config_init_adc, &adc1_handle);
    adc_oneshot_chan_cfg_t config_canal_adc = {
        .bitwidth = ADC_BITWIDTH_12, 
        .atten = ADC_ATTEN_DB_12     
    };
    adc_oneshot_config_channel(adc1_handle, CANAL_ADC, &config_canal_adc);

    // config pwm
    ledc_timer_config_t config_timer_pwm = {
        .speed_mode       = MODO_PWM,
        .duty_resolution  = LEDC_TIMER_13_BIT, 
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,              
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&config_timer_pwm);

    // canal pwm led
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

    // init i2c
    mpu6050_init();

    // cria rtos
    fila_pot_led = xQueueCreate(10, sizeof(int));
    semaforo_botao = xSemaphoreCreateBinary();
    mutex_imu = xSemaphoreCreateMutex();

    // cria tasks
    xTaskCreate(task_button, "task_button", 2048, NULL, 5, NULL); 
    xTaskCreate(task_imu, "task_imu", 4096, NULL, 4, NULL);       
    xTaskCreate(task_led, "task_led", 2048, NULL, 3, NULL);       
    xTaskCreate(task_potenciometro, "task_pot", 2048, NULL, 3, NULL); 
    xTaskCreate(task_console, "task_console", 2048, NULL, 1, NULL);   
}