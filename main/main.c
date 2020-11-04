
#include <stdio.h>
#include <esp_log.h>
//delay test
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//Ramdon
#include <esp_system.h>   //checked, heap mac etc.                   components
//LED
#include <driver/gpio.h>
//chip info
#include <esp_spi_flash.h>
//working with C
#include <string.h>

#include <freertos/semphr.h>
#include <freertos/event_groups.h>
// Memory
#include <esp_heap_caps.h>
//NVS 单个数据和struct的存储。可以作为云数据backup的本地应急少量数据backup（类似最后100个数据）。暂时不一定需要SPIFFS.
#include <nvs.h>
#include <nvs_flash.h>

#define TAG "NVS"


// void grinding_Control_Task(void * Param){

//     while (true)
//     {
//         //modbus read grindingStatus;
//         //verify grindingStatus. bit[31] = 0 
//         //if (grindingStatus[31] | 0x8000 )
//         {
//             //If grindingControlNewCommands valid(bit[31] = 1)
//             //Modbus send grindingControlNewCommands
//         }
//         //else
//         {
//         }
//         vTaskDelay(pdMS_TO_TICKS(200));
//     }
// }

void app_main(void)
{
    printf("hello world!\n");
}