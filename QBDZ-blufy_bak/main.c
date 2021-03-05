#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"  //host stack
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "driver/ledc.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "string.h"

//#include "math.h"

//nvs 存储SSID
static  char Wifi_SSID[10] ;
static char Wifi_Pass[10];
bool is_nvs_cred_load_OK = false;
bool is_wifi_cred_test_OK = false;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num ;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_SSID_RENEW    BIT2
#define WIFI_PASS_RENEW    BIT3
#define THSD_NVS_NAME "storage"
#define THSD_SSID_KEY "SSID"
#define THSD_PASS_KEY "PASS"
#define THSD_SET_SSID  0x81     //ble 更新Wifi SSID 指令
#define THSD_SET_PASS  0x82     //ble 更新Wifi Pass 指令
#define THSD_SET_PWM1  0xFF     //ble 更新 PWM - Channel 1 指令

static uint8_t  pwm_out;        // 0 ~ 255
//#include "host/ble_uuid.h"

/*自己指定的，用于和手机（小程序）ble 通讯的UUID。按要求倒序排列 */
#define BLE_THSD_SERVICE_UUID BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x00, 0x10, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
//Service UUID: "91110106-0627-7460-1000-911101080019"
//#define BLE_THSD_IMCOMPLETE_SERVICE_UUID BLE_UUID32_DECLARE(0x19, 0x00, 0x08, 0x01)
#define BLE_THSD_CHAR_COMMAND_UUID    BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x01, 0x00, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
// 指令Charicater UUID: "91110106-0627-7460-0001-911101080019"  
#define BLE_THSD_CHAR_DATA_UUID   BLE_UUID128_DECLARE(0x19, 0x00, 0x08, 0x01, 0x11, 0x91, 0x02, 0x00, 0x60, 0x74, 0x27, 0x06, 0x06, 0x01, 0x11, 0x91)
// 数据Charicater "91110106-0627-7460-0002-911101080019"


/* 210214:尝试调试成最简单的ble Write功能，服务于手机微信小程序定期读取*/
#define DEVICE_NAME "QBDZ"  //注意，发现名字长度影响adv的fields 的set
//#define mfg_data_wqj "hello"


char formatted_ble_data[100];       //输出到小程序时，使用的组织成JSON格式的数据。
uint8_t wqj_ble_addr_type;          //flag。程序过程用，不用关心
void ble_app_advertise(void);       //编译用，提前声明，函数在调用函数的后面。
void ble_app_scan(void);
uint8_t Num_ble_connected = 0;

static void Wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {          // 由esp_wifi_start()触发。
        ESP_LOGI(__func__,"WIFI_EVENT_STA_START. To try connect to Wifi"); 
        esp_wifi_connect();      
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {  //由esp_wifi_connect()连接尝试失败触发。也可能由esp_wifi_stop()触发。
            ESP_LOGI(__func__,"WIFI_EVENT_STA_DISCONNECTED. "); 

            if (s_retry_num < 3) {                          //暂时设置3次重试 ：EXAMPLE_ESP_MAXIMUM_RETRY
            ESP_LOGI(__func__, "Retry connect to Wifi AP");
            int ret = esp_wifi_connect();
            s_retry_num++;            
            ESP_LOGW(__func__,"wifi_connect() return %d.",ret);
            } 
            //此处删除例程的xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); 用最大等待时间获得Wifi连接失败的判断
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {     //由esp_wifi_connect()连接尝试成功触发。
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(__func__, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 3;       //EXAMPLE_ESP_MAXIMUM_RETRY    //只要本次操作成功连接，就设置。目的是避免在执行Wifi_stop()的时候，重新触发WIFI_EVENT_STA_DISCONNECTED，再次尝试connect。
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    }    
}
/**程序bootup 的时候，先初始化必要的wifi设置，避免test_wifi_cred 期间，重复调用至错   */
void qb_wifi_general_init(){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default()); 
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
}
/* 测试 Wifi cred 是否能连接本地WiFi，将会在bootup 和Wifi_Pass 重置时调用，最后设置 is_wifi_cred_test_OK*/
int test_Wifi_Cred(){
    wifi_config_t wifi_config = {
        .sta = {}, //最少需要初始化到 .sta。因为这里面是个 union，必须确定是 .sta 还是 .ap 。
    };
    /*** 赋值 Wifi SSID 和 password。*/
    strncpy((char *)wifi_config.sta.ssid, (char *)Wifi_SSID, strlen(Wifi_SSID));
    strncpy((char *)wifi_config.sta.password, (char *) Wifi_Pass, strlen(Wifi_Pass));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    s_retry_num = 0;                        //连接失败时，尝试重连次数复位。
    ESP_ERROR_CHECK(esp_wifi_start() );  // luanch WIFI_EVENT_STA_START
    // 等待(WIFI_CONNECTED_BIT) or timeout（连接尝试失败）The bit is set by Wifi_event_handler() (see above) 
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT,         //取消  | WIFI_FAIL_BIT, 用timeout 来处理等待时间
            pdTRUE,                     //函数返回前本bit 复位，原例程为pdFALSE。否则影响下一次 wifi_aiot_start(void)
            pdFALSE,                    // Don't wait for both bits, either bit will do.
            pdMS_TO_TICKS(5000));
    if (bits & WIFI_CONNECTED_BIT) {            //在这里继续aiot_mqtt 的 init 和 Pub 操作,结束后关闭Wifi。
        ESP_LOGI(__func__, "connected to ap SSID:%s password:%s",Wifi_SSID, Wifi_Pass);
        ESP_ERROR_CHECK(esp_wifi_stop());
        return 0;
    } else {
        ESP_LOGE(__func__, "xEventGroupWaitBits timeout! Failed to connect to SSID:%s, password:%s.",Wifi_SSID, Wifi_Pass);
        ESP_ERROR_CHECK(esp_wifi_stop());
        return -1;
    }
}
/**程序bootup 以后，一直等待，如果有 WIFI_SSID_RENEW event时，执行nvs 重写，并且在WIFI_PASS_RENEW event时，测试本地Wifi是否能连接 */
void renew_Wifi_Cred(){
    nvs_handle_t nvsHandle;
    while(true){
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_PASS_RENEW | WIFI_SSID_RENEW, pdTRUE,pdFALSE,portMAX_DELAY);
    ESP_LOGI(__func__,"either SSID or password renewed. To renew nvs flash and do wifi connection test");
    ESP_ERROR_CHECK(nvs_open(THSD_NVS_NAME, NVS_READWRITE, &nvsHandle));
    if (bits & WIFI_SSID_RENEW){
        esp_err_t ret = nvs_set_str(nvsHandle, THSD_SSID_KEY, Wifi_SSID );
        if (ret == ESP_OK){
            ret = nvs_commit(nvsHandle);
            if(ret == ESP_OK){
                ESP_LOGI(__func__,"Wifi SSID nvs renewed and commit OK!");
            }else{ESP_LOGI(__func__,"Wifi SSID commit failed!");}
        }else{ESP_LOGI(__func__,"Wifi SSID nvs set failed!");}
    }else if(bits & WIFI_PASS_RENEW){
        esp_err_t ret = nvs_set_str(nvsHandle, THSD_PASS_KEY, Wifi_Pass );
        if (ret == ESP_OK){
            ret = nvs_commit(nvsHandle);
            if(ret == ESP_OK){
                ESP_LOGI(__func__,"Wifi SSID nvs renewed and commit OK!");
                ret = test_Wifi_Cred();                                     //测试Wifi credential
                if(ret == ESP_OK){
                    is_wifi_cred_test_OK = true;
                    ESP_LOGI(__func__,"Wifi connect test OK, correct creds saved to nvs!");
                }else{
                    is_wifi_cred_test_OK = false;
                    ESP_LOGW(__func__,"Wifi connect test failed, check SSID and password");
                    } 
            }else{ESP_LOGI(__func__,"Wifi Pass commit failed!");}
        }else{ESP_LOGI(__func__,"Wifi Pass nvs set failed!");}
    }
    nvs_close(nvsHandle);
    vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* cb 函数，当gat_svcs中的对应characteristics被Read的时候，执行 */
static int receive_client_command(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg){
    printf("receive command from client %.*s. \n", ctxt->om->om_len, ctxt->om->om_data);
    char buff[50];              //未来 #define 最大 buff 的数量，测试采用50
    sprintf(buff,"%.*s",ctxt->om->om_len, ctxt->om->om_data);
    if(buff[0]== THSD_SET_SSID ){
        ESP_LOGI(__func__,"to renew WIfi SSID in nvs!");
        for(int i= 0 ; i< strlen(buff); i++){
            Wifi_SSID[i] = buff[i+1];           //复制string 到 WIFI_SSID 时，前移一个byte，去掉控制位buff[0],最后一位是‘\0’
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_SSID_RENEW);
    }else if (buff[0]== THSD_SET_PASS ){
        ESP_LOGI(__func__,"to renew WIfi PASS in nvs!");
        for(int i= 0 ; i< strlen(buff); i++){
            Wifi_Pass[i] = buff[i+1];           
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_PASS_RENEW);
    }else if(buff[0]== THSD_SET_PWM1 ){
        ESP_LOGI(__func__,"to renew PWM1 ! %d,  %s", strlen(buff),buff);
        pwm_out = (uint8_t) (buff[1]);
    }    
//TODO: handle strings
    
    ESP_LOGI(__func__,"New pwm_out is: \t %d",pwm_out);
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
                .flags = BLE_GATT_CHR_F_READ ,//| BLE_GATT_CHR_F_READ_ENC,
                .access_cb = update_client_data,
            },{0}}
    },{0}     //表示Service list结束
};

/*  ble_gap_adv_start 时注册的不同 event 的cb 函数*/
static int ble_gap_event(struct ble_gap_event *event, void * arg){
//
    struct ble_hs_adv_fields fields; //for DISC

    switch (event->type) {
//
    case BLE_GAP_EVENT_DISC:
        ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        printf("discoverd device %.*s \n", fields.name_len,fields.name);
        if(fields.uuids128 != NULL){
        printf("%d \n", fields.num_uuids128);

        }

        
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI("GAP","BLE_GAP_EVENT_DISC_COMPLETE");
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI("GAP","restart scan");
        ble_app_scan();
        break;

    case BLE_GAP_EVENT_CONNECT:     //可以是“connected”或“有未遂连接尝试”
        ESP_LOGI("GAP","BLE_GAP_EVENT_CONNECT %s",event->connect.status == 0? "OK":"Failed");
        if(event->connect.status != 0){
            ble_app_advertise();
        }
        else if(Num_ble_connected < 3){ //允许最多同时连接3个，当连接数小于3时，继续advertise
            ble_app_advertise();
            Num_ble_connected++;
            printf("Num_ble_connected = %d.\n",Num_ble_connected);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
         ESP_LOGI("GAP","BLE_GAP_EVENT_DISCONNECT");
         Num_ble_connected--;
         printf("Num_ble_connected =%d.\n",Num_ble_connected);
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
/* scan*/
void ble_app_scan(void){
    struct  ble_gap_disc_params ble_gap_disc_params;
    ble_gap_disc_params.filter_duplicates = 1;
    ble_gap_disc_params.passive = 1;
    ble_gap_disc_params.itvl = 0;
    ble_gap_disc_params.window = 0;
    ble_gap_disc_params.filter_policy = 0;
    ble_gap_disc_params.limited = 0;
    ble_gap_disc(wqj_ble_addr_type,BLE_GAP_SCAN_SLOW_INTERVAL1,&ble_gap_disc_params,ble_gap_event, NULL);
}

/* 开始advertise所需要的设置 单独成函数是为了多次调用*/
void ble_app_advertise(void){
    ESP_LOGI(__func__,"advertise.");
    struct ble_gap_adv_params adv_params;   // = (struct ble_gap_adv_params){0};  //?新建adv的参数？
    struct ble_hs_adv_fields fields;
    memset(&fields,0, sizeof(fields) );
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_DISC_LTD;// | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = true;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;  //Power level
    fields.name = (uint8_t *) ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = true;         //说明name 是 Shortened Local Name or Complete Local Name
    /*以下三行增加adv时，包含UUID，目的是其它设备可以通过搜索UUID提高scan的速度，节约资源*/
    fields.uuids128 = (ble_uuid128_t *)BLE_THSD_SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = true;
    
    // fields.uuids32 = (ble_uuid32_t *)BLE_THSD_SERVICE_UUID;
    // fields.num_uuids32 = 1;
    // fields.uuids32_is_complete = false;
   /*可以通过 fields.mfg_data 设置进行adv 时，携带的数据，成功测试可以带32位 int 和 char  
    //char  mfg_data_wqj[10]; 或 
    int  mfg_data_wqj = 0x11223344;
    //sprintf(mfg_data_wqj,"wqj");
    fields.mfg_data = (uint8_t *)&mfg_data_wqj;
    fields.mfg_data_len = sizeof(mfg_data_wqj);
    */
    /* 注意：能包含在adv中的数据字节数为31，不能超过。*/
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0){
        ESP_LOGW(__func__,"set fields failed: 0x%4x",rc);
        return;
    }
    //以后增加adv data和rsp data for scan 》》》》》》》》》》》》》》》》》》没成功，以后试
    // int ble_gap_adv_set_data(const uint8_t *data, int data_len) Configures the data to include in subsequent advertisements.
    // int ble_gap_adv_rsp_set_data(const uint8_t *data, int data_len) Configures the data to include in subsequent scan responses.

    memset(&adv_params,0,sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(wqj_ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

void ble_app_on_sync(void){
    ESP_LOGI(__func__,"on sync.");
    /* Figure out address to use while advertising (no privacy for now) */
    ble_hs_id_infer_auto(0,&wqj_ble_addr_type);     //自动生成地址类型,不理解，但似乎不需要关心
    ble_app_advertise();
    //ble_app_scan();
    //这里的函数只会执行一次
}

/* nimble_port_freertos_init 出来的task*/
void host_task(void *param){
     /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();      //2
    nimble_port_freertos_deinit(); //这句应该不会执行到
}
/* 把ble 初始化所需步骤整合到一起，简化程序app_main()*/
void thsd_ble_init(){
    int rc;
    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());  //HCI: Host Controller Interface
    nimble_port_init();
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.sync_cb = ble_app_on_sync;  //3.  Bluetooth Host main configuration structure “host stack?"
    /*gatt_svr_init*/
      
    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(gat_svcs);
     if (rc != 0) {
        ESP_LOGW(__func__,"ble_gatts_count_cfg(gat_svcs) failed.");
    }
    rc = ble_gatts_add_svcs(gat_svcs);
     if (rc != 0) {
        ESP_LOGW(__func__,"ble_gatts_ass_svcs(gat_svcs) failed.");;
    }
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);
    /* XXX Need to have template for store 例程有参考，不理解，没采用。万一有奇怪问题查这里*/
    //ble_store_config_init();

    nimble_port_freertos_init(host_task);   //第一步1
}
/** 功能程序*/
static void sgpTask(void* pvParams) {
    while(true){    //在这里添加传感器处理后的数据，组成成JSON格式的 formatted_ble_data，当执行ble characteristic CB时，判断并赋值
                    //20 byte以内一次cb送完，无需担心，软件会根据需要自动多次cb送完数据。
        sprintf(formatted_ble_data,"{\"tVoC\":23.4,\"eCO2\":1200}");
        ESP_LOGI(__func__,"now, pwm_out is: %d %%. ",pwm_out);
        uint8_t pwm_out_map127 = (uint8_t) (pwm_out *1.27) ;
        ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,pwm_out_map127);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
            vTaskDelay(pdMS_TO_TICKS(20000));
    }
}
/** 输出PWM 的初始化*/
void pwm_setup(){
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_7_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    ledc_channel_config_t channel ={
        .gpio_num = 22,         // TODO：修改成 #define 的Pin number
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .hpoint = 0,
        .duty = 255,
    };
    ledc_channel_config(&channel);
    //ledc_fade_func_install(0);
}
/*** 从nvs 中提取Wifi credential，并且进行 WIfi 连接测试*/
void NVS_Storage_Retrieve(){
    nvs_handle nvsHandle;
    size_t  nvs_lenth = sizeof(Wifi_SSID);
    ESP_ERROR_CHECK(nvs_open(THSD_NVS_NAME, NVS_READWRITE, &nvsHandle));
    esp_err_t ret = nvs_get_str(nvsHandle,THSD_SSID_KEY,Wifi_SSID, &nvs_lenth);
    if(ret == ESP_OK){
        nvs_lenth = sizeof(Wifi_Pass);
        ret = nvs_get_str(nvsHandle,THSD_PASS_KEY,Wifi_Pass, &nvs_lenth);
        if (ret == ESP_OK )
        {
            ESP_LOGI(__func__,"nvs get Wifi redential OK: SSID: %s; PASS :%s",Wifi_SSID, Wifi_Pass);
            is_nvs_cred_load_OK = true;
        }
        else
        {
            ESP_LOGI(__func__, "Wifi Pass Load failed.");
        }      
    }else{ESP_LOGI(__func__,"Wifi SSID nvs load failed! ");
    }
    if (is_nvs_cred_load_OK){
        ESP_LOGI(__func__,"Start up wifi connection test.");
        ret = test_Wifi_Cred();         //测试Wifi credential
        if(ret == ESP_OK){
       is_wifi_cred_test_OK = true;
       ESP_LOGI(__func__,"Wifi connect test OK!");
        }else{ESP_LOGW(__func__,"Wifi setting does not work here. Renew SSID and password");} 
     }
    // ESP_LOGI(__func__,"nvs get str ret = %s",esp_err_to_name(ret));
    // ESP_LOGI(__func__,"got string :%s",Wifi_SSID);
    //  sprintf(Wifi_Pass,"wqj");
    //  ret = nvs_set_str(nvsHandle,THSD_PASS_KEY,Wifi_Pass);
    //  ESP_LOGI(__func__,"nvs set str ret = %s",esp_err_to_name(ret));
    //  ret = nvs_commit(nvsHandle);
    //  ESP_LOGI(__func__,"nvs commit  = %s",esp_err_to_name(ret));
     nvs_close(nvsHandle);
    
    //先看看nvs 教程再玩
}
void app_main(void)
{

    //      while (true)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(5000));/* code */
    // }

        s_wifi_event_group = xEventGroupCreate();

    
    /*参考例程增加nvs_flash_init()的可靠性*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    qb_wifi_general_init();
    NVS_Storage_Retrieve();
    pwm_setup();
/* ble 相关初始化和启动nimble_port_freertos*/
    thsd_ble_init();
/*功能程序*/
    xTaskCreate(sgpTask, "sgpTask", 4096, NULL, 5, NULL);
    xTaskCreate(renew_Wifi_Cred,"renew_Wifi_Cred", 1024 *8, NULL, 5, NULL);
}
