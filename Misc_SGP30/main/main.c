/* 210203,尝试整合mqtt_aiot上报数据到本程序
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "SGP30.h"

//从mqtt_aiot增加的库
#include <string.h>
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <unistd.h>
#include <pthread.h>
#include "aiot_state_api.h"
#include "aiot_sysdep_api.h"
#include "aiot_mqtt_api.h"

//Nimble BLE 功能增加的库和变量, idf.py menuconfig 中enable nimble
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"  //host stack
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/*自己指定的，用于和手机（小程序）ble 通讯的UUID。按要求倒序排列 */
#define BLE_THSD_SERVICE_UUID BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x00, 0x10, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
//Service UUID: "91110106-0627-7460-1000-911101080019"
#define BLE_THSD_CHAR_COMMAND_UUID    BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x01, 0x00, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
// 指令Charicater UUID: "91110106-0627-7460-0001-911101080019"  
#define BLE_THSD_CHAR_DATA_UUID   BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x02, 0x00, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
// 数据Charicater "91110106-0627-7460-0002-911101080019"

/* 210214:尝试调试成最简单的ble Write功能，服务于手机微信小程序定期读取*/
#define DEVICE_NAME "THSD-CK"
char formatted_ble_data[100];       //输出到小程序时，使用的组织成JSON格式的数据。
uint8_t wqj_ble_addr_type;          //flag。程序过程用，不用关心
void ble_app_advertise(void);       //编译用，提前声明，函数在调用函数的后面。
// BLE setting end

/*SGP30 settings*/
#define LED_PIN 22                // 用LED闪烁快慢指示eCO2是否超标(1000ppm)。
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
//SGP30 setting end

/* mqtt_aiot settings */
xSemaphoreHandle cloudUploadReadySemaphore;         //准备好开始向云端上报数据的Semaphore
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      "66665555" //CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY  //尝试重连Wifi最大次数 
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char *TAG = "SGP30_Aiot";
char *product_key       = "a1bdhbK4VTZ";    //三元组，以后在menuconfig中设置
char *device_name       = "W8QeK6QpTYNQ3ad65alD";
char *device_secret     = "b44fcf6c61e825929a5d5e6ed611d71e";
char pub_topic[100];                        //报文topic
char pub_payload[250];                      //报文Payload
extern aiot_sysdep_portfile_t g_aiot_sysdep_portfile;
extern const char *ali_ca_cert;
static pthread_t g_mqtt_process_thread;
static pthread_t g_mqtt_recv_thread;
static uint8_t g_mqtt_process_thread_running = 0;
static uint8_t g_mqtt_recv_thread_running = 0;
static int s_retry_num = 0;
// mqtt aiot setting end

/* BLE functions start*/
/* cb 函数，当gat_svcs中的对应characteristics被Read的时候，执行 */
static int receive_client_command(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg){
    printf("receive command from client %.*s. \n", ctxt->om->om_len, ctxt->om->om_data);
    //TODO： 后续处理，赋值给控制
    return 0;
}
static int update_client_data(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg){
    os_mbuf_append(ctxt->om ,&formatted_ble_data ,strlen(formatted_ble_data));
    ESP_LOGI(__func__,"update sensor data.");
    return 0;
}

/* 声明一个gatt service,可包含多个services, 每个service包含多个characteristics,每个characteristics都有其UUID和access_cb */
static const struct ble_gatt_svc_def gat_svcs[] = {  
    {       /*第一个 Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_THSD_SERVICE_UUID, 
        .characteristics = (struct ble_gatt_chr_def[]){
            {   //第一个 Characteristic: 
                .uuid = BLE_THSD_CHAR_COMMAND_UUID,
                .flags = BLE_GATT_CHR_F_WRITE,
                .access_cb = receive_client_command,
            },
            {   //第二个 Characteristic
                .uuid = BLE_THSD_CHAR_DATA_UUID, 
                .flags = BLE_GATT_CHR_F_READ, 
                .access_cb = update_client_data,
            },{0}}
    },{0}     //表示Service list结束
};

/*  ble_gap_adv_start 时注册的不同 event 的cb 函数*/
static int ble_gap_event(struct ble_gap_event *event, void * arg){
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("GAP","BLE_GAP_EVENT_CONNECT %s",event->connect.status == 0? "OK":"Failed");
        if(event->connect.status != 0){
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
         ESP_LOGI("GAP","BLE_GAP_EVENT_DISCONNECT");
         ble_app_advertise();
        break;    
    case BLE_GAP_EVENT_ADV_COMPLETE:
         ESP_LOGI("GAP","BLE_GAP_EVENT_ADV_COMPLETE");
         ble_app_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI("GAP","BLE_GAP_EVENT_SUBSCRIBE");
        break;
    default:
        break;
    }
    return 0;
}

/* 开始advertise所需要的设置 单独成函数是为了多次调用*/
void ble_app_advertise(void){
    struct ble_hs_adv_fields fields;
    memset(&fields,0, sizeof(fields) );
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_DISC_LTD;
    fields.tx_pwr_lvl_is_present = true;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;  //Power level
    fields.name = (uint8_t *) ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = true;
    ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params adv_params;   // = (struct ble_gap_adv_params){0};  //?新建adv的参数？
    memset(&adv_params,0,sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(wqj_ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

void ble_app_on_sync(void){
    ble_hs_id_infer_auto(0,&wqj_ble_addr_type);     //自动生成地址类型,不需要关心
    ble_app_advertise();
}
/* nimble_port_freertos_init 出来的task*/
void host_task(void *param){
    nimble_port_run();      //2
}
/* 把ble 初始化所需步骤整合到一起，简化程序app_main()*/
void thsd_ble_init(){
    esp_nimble_hci_and_controller_init();  //HCI: Host Controller Interface
    nimble_port_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);  
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gat_svcs);
    ble_gatts_add_svcs(gat_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;  //3.  Bluetooth Host main configuration structure “host stack?"
    nimble_port_freertos_init(host_task);   //第一步1
}
// BLE functions end

/* mqtt functions start*/
static void Wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {          // 由esp_wifi_start()触发。
        ESP_LOGI(__func__,"WIFI_EVENT_STA_START. To try connect to Wifi"); 
        esp_wifi_connect();      
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {  //由esp_wifi_connect()连接尝试失败触发。也可能由esp_wifi_stop()触发。
            ESP_LOGI(__func__,"WIFI_EVENT_STA_DISCONNECTED. "); 
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            ESP_LOGI(__func__, "Retry connect to Wifi AP");
            int ret = esp_wifi_connect();
            s_retry_num++;            
            ESP_LOGW(__func__,"wifi_connect() return %d.",ret);
            } 
            //此处删除例程的xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); 用最大等待时间获得Wifi连接失败的判断
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {     //由esp_wifi_connect()连接尝试成功触发。
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(__func__, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = EXAMPLE_ESP_MAXIMUM_RETRY;                                //只要本次操作成功连接，就设置。目的是避免在执行Wifi_stop()的时候，重新触发WIFI_EVENT_STA_DISCONNECTED，再次尝试connect。
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }    
}

int32_t demo_state_logcb(int32_t code, char *message)
{
    printf("%s", message);
    return 0;
}

void demo_mqtt_event_handler(void *handle, const aiot_mqtt_event_t *event, void *userdata)
{
    switch (event->type) {
        /* SDK因为用户调用了aiot_mqtt_connect()接口, 与mqtt服务器建立连接已成功 */
        case AIOT_MQTTEVT_CONNECT: {
            printf("AIOT_MQTTEVT_CONNECT.\n");
            /* TODO: 处理SDK建连成功, 不可以在这里调用耗时较长的阻塞函数 */
        }
        break;

        /* SDK因为网络状况被动断连后, 自动发起重连已成功 */
        case AIOT_MQTTEVT_RECONNECT: {
            printf("AIOT_MQTTEVT_RECONNECT\n");
            /* TODO: 处理SDK重连成功, 不可以在这里调用耗时较长的阻塞函数 */
        }
        break;

        /* SDK因为网络的状况而被动断开了连接, network是底层读写失败, heartbeat是没有按预期得到服务端心跳应答 */
        case AIOT_MQTTEVT_DISCONNECT: {
            char *cause = (event->data.disconnect == AIOT_MQTTDISCONNEVT_NETWORK_DISCONNECT) ? ("network disconnect") :
                          ("heartbeat disconnect");
            printf("AIOT_MQTTEVT_DISCONNECT: %s\n", cause);
            /* TODO: 处理SDK被动断连, 不可以在这里调用耗时较长的阻塞函数 */
        }
        break;
        default: {
        }
    }
}

void demo_mqtt_default_recv_handler(void *handle, const aiot_mqtt_recv_t *packet, void *userdata)
{
    switch (packet->type) {
        case AIOT_MQTTRECV_HEARTBEAT_RESPONSE: {
            printf("heartbeat response\n");
            /* TODO: 处理服务器对心跳的回应, 一般不处理 */
        }
        break;

        case AIOT_MQTTRECV_SUB_ACK: {
            printf("sub_ack, res: -0x%04X, packet id: %d, max qos: %d\n",
                   -packet->data.sub_ack.res, packet->data.sub_ack.packet_id, packet->data.sub_ack.max_qos);
            /* TODO: 处理服务器对订阅请求的回应, 一般不处理 */
        }
        break;

        case AIOT_MQTTRECV_PUB: {
            printf("pub, qos: %d, topic: %.*s\n", packet->data.pub.qos, packet->data.pub.topic_len, packet->data.pub.topic);
            printf("pub, payload: %.*s\n", packet->data.pub.payload_len, packet->data.pub.payload);
            /* TODO: 处理服务器下发的业务报文 */
        }
        break;

        case AIOT_MQTTRECV_PUB_ACK: {
            printf("pub_ack, packet id: %d\n", packet->data.pub_ack.packet_id);
            /* TODO: 处理服务器对QoS1上报消息的回应, 一般不处理 */
        }
        break;

        default: {

        }
    }
}

void *demo_mqtt_process_thread(void *args)
{
    int32_t res = STATE_SUCCESS;
    while (g_mqtt_process_thread_running) {
        res = aiot_mqtt_process(args);
        if (res == STATE_USER_INPUT_EXEC_DISABLED) {
            break;
        }
        sleep(1);
    }
    return NULL;
}

void *demo_mqtt_recv_thread(void *args)
{
    int32_t res = STATE_SUCCESS;

    while (g_mqtt_recv_thread_running) {
        res = aiot_mqtt_recv(args);
        if (res < STATE_SUCCESS) {
            if (res == STATE_USER_INPUT_EXEC_DISABLED) {
                break;
            }
            sleep(1);
        }
    }
    return NULL;
}

int linkkit_main(void)
{
    int32_t     res = STATE_SUCCESS;
    void       *mqtt_handle = NULL;
    char       *url = "iot-as-mqtt.cn-shanghai.aliyuncs.com"; /* 阿里云平台上海站点的域名后缀 */
    char        host[100] = {0}; /* 用这个数组拼接设备连接的云平台站点全地址, 规则是 ${productKey}.iot-as-mqtt.cn-shanghai.aliyuncs.com */
    uint16_t    port = 443;      /* 无论设备是否使用TLS连接阿里云平台, 目的端口都是443 */
    aiot_sysdep_network_cred_t cred; /* 安全凭据结构体, 如果要用TLS, 这个结构体中配置CA证书等参数 */

    /* 配置SDK的底层依赖 */
    aiot_sysdep_set_portfile(&g_aiot_sysdep_portfile);
    /* 配置SDK的日志输出 */
    aiot_state_set_logcb(demo_state_logcb);

    /* 创建SDK的安全凭据, 用于建立TLS连接 */
    memset(&cred, 0, sizeof(aiot_sysdep_network_cred_t));
    cred.option = AIOT_SYSDEP_NETWORK_CRED_SVRCERT_CA;  /* 使用RSA证书校验MQTT服务端 */
    cred.max_tls_fragment = 16384; /* 最大的分片长度为16K, 其它可选值还有4K, 2K, 1K, 0.5K */
    cred.sni_enabled = 1;                               /* TLS建连时, 支持Server Name Indicator */
    cred.x509_server_cert = ali_ca_cert;                 /* 用来验证MQTT服务端的RSA根证书 */
    cred.x509_server_cert_len = strlen(ali_ca_cert);     /* 用来验证MQTT服务端的RSA根证书长度 */

    /* 创建1个MQTT客户端实例并内部初始化默认参数 */
    mqtt_handle = aiot_mqtt_init();
    if (mqtt_handle == NULL) {
        printf("aiot_mqtt_init failed\n");
        return -1;
    }

    snprintf(host, 100, "%s.%s", product_key, url);
    /* 配置MQTT服务器地址 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_HOST, (void *)host);
    /* 配置MQTT服务器端口 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PORT, (void *)&port);
    /* 配置设备productKey */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PRODUCT_KEY, (void *)product_key);
    /* 配置设备deviceName */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_NAME, (void *)device_name);
    /* 配置设备deviceSecret */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_SECRET, (void *)device_secret);
    /* 配置网络连接的安全凭据, 上面已经创建好了 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_NETWORK_CRED, (void *)&cred);
    /* 配置MQTT默认消息接收回调函数 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_RECV_HANDLER, (void *)demo_mqtt_default_recv_handler);
    /* 配置MQTT事件回调函数 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_EVENT_HANDLER, (void *)demo_mqtt_event_handler);

    /* 与服务器建立MQTT连接 */
    res = aiot_mqtt_connect(mqtt_handle);
    if (res < STATE_SUCCESS) {
        /* 尝试建立连接失败, 销毁MQTT实例, 回收资源 */
        aiot_mqtt_deinit(&mqtt_handle);
        printf("aiot_mqtt_connect failed: -0x%04X\n", -res);
        return -1;
    }

    /* MQTT 发布消息功能示例, 请根据自己的业务需求进行使用 */
    {
        //利用传感器Task(本程序是 sgpTask(void* pvParams))中组件的Topic和Payload上报MQTT消息
        res = aiot_mqtt_pub(mqtt_handle, pub_topic, (uint8_t *)pub_payload, strlen(pub_payload), 0);
        if (res < 0) {
            printf("aiot_mqtt_sub failed, res: -0x%04X\n", -res);
            return -1;
        }
    }

    /* 创建一个单独的线程, 专用于执行aiot_mqtt_process, 它会自动发送心跳保活, 以及重发QoS1的未应答报文 */
    g_mqtt_process_thread_running = 1;
    res = pthread_create(&g_mqtt_process_thread, NULL, demo_mqtt_process_thread, mqtt_handle);
    if (res < 0) {
        printf("pthread_create demo_mqtt_process_thread failed: %d\n", res);
        return -1;
    }

    /* 创建一个单独的线程用于执行aiot_mqtt_recv, 它会循环收取服务器下发的MQTT消息, 并在断线时自动重连 */
    g_mqtt_recv_thread_running = 1;
    
    res = pthread_create(&g_mqtt_recv_thread, NULL, demo_mqtt_recv_thread, mqtt_handle);
    if (res < 0) {
        printf("pthread_create demo_mqtt_recv_thread failed: %d\n", res);
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(60000)); //等待一段时间，收取云反馈或指令

    /* 断开MQTT连接, 一般不会运行到这里 */
    ESP_LOGI(__func__, "aiot_mqtt_disconnect()");
    res = aiot_mqtt_disconnect(mqtt_handle);
    if (res < STATE_SUCCESS) {
        aiot_mqtt_deinit(&mqtt_handle);
        printf("aiot_mqtt_disconnect failed: -0x%04X\n", -res);
        return -1;
    }

    /* 销毁MQTT实例, 一般不会运行到这里 */
    ESP_LOGI(__func__, "aiot_mqtt_deinit()");
    res = aiot_mqtt_deinit(&mqtt_handle);
    if (res < STATE_SUCCESS) {
        printf("aiot_mqtt_deinit failed: -0x%04X\n", -res);
        return -1;
    }
    ESP_LOGI(__func__, "to Stop Threads!");
    g_mqtt_process_thread_running = 0;
    g_mqtt_recv_thread_running = 0;
    pthread_join(g_mqtt_process_thread, NULL);
    pthread_join(g_mqtt_recv_thread, NULL);
    return 0;
}

void wifi_aiot_start(void){
    ESP_LOGI(__func__, "wifi_init_start.");
    s_retry_num = 0;                        //连接失败时，尝试重连次数复位。
    ESP_ERROR_CHECK(esp_wifi_start() );  // luanch WIFI_EVENT_STA_START
    /* 等待(WIFI_CONNECTED_BIT) or timeout（连接尝试失败）The bit is set by Wifi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT,         //取消  | WIFI_FAIL_BIT, 用timeout 来处理等待时间
            pdTRUE,                     //函数返回前本bit 复位，原例程为pdFALSE。否则影响下一次 wifi_aiot_start(void)
            pdFALSE,                    // Don't wait for both bits, either bit will do.
            pdMS_TO_TICKS(5000));       // Timeout设置。 原例程portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {            //在这里继续aiot_mqtt 的 init 和 Pub 操作,结束后关闭Wifi。
        ESP_LOGI(__func__, "connected to ap SSID:%s password:%s",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        ESP_LOGI(__func__, "Start linkkit_main and wait for return.");
        int ret = linkkit_main();
        if (ret == ESP_OK){
            ESP_LOGI(__func__, "Job done! To dowifi_stop()");
            ESP_ERROR_CHECK(esp_wifi_stop());
            }
        else{
            ESP_LOGW(__func__, "linkkit_main return error = %d. To do Wifi_stop()", ret);
            ESP_ERROR_CHECK(esp_wifi_stop());
            }
    } else {
        ESP_LOGE(__func__, "xEventGroupWaitBits timeout! Failed to connect to SSID:%s, password:%s.",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_LOGW(__func__,"TODO somthing to record failed to upload data to cloud."); 
    }
}
// mqtt aiot functions end

void cloud_Upload(void *params){                        //程序开始云上报task的入口
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default()); 
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    while (true){
        if (xSemaphoreTake(cloudUploadReadySemaphore,portMAX_DELAY)) {
            ESP_LOGI(__func__, "Start wifi and Aiot handling.");
            wifi_aiot_start();
        }
    }

}
/*根据功能变动的程序，在这里组织aiot pub topic&payload，组织BLE read callback 调用的输出*/
static void sgpTask(void* pvParams) {
    for(int i = 0; i < 30; i++){   //开机前15秒数据无效，先执行30秒的开机循环，等待SPG30进入正常工作状态
            esp_err_t err;
            if((err = sgp30_ReadData()) == ESP_OK) {
            ESP_LOGI(__func__, "tVoC: %dppb, eCO2: %dppm",
                     sgp30_Data.tVoC, sgp30_Data.eCO2);
            } else {
            ESP_LOGE(__func__, "SGP READ ERROR:: 0x%04X", err);
            }
            if(sgp30_Data.eCO2 > CONFIG_eCO2_Alarm_PPM){
                eCO2_Alarm = true;
            }else
            {
            eCO2_Alarm = false;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS); //为了SGP30能自动修正Baseline，必须每一秒发送一次 “sgp30_measure_iaq”
        }
    while(true) {
        sprintf(pub_topic,"/sys/%s/%s/thing/event/property/post", product_key, device_name); 
        sprintf(pub_payload,"{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"%s\":%d,\"%s\":%d}}","eCO2",sgp30_Data.eCO2,"voc",sgp30_Data.tVoC); //TODO:增加物模型
        ESP_LOGI (__func__, "Pub payload ready. Give semaphore\n");
        xSemaphoreGive(cloudUploadReadySemaphore);
        for(int i = 0; i < 60 * 30; i++){  //初次上报数据后，每半小时上报一次数据。
            esp_err_t err;
            if((err = sgp30_ReadData()) == ESP_OK) {
            ESP_LOGI(__func__, "tVoC: %dppb, eCO2: %dppm",
                     sgp30_Data.tVoC, sgp30_Data.eCO2);
                     sprintf(formatted_ble_data,"{\"tVoC\":%d,\"eCO2\":%d}",sgp30_Data.tVoC, sgp30_Data.eCO2);
            } else {
            ESP_LOGE(__func__, "SGP READ ERROR:: 0x%04X", err);
            }
            if(sgp30_Data.eCO2 > CONFIG_eCO2_Alarm_PPM){
                eCO2_Alarm = true;
            }else
            {
            eCO2_Alarm = false;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS); //为了SGP30能自动修正Baseline，必须每一秒发送一次 “sgp30_measure_iaq”
        }
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
        //ESP_LOGI(__func__, "LED_Status: %d, eCO2_alarm: %d", LED_Status, eCO2_Alarm);
        vTaskDelay(pdMS_TO_TICKS(2000 / (1 + eCO2_Alarm *3) ));
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
    cloudUploadReadySemaphore = xSemaphoreCreateBinary(); 
// BLE initial
    thsd_ble_init();
// BLE end
    xTaskCreate(sgpTask, "sgpTask", 4096, NULL, 5, NULL);
    xTaskCreate(sgpBaselineTask, "sgpBaselineTask", 4096, NULL, 5, NULL);
    xTaskCreate(cloud_Upload, "cloud_Upload", 1024 * 4, NULL, 5, NULL);
    xTaskCreate(HMI, "HMI", 1024 * 2, NULL, 5, NULL);

}


