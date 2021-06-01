/*
总结：
1、在 Gitee 上的 REPO 里面存放 Bin File。 然后从浏览器处的（site information）(就在输入地址旁边)获取证书（gitee.cer）。
2、获取存放 .bin file 的URL。
3、证书存在与 Main 目录同级的 cert 目录下。
4、修改 main 目录下的 CMakelists, 添加 set(COMPONENT_EMBED_TXTFILES "../cert/gitee.cer"), 让证书一同写入flash。
5、menuconfig 修改 partition 为 Factory + 2 OTA。
6、
*/

/*
尝试添加新的Wifi Check 程序，来每次开机时，检查OTA。
以后利用相同 SSID， 变化的PASS来控制升级，每一个版本有一个独有的PASSWORD，
为下一次升级连接WIFI用。
控制升级的人员需要控制发放这个密码来控制未经授权人员控制升级操作。
*/



#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"

#define OTA_WIFI_SSID "thsd"       
#define OTA_WIFI_PASS "55551114"      //兼顾version的信息，思考利用git的log来确定这个码，也许应该放在 menuconfig 里面操作。
#define LED_Pin  22
#define OTA_WIFI_COMPLETE       BIT8                 // OTA 完成（含成功或失败），程序继续进行
#define OTA_FAILED              BIT9                      //
#define OTA_WIFI_FAILED         BIT10
static EventGroupHandle_t       QBDZ_Event_Group;
# define TAG "OTA"



extern const uint8_t server_cert_pem_start[] asm("_binary_gitee_cer_start");

esp_err_t  client_event_handler(esp_http_client_event_t *evt)
{
  return ESP_OK;
}


/*OTA专用的 Event_Handler。*/
static void OTA_Wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data){
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {          // 由esp_wifi_start()触发。
        ESP_LOGI(__func__,"WIFI_EVENT_STA_START. To try connect to OTA_Wifi"); 
        esp_wifi_connect(); 
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {  //由esp_wifi_connect()连接尝试失败触发。也可能由esp_wifi_stop()触发。
            ESP_LOGI(__func__,"WIFI_EVENT_STA_DISCONNECTED. ");
        xEventGroupSetBits(QBDZ_Event_Group,OTA_WIFI_FAILED);
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {     //由esp_wifi_connect()连接尝试成功触发。
          ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
          ESP_LOGI(__func__, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
          esp_http_client_config_t clientConfig = {
            .url = "https://gitee.com/kartwu/qbdz-ota/raw/master/QBDZ-OTA.bin", // our ota location
            .event_handler = client_event_handler,
            .cert_pem = (char *)server_cert_pem_start
          };        
          if(esp_https_ota(&clientConfig) == ESP_OK){
            ESP_LOGI(TAG,"OTA flash succsessfull for version %s.", OTA_WIFI_PASS );
            printf("restarting in 5 seconds\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
          } else{
            ESP_LOGE(TAG,"Failed to update firmware");
            xEventGroupSetBits(QBDZ_Event_Group,OTA_FAILED);
          }
        }
     }
/*每次开机开始升级程序，如果升级用的credential不正确，放弃升级。credential正确会完成升级,升级失败会放弃升级。下次升级的credential会被修改。*/
void OTA_Wifi_PU(void *Params){
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OTA_Wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OTA_Wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_config_t wifi_config = {
    .sta = {
      .ssid = OTA_WIFI_SSID,
      .password = OTA_WIFI_PASS
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI("wait OTA"," ");
  xEventGroupWaitBits(QBDZ_Event_Group, OTA_FAILED | OTA_WIFI_FAILED, true, false, pdMS_TO_TICKS(30000));
  ESP_LOGI("OTA process complete without upgrade!"," ");
  esp_event_handler_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID, &OTA_Wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP, &OTA_Wifi_event_handler);
  esp_wifi_disconnect();
  esp_wifi_stop();
  xEventGroupSetBits(QBDZ_Event_Group, OTA_WIFI_COMPLETE);
  ESP_LOGI("OTA task delete"," ");
  vTaskDelete(NULL);
}


void HMI(void *params){
  gpio_pad_select_gpio(LED_Pin);
  gpio_set_direction(LED_Pin,GPIO_MODE_OUTPUT);
  
  ESP_LOGI("HMI task running"," ");
      bool LED_Status = true; 
      while(true){
        if(LED_Status == true){
          gpio_set_level(LED_Pin, false);
          LED_Status = false;
        }
        else{
          gpio_set_level(LED_Pin, true);
          LED_Status = true;
        }
        vTaskDelay(pdMS_TO_TICKS( 300 ));
      }

}

void LOG_P(void * Params){
  while(true){
    ESP_LOGI("I am running...","");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
void Normal_program(void * params){
  ESP_LOGI("Normal program task wait"," ");
  xEventGroupWaitBits(QBDZ_Event_Group, OTA_WIFI_COMPLETE, true,true, portMAX_DELAY); 
  xTaskCreate(HMI, "HMI", 1024 * 2, NULL, 1, NULL);
  xTaskCreate(LOG_P, "LOG_P", 1024 *2, NULL, 1, NULL);
  vTaskDelete(NULL);
}

void app_main(void)
{
  ESP_LOGI("Software OTA upgrade method", "SSID: %s; PASS: %s", OTA_WIFI_SSID,OTA_WIFI_PASS);
  QBDZ_Event_Group = xEventGroupCreate();
  // nvs flash init
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  //wifi_init_prepare()。作为各种用途的wifi_start()前的准备;
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  //每次开机开始升级程序，如果升级用的credential不正确，放弃升级。credential正确会完成升级,升级失败会放弃升级。下次升级的credential会被修改。
  xTaskCreate(OTA_Wifi_PU,"OTA_Wifi_PU",1024 *2, NULL, 3, NULL);
  //其他正常使用的程序包
  xTaskCreate(Normal_program,"Normal_program", 1024*2, NULL, 3, NULL);
}
