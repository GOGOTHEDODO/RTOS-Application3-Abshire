/* --------------------------------------------------------------
   Application: 03 - Rev2
   Release Type: Baseline Preemption
   Class: Real Time Systems - Fa 2025
   Author: [Henry Abshire]
   Email: [he248516@ucf.edu]
   Company: [University of Central Florida]
   Website: N/A
   AI Use: Commented inline -- None
---------------------------------------------------------------*/

/*----------------------------------------------------------------
Senario: Earthquake Early Warning System (EEWS)

-----------------------------------------------------------------*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "math.h"
#include <string.h>
#include "freertos/semphr.h"
#include "esp_log.h"

#define LED_PIN GPIO_NUM_2 // Using GPIO2 for the LED
#define BUTTON_PIN GPIO_NUM_4
#define LDR_PIN GPIO_NUM_32
#define LDR_ADC_CHANNEL ADC1_CHANNEL_4

#define BUFFER_SIZE 50

int SENSOR_THRESHOLD = 500;
double GAMA = 0.7;
int GLOBALBUFFER[BUFFER_SIZE];

#define CONFIG_LOG_DEFAULT_LEVEL_VERBOSE (1)
#define CONFIG_LOG_DEFAULT_LEVEL (5)
#define CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE (1)
#define CONFIG_LOG_MAXIMUM_LEVEL (5)
#define CONFIG_LOG_COLORS (1)
#define CONFIG_LOG_TIMESTAMP_SOURCE_RTOS (1)
char* TAG ="DEBUG:";
// Synchronization
static SemaphoreHandle_t xLogMutex;  // Hand off access to the buffer!
static SemaphoreHandle_t xButtonSem; // Hand off on button press!

// Consider supressing the output
void systemActiveIndicatorTask(void *pvParameters)
{
    bool led_status = false;
    TickType_t currentTime = pdTICKS_TO_MS(xTaskGetTickCount());

    while (1)
    {
        currentTime = pdTICKS_TO_MS(xTaskGetTickCount());
        gpio_set_level(LED_PIN, 1);
        led_status = true;          
        ESP_LOGI(TAG,"System Alive and monitoring: %s @ %lu\n", led_status ? "ON" : "OFF", currentTime);
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 500 ms using MS to Ticks Function vs alternative which is MS / ticks per ms
        gpio_set_level(LED_PIN, 0);     // set low
        led_status = false;             // toggle
        ESP_LOGI(TAG,"System Alive and monitoring: %s @ %lu\n", led_status ? "ON" : "OFF", currentTime);
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 500 ms
    }
    vTaskDelete(NULL); // We'll never get here; tasks run forever
}


void printSeismicReadings(void *pvParameters)
{
    TickType_t currentTime = pdTICKS_TO_MS(xTaskGetTickCount());
    TickType_t previousTime = 0;
    int count = 0;
    while (1)
    {
        previousTime = currentTime;
        currentTime = pdTICKS_TO_MS(xTaskGetTickCount());
        // Prints a periodic message based on a thematic area. Output a timestamp (ms) and period (ms
        ESP_LOGI(TAG,"Seismic monitor update: system stable. No major erros @ time %lu [period = %lu]!\n", currentTime, currentTime - previousTime);
        count++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1000 ms
    }
    vTaskDelete(NULL); // We'll never get here; tasks run forever
}

// TODO11: Create new task for sensor reading every 500ms
void sensor_task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount(); // Initialize last wake time

    // Variables to compute LUX
    int raw;
    float Vmeasure = 0.;
    float Rmeasure = 0.;
    float lux = 0.;
    // Variables for moving average
    int idx = 0;
    for (int i = 0; i < BUFFER_SIZE; ++i)
    {
        GLOBALBUFFER[i] = 0; // Initialize all readings to zero
    }

    const TickType_t periodTicks = pdMS_TO_TICKS(200); // e.g. 200 ms period
    
    TickType_t prevTime = 0;
    while (1)
    {
        // Read current sensor value
        raw = adc1_get_raw(LDR_ADC_CHANNEL);

        // Compute LUX
        Vmeasure = ((float)raw / 4096.0) * 3.3;
        Rmeasure = 10000 * Vmeasure / (3.3 - Vmeasure);
        lux = (pow(50.0 * 1000.0 * pow(10.0, GAMA) / Rmeasure, (1.0 / GAMA)));

        // Update logging buffer
        if (xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(100))) {
           GLOBALBUFFER[idx] = lux; // place new reading
           idx = (idx + 1) % BUFFER_SIZE;
           xSemaphoreGive(xLogMutex); // give up the semaphore we are done with access the buffer
           ESP_LOGI(TAG, "New LUX reading: %f", lux);
        }
        //delay if we cant get the mutex or if we can
        prevTime = lastWakeTime;
        vTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

void logger_task(void *pvParameters)
{
    int internalBuffer[BUFFER_SIZE];
    
    for(int i=0; i<BUFFER_SIZE;i++){
        internalBuffer[i] = 0;
    }

    while(1){
        ESP_LOGI(TAG, "Now waiting for button press...\n");
        if (xSemaphoreTake(xButtonSem, portMAX_DELAY) == pdTRUE){
        ESP_LOGI(TAG, "Button Pressed, logging data...");
        //read from buffer, 
        if(xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(100))){ //try to take the mutex
            //copy data internally:
            for(int i=0; i<BUFFER_SIZE; i++){
               internalBuffer[i] = GLOBALBUFFER[i];
            }
            xSemaphoreGive(xLogMutex); //give up the mutex
        }

        //process data
        int32_t min = INT32_MAX; 
        int32_t max = INT32_MIN;
        double avg = 0;
        double stddev = 0.0;
        for(int i=0; i<BUFFER_SIZE; i++){
            //update min
            if(internalBuffer[i]<min){
                min = internalBuffer[i]; 
            }
            //update max
            if(internalBuffer[i]>max){
                max = internalBuffer[i];
            }
            //update avg
            avg += internalBuffer[i];
        }

        //calc stdev
        avg = ((double) avg)/ BUFFER_SIZE;
        double sqrDiffSum = 0.0;
        for(int i=0; i<BUFFER_SIZE; ++i){
            double diff = internalBuffer[i] - avg;
            sqrDiffSum += diff * diff;
        }
        double variance = sqrDiffSum/ BUFFER_SIZE;
        stddev = sqrt(variance); 

        ESP_LOGI(TAG, "Logging Data Complete! \n Min: %ld \n Max: %ld \n Avg: %f \n StdDev: %f", min, max, avg, stddev);
        }
    }//end of forever loop
}//end of logger task
    

//this is a basic interupt handler, returns void has IRAM_ATTR listed, and takes in void *args
void IRAM_ATTR button_isr_handler(void *arg)
{
    //this is used rather than printf becuase we can't use print f in a interupt function
    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonSem, &xHigherPrioTaskWoken);
    portYIELD_FROM_ISR(xHigherPrioTaskWoken);
}

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);        // enable info globally
    esp_log_level_set(TAG, ESP_LOG_INFO); // enable for your specific tag
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Initialize LED GPIO
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LDR_PIN);
    gpio_set_direction(LDR_PIN, GPIO_MODE_INPUT);
    //button io
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_PIN); // Enable pull-up resistor for the button pin
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE); // Trigger interrupt on falling edge (button press)
    //adc init
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(LDR_ADC_CHANNEL, ADC_ATTEN_DB_11);

    //interupt code:
    gpio_install_isr_service(0); //no flags
    gpio_isr_handler_add(GPIO_NUM_4, button_isr_handler, NULL); //binds the interupt function to the pin

    //semphore:
    xButtonSem = xSemaphoreCreateBinary();
    //mutex
    xLogMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(systemActiveIndicatorTask, "LEDTask", 2048, NULL, 1, NULL, 1); // blinking the led is the lowest priority task as it is nice to know
                                                                                           //  but the pint task will give the same system running info,
    xTaskCreatePinnedToCore(printSeismicReadings, "PrintTask", 4096, NULL, 1, NULL, 1); // print is second lowest as it provides important info,
                                                                                        // but is not system critical
    xTaskCreatePinnedToCore(sensor_task, "SensorTask", 8192, NULL, 2, NULL, 1); // the sensor task is the highest priority as it is critical to system operation
    
    xTaskCreatePinnedToCore(logger_task, "Logger", 4096, NULL, 3, NULL,1);
}