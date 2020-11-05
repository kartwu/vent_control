
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

#define TAG_TEMPR "TEMPR"
#define TAG_HMI "HMI"

typedef struct Vent_machine_Struct {
  char Name[20];
  bool Fan_Power_Control;               //风扇开关控制
  bool Fan_Power_Status;                //风扇状态，用于程序逻辑判断
  bool Heat_Exchanger_Status;           //热交换器的状态，开机为热交换器水循环泵在工作。从外界获取状态前，计算空气流经前后温差初判
  float In_Air_Temperature;             //从室外吸进来的空气，经过管路加温后，到达软交换器之前的温度
  float Out_Air_Temperature;            //热交换后空气的温度
  uint32_t Re_PowerOn_Delay;            //开机延时，尤其是在低温保护后重启是需要延时，避免频繁关机和重启
} Vent_machine;

void temperatureMonitoring_control(void * params){
    Vent_machine * myMachine = (Vent_machine *) params;
    for(;;)
    {
      ESP_LOGI(TAG_TEMPR,"ventMachine name from HMI is %s\n",myMachine->Name );
      strcpy(myMachine->Name, "wqjian");
      ESP_LOGI(TAG_TEMPR, "In_Air_Temperature is %f\n", myMachine->In_Air_Temperature);
      myMachine->In_Air_Temperature = 20.1 ;

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
void HMI(void *params)
{
  
  Vent_machine * myMachine = (Vent_machine *) params;   
  while(true){
  ESP_LOGW(TAG_HMI,"ventMachine name from XXXX is %s\n",myMachine->Name );
  strcpy(myMachine->Name, "QBDZ-001");
  ESP_LOGW(TAG_HMI,"In_Air_Temperature is %f\n", myMachine->In_Air_Temperature);
  myMachine->In_Air_Temperature = 10.5 ;
  vTaskDelay(pdMS_TO_TICKS(3000));
 
  }
}
void app_main(void)
{
    Vent_machine  ventMachine;
    strcpy(ventMachine.Name, "QBDZ-001");
    ventMachine.Fan_Power_Control = 1;
    ventMachine.In_Air_Temperature =25;
    xTaskCreate(&temperatureMonitoring_control,"Temp_Meas",2048, &ventMachine,1,NULL);
    vTaskDelay(pdMS_TO_TICKS(200));
    xTaskCreate(&HMI,"HMI", 2048, &ventMachine, 1 , NULL );
  }