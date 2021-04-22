/* Hello World Example
以这个例程为基础，应该可以很快理解和解决 MLX90621，90640等的API使用。
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "MLX90614_API.h"
#include "MLX90614_SMBus_Driver.h"

#define MLX90614_DEFAULT_ADDRESS 0x5A // default chip address(slave address) of MLX90614
/* 曾经尝试其他的 pin 作为 SDA和SCL， 踩坑，未彻底理解，换成常用的 21 22后正常。*/
#define MLX90614_SDA_GPIO 21 // sda for MLX90614
#define MLX90614_SCL_GPIO 22 // scl for MLX90614
#define MLX90614_VCC_GPIO 26 // vcc for MLX90614

void app_main()
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    // setup gpio for MLX90614
    gpio_pad_select_gpio(MLX90614_VCC_GPIO);
    gpio_set_direction(MLX90614_VCC_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MLX90614_VCC_GPIO, 1);
    vTaskDelay(1000/portTICK_RATE_MS);
    MLX90614_SMBusInit(MLX90614_SDA_GPIO, MLX90614_SCL_GPIO, 50000); // sda scl and 50kHz

    float to = 0; // temperature of object
    float ta = 0; // temperature of ambient
    /* dumpInfo 的数据格式有疑问，但能运行。*/
    uint16_t dumpInfo = 0;
    // loop
    while (1)
    {
        // printf("test-data-log:%lf \r\n", temp);
        MLX90614_GetTo(MLX90614_DEFAULT_ADDRESS, &to);
        MLX90614_GetTa(MLX90614_DEFAULT_ADDRESS, &ta);
        MLX90614_GetTa(MLX90614_DEFAULT_ADDRESS, &dumpInfo);
        printf("log:%lf %lf %d\r\n", to, ta, dumpInfo);
        vTaskDelay(1000/portTICK_RATE_MS);
    }
}
