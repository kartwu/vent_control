
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "SGP30.h"

#define LED_PIN 22                // 用LED闪烁快慢指示eCO2是否超标(1000ppm),核查22已经被i2c用了，暂时换成27
bool eCO2_Alarm = false;

#define NVS_BASE_NAME "storage"
#define SGP_TVOC_BASE_KEY "SGP_TVOC"
#define SGP_ECO2_BASE_KEY "SGP_ECO2"

#ifndef REAL_MS
#define REAL_MS(n) ((n) / portTICK_PERIOD_MS)
#endif
#ifndef REAL_SEC
#define REAL_SEC(n) ((n)*1000 / portTICK_PERIOD_MS)
#endif
#ifndef CHECK_ERROR
#define CHECK_ERROR(x) if(x != ESP_OK) {return x;}
#endif


static void sgpTask(void* pvParams) {
    while(true) {
        esp_err_t err;
        if((err = sgp30_ReadData()) == ESP_OK) {
            ESP_LOGI(__func__, "tVoC: %dppb, eCO2: %dppm",
                     sgp30_Data.tVoC, sgp30_Data.eCO2);
        } else {
            ESP_LOGE(__func__, "SGP READ ERROR:: 0x%04X", err);
        }
        if(sgp30_Data.eCO2 > 1000){
            eCO2_Alarm = true;
        }else
        {
            eCO2_Alarm = false;
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS); //为了SGP30能自动修正Baseline，必须每一秒发送一次 “sgp30_measure_iaq”
    }
    vTaskDelete(NULL);
}




static void sgpBaselineTask(void* pvParams) {
    static uint8_t baseline_set_nvs_count = 0;  //更新Baseline的次数
    static bool is_Baseline_NVS_Saved = false;  // nvs是否有存储有效的Baseline data
    static uint16_t tVocBase = 0xFFFF;
    static uint16_t eCo2Base = 0xFFFF;
    bool NVS_load_Baseline_OK = true;           //是否成功从NVS读取Baseline Data

    /*  检查是否有nvs存贮的Baseline 数据，如果有，is_Baseline_NVS_Saved 置 true. */
    nvs_handle nvsReadHandle;
    esp_err_t ret = nvs_open(NVS_BASE_NAME, NVS_READWRITE, &nvsReadHandle);
    ESP_ERROR_CHECK(ret);

    if ((ret = nvs_get_u16(nvsReadHandle, SGP_TVOC_BASE_KEY, &tVocBase)) != ESP_OK) {
        ESP_LOGE(__func__, "SGP30::Get tVoc_base from NVS failed %s(0x%X). \n", esp_err_to_name(ret), ret);
        NVS_load_Baseline_OK = NVS_load_Baseline_OK && false;
    }
    if ((ret = nvs_get_u16(nvsReadHandle, SGP_ECO2_BASE_KEY, &eCo2Base)) != ESP_OK) {
        ESP_LOGE(__func__, "SGP30::Get eCO2_base from NVS failed %s(0x%X). \n", esp_err_to_name(ret), ret);
        NVS_load_Baseline_OK = NVS_load_Baseline_OK && false;
    }
    if (NVS_load_Baseline_OK) {
        ESP_LOGI(__func__, "Baseline history data loaded from NVS OK! tVoc_base = 0x%x; eCO2_base = 0x%x. \n", tVocBase, eCo2Base);
        is_Baseline_NVS_Saved = true;
    }
    nvs_close(nvsReadHandle);

    while (true) {
        
        vTaskDelay(REAL_SEC(3600));  //每1小时一个循环
        //vTaskDelay(REAL_SEC(10));      //测试程序临时用
        ESP_LOGI(__func__, "In sgpBaselineTask while loop. Baseline_set_nvs_count = %d. \n", baseline_set_nvs_count);  //添加
        if (++baseline_set_nvs_count > 12) {       //如果初始运行（isBurn 为 false），12小时后，get_Baseline() 才有效
            is_Baseline_NVS_Saved = true;
        }
        if (is_Baseline_NVS_Saved) {
            nvs_handle nvsHandle;
            esp_err_t err = nvs_open(NVS_BASE_NAME, NVS_READWRITE, &nvsHandle);
            ESP_ERROR_CHECK(err);
            if (sgp30_GetBaseline() == ESP_OK) {
                ESP_LOGI(__func__, "New SGP30 Baseline read OK! tVocBase = 0x%x; eCO2Base =  0x%x.\n", sgp30_Data.tVoC_baseline, sgp30_Data.eCO2_baseline);
                tVocBase = sgp30_Data.tVoC_baseline;
                eCo2Base = sgp30_Data.eCO2_baseline;

                /*   新Baseline 参数更新到NVS   */
                if ((err = nvs_set_u16(nvsHandle, SGP_TVOC_BASE_KEY, tVocBase)) == ESP_OK) {
                    if ((err = nvs_set_u16(nvsHandle, SGP_ECO2_BASE_KEY, eCo2Base)) == ESP_OK) {
                        if ((err = nvs_commit(nvsHandle)) == ESP_OK) {
                            ESP_LOGI(__func__, "New SGP30 Baseline save to NVS OK!.\n");
                        } else {
                            ESP_LOGE(__func__, "New SGP30 Baseline save to NVS falied, ERROR is %s(0x%X)", esp_err_to_name(ret), err);
                        }
                    } else {
                        ESP_LOGI(__func__, "New eCo2 Baseline save to NVS failed. ERROR is %s(0x%X)", esp_err_to_name(ret), err);
                    }
                } else {
                    ESP_LOGE(__func__, "New tVoc Baseline save to NVS failed. ERROR is %s(0x%X)", esp_err_to_name(ret), err);
                }
            } else {
                ESP_LOGE(__func__, "New SGP30 Baseline read failed!");
            }
            nvs_close(nvsHandle);
        }
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
        ESP_LOGI(__func__, "LED_Status: %d, eCO2_alarm: %d", LED_Status, eCO2_Alarm);
        vTaskDelay(pdMS_TO_TICKS(1000 / (1 + eCO2_Alarm *2) ));
      }

}

void app_main() {
    /* SGP30实例初始化，测试SGP30，读取ID和version，验证SGP30就绪。*/
    sgp30_param_t sgp30Param = SGP30_ESP32_HAL_DEFAULT;
    sgp30Param.i2c_freq_hz = CONFIG_I2C_SPEED * 1000;
    sgp30Param.scl = CONFIG_I2C_SCL;
    sgp30Param.sda = CONFIG_I2C_SDA;
    sgp30Param.i2c_port = I2C_NUM_0;      //git 上是 I2C_NUM_1
    sgp30Param.using_library_i2c = true;
    esp_err_t err = sgp30_Init(sgp30Param);   //读取ID和version，验证SGP30就绪。否则系统重启
    if(err != ESP_OK){
        esp_restart();
    }
    /*     NVS中存储的Baseline data 读取和设置到SGP30    */
    err = ESP_OK;
    uint16_t tVocBase = 0xFFFF;
    uint16_t eCo2Base = 0xFFFF;

    bool NVS_load_Baseline_OK = true;
    nvs_handle nvsHandle;
    ESP_ERROR_CHECK(nvs_flash_init());          //github 上的例程没有这句以及相应的.h
    esp_err_t ret = nvs_open(NVS_BASE_NAME, NVS_READWRITE, &nvsHandle);
    if ((ret = nvs_get_u16(nvsHandle, SGP_TVOC_BASE_KEY, &tVocBase)) != ESP_OK) {                           //从NVS获取 SGP_TVOC_BASE_KEY
        ESP_LOGE(__func__, "SGP30::Get tVoc_base from NVS failed 0x%X", ret);
        NVS_load_Baseline_OK = NVS_load_Baseline_OK & false;
        tVocBase = 0;
        if((err = nvs_set_u16(nvsHandle, SGP_TVOC_BASE_KEY, tVocBase)) != ESP_OK) {                         //否则设置 SGP_TVOC_BASE_KEY 为0
            ESP_LOGE(__func__, "%s: nvs_set_u16 failed %s(0x%X). failback to AQ_PM025", __func__, esp_err_to_name(err), err);
        } else {
            ESP_LOGI(__func__, "%s: nvs_set_u16(tVocBase) success", __func__);
        }
        if((err = nvs_commit(nvsHandle)) != ESP_OK) {                                                       //并且验证 commit 设置成功
            ESP_LOGE(__func__, "%s: nvs_commit failed %s(0x%X). failback to AQ_PM025", __func__, esp_err_to_name(err), err);
        } else {
            ESP_LOGI(__func__, "%s: nvs_commit(tVocBase) success", __func__);
        }
    }
    if ((ret = nvs_get_u16(nvsHandle, SGP_ECO2_BASE_KEY, &eCo2Base)) != ESP_OK) {
        ESP_LOGE(__func__, "SGP30::Get eCO2_base from NVS failed 0x%X", ret);
        NVS_load_Baseline_OK = NVS_load_Baseline_OK & false;
        eCo2Base = 0;
        if((err = nvs_set_u16(nvsHandle, SGP_ECO2_BASE_KEY, eCo2Base)) != ESP_OK) {
            ESP_LOGE(__func__, "%s: nvs_set_u16 failed %s(0x%X). failback to AQ_PM025", __func__, esp_err_to_name(err), err);
        } else {
            ESP_LOGI(__func__, "%s: nvs_set_u16(eCo2Base) success", __func__);
        }
        if((err = nvs_commit(nvsHandle)) != ESP_OK) {
            ESP_LOGE(__func__, "%s: nvs_commit failed %s(0x%X). failback to AQ_PM025", __func__, esp_err_to_name(err), err);
        } else {
            ESP_LOGI(__func__, "%s: nvs_commit(eCo2Base) success", __func__);
        }
    }
    if (NVS_load_Baseline_OK) {
        ESP_LOGI(__func__, "Get BASELINE FROM NVS OK!: %04X eCO2Base: %04X", tVocBase, eCo2Base);
    } else {
        ESP_LOGE(__func__, "Get BASELINE FROM NVS Failed");
    }
    nvs_close(nvsHandle);

    /* 初始化SGP30,进入循环1s测量流程，需要参考SGP30 的 Datasheet,才能正确理解  */
    err = sgp30_IAQ_Init();    

    /*    如果从NVS成功读取Baseline，则发送指令到SGP30,参考本Baseline.    */
    if(NVS_load_Baseline_OK) {
        err = ESP_OK;
        if( (err = sgp30_SetBaseline(tVocBase, eCo2Base)) != ESP_OK ){
                 ESP_LOGW(__func__, "Set Baseline from NVS to SGP30 failed!\n");
        }
        else {
            ESP_LOGI(__func__, "Set Baseline from NVS to SGP30 OK!: tVocBase: %x eCO2Base: %x", tVocBase, eCo2Base);
        }
    }

    xTaskCreate(sgpTask, "sgpTask", 4096, NULL, 5, NULL);
    xTaskCreate(sgpBaselineTask, "sgpBaselineTask", 4096, NULL, 5, NULL);
    xTaskCreate(HMI, "HMI", 1024 * 2, NULL, 5, NULL);

}


