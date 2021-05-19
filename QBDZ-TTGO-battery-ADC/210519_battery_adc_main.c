/* ADC1 Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "math.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "st7789.h"
#include "fontx.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   8          //Multisampling

static esp_adc_cal_characteristics_t *adc_chars;
#if CONFIG_IDF_TARGET_ESP32
static const adc_channel_t channel = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#elif CONFIG_IDF_TARGET_ESP32S2
static const adc_channel_t channel = ADC_CHANNEL_6;     // GPIO7 if ADC1, GPIO17 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

//int battery_voltage = 0;//测试用
int battery_percent = 0;
/*
电池电量定义：4.1（100%），3.7（0%），4.3（充电）

*/


static void check_efuse(void)
{
#if CONFIG_IDF_TARGET_ESP32
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
#elif CONFIG_IDF_TARGET_ESP32S2
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
    }
#else
#error "This example is configured for ESP32/ESP32S2."
#endif
}


static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

void HMI(void * params){
    
	FontxFile fx24G[2];
	FontxFile fx32G[2];
	InitFontx(fx24G,"/spiffs/ILGH24XB.FNT",""); // 12x24Dot Gothic
	InitFontx(fx32G,"/spiffs/ILGH32XB.FNT","");
	TFT_t dev;
	spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
	lcdInit(&dev, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);
    lcdFillScreen(&dev,BLACK);
	//本设备信息
	lcdSetFontDirection(&dev, 1);		
    uint8_t ascii[20];
    char  voltage_display[10];
	strcpy((char *)ascii, "MS-2C");	
	lcdSetFontUnderLine(&dev, RED);
	lcdDrawString(&dev, fx24G, 0, 100, ascii, BLUE);
	lcdUnsetFontUnderLine(&dev);

    //画电池电量的坐标
    uint8_t battery_sign_x_base = 115;
    uint8_t battery_sign_y_base = 200;
    while (true){
        lcdFillScreen(&dev, BLACK);
        if(battery_percent > 100){                                              //正在充电
        lcdDrawRoundRect(&dev,battery_sign_x_base,battery_sign_y_base, battery_sign_x_base + 15, battery_sign_y_base + 30, 2, BLUE);
        lcdDrawRoundRect(&dev, battery_sign_x_base + 5, battery_sign_y_base + 30,battery_sign_x_base + 10, battery_sign_y_base + 30+3,1,BLUE );
        lcdDrawTriangle(&dev,battery_sign_x_base + 5,battery_sign_y_base+10,5,10,180,YELLOW);
        lcdDrawTriangle(&dev,battery_sign_x_base + 10,battery_sign_y_base+20,5,10,0,YELLOW);
        } else if (battery_percent <= 100 && battery_percent > 20){             //合理电压区间
        lcdDrawRoundRect(&dev,battery_sign_x_base,battery_sign_y_base, battery_sign_x_base + 15, battery_sign_y_base + 30, 2, BLUE);
        lcdDrawRoundRect(&dev, battery_sign_x_base + 5, battery_sign_y_base + 30,battery_sign_x_base + 10, battery_sign_y_base + 30+3,1,BLUE );
        lcdDrawFillRect(&dev,battery_sign_x_base+1 ,battery_sign_y_base, battery_sign_x_base +14,battery_sign_y_base + (29 * battery_percent / 100),BLUE );
        }
        else        {                                                           //需要充电
        lcdDrawRoundRect(&dev,battery_sign_x_base,battery_sign_y_base, battery_sign_x_base + 15, battery_sign_y_base + 30, 2, YELLOW);
        lcdDrawRoundRect(&dev, battery_sign_x_base + 5, battery_sign_y_base + 30,battery_sign_x_base + 10, battery_sign_y_base + 30+3,1,YELLOW );
        lcdDrawFillRect(&dev,battery_sign_x_base+1 ,battery_sign_y_base, battery_sign_x_base +14,battery_sign_y_base + (29 * battery_percent / 100),YELLOW );
        }
         //测试时用于显示实际电压        
        //sprintf(voltage_display, "%d mv", (int)battery_voltage);
        //lcdDrawString(&dev, fx32G, 50,50, (uint8_t *) voltage_display, BLUE);

        vTaskDelay(pdMS_TO_TICKS(2000));

    }
}
/*电池电压监测（适用于 TTGO T-Display）*/
void battery_voltage_monitoring(void * params){
    //Check if Two Point or Vref are burned into eFuse
    check_efuse();
    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }
    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);
    //Continuously sample ADC1
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)channel);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)channel, width, &raw);
                adc_reading += raw;
            }
        }
        adc_reading /= NO_OF_SAMPLES;
        //Convert adc_reading to voltage in mV
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        //计算显示电池电量的百分比        
        battery_percent =(int) ((float)((voltage * 2 - 3700.00) / (4250.00 -3700.00)) * 100);
        printf("Raw: %d\tVoltage: %dmV\t percent: %d %% \n", adc_reading, voltage, battery_percent);
        // battery_percent = 10;
        // for(int i = 0; i < 10; i ++){
        // battery_percent += 10;
        // vTaskDelay(pdMS_TO_TICKS(4000));
        // }
        vTaskDelay(pdMS_TO_TICKS(2000));
        }
}
void app_main(void)
{


    //SPI for HMI fonts etc.
    esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 10,
		.format_if_mount_failed =true
	};

	// Use settings defined above toinitialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is anall-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(__func__, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(__func__, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(__func__, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total,&used);
	if (ret != ESP_OK) {
		ESP_LOGE(__func__,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(__func__,"Custom partition(fonts) size: total: %d, used: %d", total, used);
	}


    xTaskCreate(HMI, "HMI", 1024*6, NULL, 2, NULL);
    xTaskCreate(battery_voltage_monitoring,"battery_voltage_monitoring",1024 *2, NULL,1, NULL);
    
    
}
