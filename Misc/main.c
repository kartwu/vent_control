#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "connect.h"
#include "mqtt_client.h"
#include "sign_api.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "math.h"
#include "esp_adc_cal.h"

#define TAG "MQTT"
#define LED_PIN 22                // 用LED闪烁快慢指示是否风扇开关状态，快闪为关，延长一倍时间慢闪为开（默认正常）
static esp_adc_cal_characteristics_t *adc_chars;

//阿里云三元组,一机一密
#define PRODUCT_KEY "a1m81zhi11W"
#define PRODUCT_SECRET "MrY7mFRZCDXHdDld"
#define DEVICE_SECRET   "32kSDyw5lXLbpFsIgHai8NgSCl7RwwGV"
#define DEVICE_NAME "THSD_Data"

//xQueueHandle readingQueue;
xSemaphoreHandle readingSemaphore;
TaskHandle_t taskHandle;

const uint32_t WIFI_CONNEECTED = BIT1;
const uint32_t MQTT_CONNECTED = BIT2;
const uint32_t MQTT_PUBLISHED = BIT3;   //收到云端回复成功后:TRUE
const uint32_t MQTT_PUB_CHECKED = BIT4; //收到云端回复，判断code==200？并设置isPubSuccessful后，启动关闭 MQTT 和 WIFI。

//未来组成 VentFan object的参数组合
const uint32_t Fan_Power_Pin = 26;                  //Pin 35 对外控制继电器
bool Fan_Power_Status = false;
float Tin = 25;
float Tout = 25; 
int Restart_countdown = 0;
bool isPubSuccessful = 0;
// 采样管脚 ESP32 ADC1（IO32~39）:原来使用26不能和Wifi 同时使用，因为ADC2（GPIOs 0, 2, 4, 12 - 15 and 25 - 27）已经被Wifi 驱动使用

void mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    xTaskNotify(taskHandle, MQTT_CONNECTED, eSetValueWithOverwrite);
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    xTaskNotify(taskHandle, MQTT_PUBLISHED, eSetValueWithOverwrite);
    break;
  case MQTT_EVENT_DATA:                           //以后更多收到数据的程序会在这里添加。
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);
    cJSON *root =  cJSON_Parse(event->data);
    cJSON *mode_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    if( mode_item->valueint == 200 ){
       ESP_LOGI(TAG, "MQTT_PUB_CHECKED");
       isPubSuccessful = 1 ;
    }else
    {
      ESP_LOGI(TAG, "MQTT_PUB_CHECKED failed");
      isPubSuccessful = 0 ;
    }
    xTaskNotify(taskHandle, MQTT_PUB_CHECKED, eSetValueWithOverwrite);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;
  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  mqtt_event_handler_cb(event_data);
}

void Wifi_MQTTLogic()
{

   //赋值静态定义给meta_info
    iotx_dev_meta_info_t meta_info;
    iotx_sign_mqtt_t sign_mqtt;
    //memcpy(meta_info)
    memset(&meta_info, 0, sizeof(iotx_dev_meta_info_t));

    //下面的代码是将上面静态定义的设备身份信息赋值给meta_info
    memcpy(meta_info.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY));
    memcpy(meta_info.product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));
    memcpy(meta_info.device_name, DEVICE_NAME, strlen(DEVICE_NAME));
    memcpy(meta_info.device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));
    //调用签名函数，生成MQTT连接时需要的各种数据
    IOT_Sign_MQTT(IOTX_CLOUD_REGION_SHANGHAI, &meta_info, &sign_mqtt);    
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .host = sign_mqtt.hostname,
        .port = 1883,
        .keepalive = 600,
        .password = sign_mqtt.password,
        .client_id = sign_mqtt.clientid,
        .username = sign_mqtt.username,
        //.event_handle = mqtt_event_handler,   貌似应该在client_register_event()中的参数去设置，暂时去掉这个项目
    };
  uint32_t command = 0;
  esp_mqtt_client_handle_t client = NULL;

  while (true)
  {
    xTaskNotifyWait(0, 0, &command, portMAX_DELAY);
    switch (command)
    {
    case WIFI_CONNEECTED:
      client = esp_mqtt_client_init(&mqtt_cfg);
      esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
      esp_mqtt_client_start(client);
      break;
    case MQTT_CONNECTED:;
      char Event_Post_Topic[100];
      sprintf(Event_Post_Topic,"/sys/%s/%s/thing/event/property/post", PRODUCT_KEY, DEVICE_NAME); // "/sys/a1m81zhi11W/THSD_Data/thing/event/property/post";
       char Event_Post_Payload[200];
      sprintf(Event_Post_Payload,"{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"%s\":%5.1f,\"%s\":%5.1f,\"%s\":%d},\"method\": \"thing.event.property.post\"}","Temperature_In",Tin,"Temperature_Out",Tout,"Fan_Power_Status",Fan_Power_Status);
     esp_mqtt_client_publish(client,Event_Post_Topic, Event_Post_Payload, strlen(Event_Post_Payload) , 1, 0);
/*    
      char *Event_Post_Topic = "/sys/a1m81zhi11W/THSD_Data/thing/event/property/post";
      char *Event_Post_Payload = "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"DewPoint\":16},\"method\": \"thing.event.property.post\"}";
      esp_mqtt_client_publish(client,Event_Post_Topic, Event_Post_Payload, strlen(Event_Post_Payload) , 1, 0); */

/*    esp_mqtt_client_subscribe(client, "/topic/my/subscription/1", 2);
      char data[50];
      sprintf(data, "%d", sensorReading);
      printf("sending data: %d", sensorReading);
      esp_mqtt_client_publish(client, "topic/my/publication/1", data, strlen(data), 2, false);
      break; */
      break;
    case MQTT_PUBLISHED:
      break;
    case MQTT_PUB_CHECKED:
      //esp_mqtt_client_stop(client); 这句已经包含在 destroy 里面了
      esp_mqtt_client_destroy(client);
      esp_wifi_stop();
      return;
    default:
      break;
    }
  }
}

void Waiting_Sensor_Reading(void *params)
{
  while (true)
  {
    if (xSemaphoreTake(readingSemaphore,portMAX_DELAY)) 
    {
      ESP_ERROR_CHECK(esp_wifi_start());
      Wifi_MQTTLogic();
    }
  }
}

float NTC3950_Reading(adc1_channel_t channel){          //读取ADC数值，计算成温度，返回温度值
    int val_Sample = 0;
    int Sample_Number = 20;
    for(int i =0; i < Sample_Number; i++){
      val_Sample += adc1_get_raw(channel);
    }
    val_Sample /=  Sample_Number;
    uint32_t Vmeasure = esp_adc_cal_raw_to_voltage(val_Sample, adc_chars);                  //总结：ESP-IDF 已经做好了ADC之后换算成电压的函数，直接调用。害的我自己瞎算半天
    float rThermistor = 10000.0 * (3300-Vmeasure) / Vmeasure;                               //按自己的电路计算热敏电阻值
    float tCelsius = (3950*298.15)/(3950 + (log(rThermistor / 100000) * 298.15 )) - 273.15; // NTC3950 热敏电阻计算温度的计算公式，google可查询
    return tCelsius;
}

void GPIO_Reading_Handling(void *params)
{
  //GPIO init for ADC
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten( ADC1_CHANNEL_5, ADC_ATTEN_DB_0); //ADC1_CHANNEL_5,     /*!< ADC1 channel 5 is GPIO33 (ESP32), GPIO6 (ESP32-S2) */
  adc1_config_channel_atten( ADC1_CHANNEL_6, ADC_ATTEN_DB_0);
  adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_0db, ADC_WIDTH_BIT_12, 1100, adc_chars); //注：adc_chars 将会被用于ADC电压的计算
     if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
    //GPIO init for Power_On relay control
    gpio_pad_select_gpio(Fan_Power_Pin);
    gpio_set_direction(Fan_Power_Pin, GPIO_MODE_OUTPUT);


  //逐渐修改读ADC和温敏电阻程序
  while (true)
  {
   for(int i = 0; i < 3; i++){                       //每一分钟对温度进行一次监测，每30次监测上报一次数据
         //ADC read
        Tin = NTC3950_Reading(ADC1_CHANNEL_5); 
        Tout = NTC3950_Reading(ADC1_CHANNEL_6);
        //判断风扇开关条件
        if(Fan_Power_Status == true && Tin < 25 && Tout < 30){
            gpio_set_level(Fan_Power_Pin, false);
            Fan_Power_Status = false;
            Restart_countdown = 10;                   //每次停机触发重新启机倒数，倒数到规定次数才允许重新启动，避免频繁启停。
            ESP_LOGW(TAG,"Temp warning: Tin = %f;\t Tout = %f; Fan Power off. \n", Tin, Tout);
          };
        if (Restart_countdown == 0)
        {
          if(Fan_Power_Status == false && Tin > 25 ){
            gpio_set_level(Fan_Power_Pin, true);
            Fan_Power_Status = true;
            ESP_LOGW(TAG,"Restart fan: Tin = %f;\t Tout = %f; Fan Power On!\n", Tin, Tout);
          }
        } else
        {
          Restart_countdown--;
        }
        ESP_LOGI(TAG,"Normal condition: Tin = %f;\t Tout = %f; \tFan_Power_Status is %s; \t Restart_countdown: %d.\n", Tin, Tout,Fan_Power_Status?"On":"Off",Restart_countdown);
        vTaskDelay(pdMS_TO_TICKS(2000));    //每60秒执行一次温度检查。如果异常处理，如果正常30分钟退出循环，到while（）循环中上报一次数据
     }
      //Semaphore通知上报数据
      ESP_LOGI(TAG,"Upload data to cloud: Tin = %f;\t Tout = %f;\t Fan_Power_Status: %s.\n", Tin, Tout, Fan_Power_Status? "On": "Off");
      xSemaphoreGive(readingSemaphore); 
  }
}

void HMI(void *params){
      gpio_pad_select_gpio(LED_PIN);
      gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
      bool LED_Status = true; 
      while(true){
        if(LED_Status == true){
          gpio_set_level(LED_PIN, false);
          LED_Status = false;
        }
        else
        {
          gpio_set_level(LED_PIN, true);
          LED_Status = true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000 * (1 + Fan_Power_Status + isPubSuccessful) ));
      }

}
void app_main()
{
  vTaskDelay(pdMS_TO_TICKS(2000));
  readingSemaphore = xSemaphoreCreateBinary();
  wifiInit();             //源程序修改，只init，start移到 Waiting_Sensor_Reading(),每次 seamaphore通知后Start
  xTaskCreate(Waiting_Sensor_Reading, "Waiting_Sensor_Reading", 1024 * 5, NULL, 5, &taskHandle);
  xTaskCreate(GPIO_Reading_Handling, "GPIO_Reading_Handling", 1024 * 5, NULL, 5, NULL);
  xTaskCreate(HMI, "HMI", 1024 * 2, NULL, 5, NULL);
}