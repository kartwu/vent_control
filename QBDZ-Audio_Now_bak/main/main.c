#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "driver/i2s.h"
#include "driver/adc.h"

#define TAG "ESP_NOW"
#define ESP_NOW_SENDER                    //如果是发送方，二选一
//#define ESP_NOW_RECEIVER                  //如果是接收方，二选一
#define ESP_NOW_PEER_READY_BIT  BIT4      //peer 匹配准备好
#define ESP_NOW_DATA_READY_BIT  BIT5      //数据准备好
#define ESP_NOW_RECEIVE_ACK_OK_BIT     BIT6      //发送函数返回成功
#define ESP_NOW_SEND_CB_ABNORMAL BIT7     //发送CB 发现异常，暂时未编程
xQueueHandle message_received_queue;
static EventGroupHandle_t ESP_NOW_Event_Group;
#define ESP_NOW_send_data_size (240)
#define ESP_NOW_Package_Number (100)
static char ESP_NOW_send_buff[ESP_NOW_send_data_size * ESP_NOW_Package_Number];          //测试用的发送数据,10K频率下发送2.4秒语音。
#define RESTING_SCALE (127)     //安静状态下的最小音量比例，大音量条件下，该比例会变大，起到自动增益的作用
//#define I2S_READ_data_size (240 * 150)                  //i2s 读取的数据的长度，可能要先读取Xs,再开始（分多个payload）传输这些数据，完成后才可以在读取后面信息

/*发送信息的种类 */
typedef enum message_type_t {
  Borad_cast,
  Data_Message,
  Voice_Message,
  Ack_OK,           //成功读取后，发送确认
  Ack_ERR           //表示收取有异常
}message_type_t;

/*发送信息 Payload 结构体,最大不超过 250 bytes. */
typedef struct  payload_t{
  char message[ESP_NOW_send_data_size];
  message_type_t message_type;
  uint16_t message_pack_num;        //一整段数据分段发送的总分段数量
  uint16_t message_pack_ID;         //每一个分段给予的ID号，收取后可用于核实。
}payload_t;

/*跟随payload 一起收到的信息*/
typedef struct payload_ext_t {
  payload_t payload;
  uint8_t mac_from[6];
}payload_ext_t;

/* 从指针 mac 中解析出 char 形式的字符，返回给指针 buffer*/
char *mac_to_str(char *buffer, uint8_t *mac)
{
  sprintf(buffer, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buffer;
}

/*发送的 callback. */
void on_ESP_NOW_sent(const uint8_t *mac_addr, esp_now_send_status_t status){
  char buffer[13];
  switch (status)
  {
  case ESP_NOW_SEND_SUCCESS:
    //ESP_LOGI(__func__, "message sent to %s", mac_to_str(buffer,(uint8_t *) mac_addr));
    break;
  case ESP_NOW_SEND_FAIL:     //如果出现发送失败，且非 broadcast，设置提醒 ESP_NOW_SEND_CB_ABNORMAL。
    ESP_LOGE(TAG, "message sent to %s failed", mac_to_str(buffer,(uint8_t *) mac_addr));
        if( strcmp(buffer,"ffffffffffff") != 0){
          xEventGroupSetBits(ESP_NOW_Event_Group, ESP_NOW_SEND_CB_ABNORMAL);
    }
    break;
  }
}

/*收到信息的 callback. */
void on_ESP_NOW_receive(const uint8_t *mac_addr, const uint8_t *data, int data_len){
  if(data_len != sizeof(payload_t)){                //验证payload长度合法性？有疑问
    //ESP_LOGE(__func__, "received incorrect payload");
    return;
  }
  //ESP_LOGI(__func__, "received correct payload");
  payload_ext_t payload_ext;
  payload_t *payload = (payload_t *) data;
  payload_ext.payload = *payload;
  memcpy(payload_ext.mac_from, mac_addr, 6);
  if(payload_ext.payload.message_type == Borad_cast || payload_ext.payload.message_type == Voice_Message || payload_ext.payload.message_type == Data_Message){
    xQueueSend(message_received_queue,&payload_ext,0);
  }
  if( payload_ext.payload.message_type == Ack_OK){      //可能需要增加 Ack 的ID 是最近一次发送的 ID。
    xEventGroupSetBits(ESP_NOW_Event_Group,ESP_NOW_RECEIVE_ACK_OK_BIT);
  }
}

/*广播 cb，发送后给所有 ESP_Now peer，接收方凭借payload 的字节数量，判断是否收到“合法” payload，
    接收方再提取 payload.message_type ，去判断广播的peer是否已经是配对，如果没有就配对。
    应该还可以添加其它判断是否自动配对的条件（比如payload.message比较）。*/
void broadcast_cb(TimerHandle_t xTimer){
  uint8_t broadcast_address[6];
  memset(broadcast_address,0xff,6);
  payload_t payload ={
    .message = "QBDZ_ping",
    .message_type = Borad_cast
  };
  esp_now_send(broadcast_address,(uint8_t *) &payload, sizeof(payload_t));
}
// /*12位压缩成8位，目的是只发送8位，收到后再处理*/
// void i2s_adc_data_scale(uint8_t * d_buff, uint8_t* s_buff, uint32_t len)
// {
//     uint32_t j = 0;
//     uint32_t dac_value = 0;
//     for (int i = 0; i < len ; i += 2) {
//         dac_value = ((((uint16_t) (s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
//         //dac_value = ((((uint16_t)(s_buff[i + 3] & 0xff) << 8) | ((s_buff[i + 2]))));
//         d_buff[j++] = 0;
//         d_buff[j++] = dac_value * 256 / 4096;
//     }
// }

/*发送信息 task, 数据只发送给配对的peer。以后改成Touch 控制。*/
  void send_ESP_NOW_message_task(void *params){
    esp_now_peer_num_t peer_num;
    payload_ext_t payload_ext;
    while (true){
        // 等待 voice_message_setup Task吧数据准备好
        xEventGroupWaitBits(ESP_NOW_Event_Group, ESP_NOW_DATA_READY_BIT,true,true,portMAX_DELAY);
        //如果是 Voice_Message。
        payload_ext.payload.message_type = Voice_Message;
        payload_ext.payload.message_pack_num = sizeof(ESP_NOW_send_buff) / ESP_NOW_send_data_size ;
        payload_ext.payload.message_pack_ID = 0 ;
        //把有效数据发送完，（有数据需要发送,即还有剩余报没有发完）
        while(payload_ext.payload.message_pack_ID <payload_ext.payload.message_pack_num) { 
              esp_now_get_peer_num(&peer_num);
              //装载数据：把分段的数据赋值给 payload.message 和 ID。
              for( int i =0 ; i < sizeof(payload_ext.payload.message); i++){
//稍后在这里调节音量。再装载到payload 等待发送。
                  payload_ext.payload.message[i] = ESP_NOW_send_buff[ESP_NOW_send_data_size * payload_ext.payload.message_pack_ID + i];
                }
              if(peer_num.total_num <= 1){
                ESP_LOGW(__func__,"No valid peer yet.");
              }
              else{
                if (esp_now_send(NULL, (uint8_t*)&payload_ext.payload, sizeof(payload_t)) != ESP_OK){
                  ESP_LOGW(__func__,"esp_now_send failed.");
                }
              }
              payload_ext.payload.message_pack_ID ++;
               //等待接收数据方，发送回的 .message_type = Ack_OK 的数据，再继续发送下一条数据，保证数据完整。否则按UDP方式，测试结果是Queue等操作后，数据不完整。必须在cb里面直接 i2s_write。
              xEventGroupWaitBits(ESP_NOW_Event_Group, ESP_NOW_RECEIVE_ACK_OK_BIT,true, true, portMAX_DELAY);// pdMS_TO_TICKS(10)
            }
            //一组数据发送完成，把payload的参数复位。
        payload_ext.payload.message_pack_ID = 0;
        payload_ext.payload.message_pack_num = 0;
        //disp_buf((uint8_t*)payload_ext.payload.message,480);
      }
    }

void disp_buf(uint8_t* buf, int length)
{
    printf("======\n");
    for (int i = 0; i < length; i++) {
        printf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("======\n");
}

/*收取信息并更新 Queue 后的信息后续处理 Task。*/
void message_received_task(void *params){
  // int task_param = (int ) params;
  // ESP_LOGI(__func__," task Param : %d ", task_param);
  char buffer[13];
  size_t byte_writen = 0;
  payload_ext_t payload_ext;
  esp_err_t ret =0;
  static char ESP_NOW_RECEIVE_BUFF[240 * 100];  //最大的单批接收数据存储容量
  while (true){
      xQueueReceive(message_received_queue, &payload_ext, portMAX_DELAY);
      //ESP_LOGI(__func__," Queue received");
      switch (payload_ext.payload.message_type) {
          case Borad_cast:                                        //如果是Borad_cast, 判断并决定是否加 peer.
              //如果已经 peer_exist，则直接 set ESP_NOW_PEER_READY_BIT，否则等到第一个add_peer 成功后 set ESP_NOW_PEER_READY_BIT。
              //之后开通 voice_message_setup 流程。
            if(!esp_now_is_peer_exist(payload_ext.mac_from)){
              ESP_LOGI(__func__," find new peer from %s", mac_to_str(buffer, payload_ext.mac_from));
              esp_now_peer_info_t peer;
              memset(&peer, 0, sizeof(esp_now_peer_info_t));
              memcpy(peer.peer_addr, payload_ext.mac_from, 6);
              /*增加 Encription 的方法，添加local master key, 并设置 Encrypt = true。*/
              memcpy(peer.lmk,"qbdzkey",8);
              peer.encrypt =true;
               ret = esp_now_add_peer(&peer);
              if (ret == ESP_OK){
                xEventGroupSetBits(ESP_NOW_Event_Group, ESP_NOW_PEER_READY_BIT);
              }
              else{
                ESP_LOGW(__func__,"Add peer resturn %s.", esp_err_to_name(ret));
              }              
            }
            else{
              ESP_LOGI(__func__,"Peer already exist! broadcast message: %s", payload_ext.payload.message);
              xEventGroupSetBits(ESP_NOW_Event_Group, ESP_NOW_PEER_READY_BIT);
            }    
            break;
          case Data_Message:
          // TODO data manage here
          //ESP_LOGI(__func__,"received send_message: %s ",payload_ext.payload.message);
          //disp_buf((uint8_t*)payload_ext.payload.message,64);
          break;
          case Voice_Message:  //如果是 V_M, 则不止一条信息，每收到一条信息,回复一条信息，包含 .message_type = Ack_OK。
          payload_ext.payload.message_type = Ack_OK;
          ret = esp_now_send(NULL, (uint8_t *) &payload_ext.payload, sizeof(payload_t));
          //ESP_LOGW(__func__,"sent ack return %s.", esp_err_to_name(ret));
          //把这段信息存入总 ESP_NOW_RECEIVE_BUFF
          for(int i =0; i < ESP_NOW_send_data_size ; i++){
            ESP_NOW_RECEIVE_BUFF[i + payload_ext.payload.message_pack_ID * ESP_NOW_send_data_size] = payload_ext.payload.message[i]; 
          }
          //ESP_LOGI(__func__, " pack_Num %d, ID Num %d.", payload_ext.payload.message_pack_num, payload_ext.payload.message_pack_ID);
          //当收取信息确认是最后一条时，开始向DAC输出
          if(payload_ext.payload.message_pack_num == (payload_ext.payload.message_pack_ID +1)){
            // ESP_LOGI(__func__,"data received complete! Playing...");
            i2s_start(I2S_NUM_0);
            int i2s_write_len = ESP_NOW_send_data_size * 2;
            uint8_t * i2s_write_buffer = (uint8_t *) calloc( (i2s_write_len),sizeof(char));
            for(int i =0; i < payload_ext.payload.message_pack_num; i++){
                for(int k = 0; k < ESP_NOW_send_data_size; k++){
                i2s_write_buffer[k * 2] = 0;            //输出DAC是16位，LSB被放弃，展低8位0。
                i2s_write_buffer[k * 2 +1] = ESP_NOW_RECEIVE_BUFF[i * ESP_NOW_send_data_size + k];  //MSB才是有效数，会被输出
                }
                ret = i2s_write(I2S_NUM_0, i2s_write_buffer, i2s_write_len, &byte_writen, portMAX_DELAY);
                if(ret != ESP_OK){ESP_LOGW(__func__,"i2s DAC write return %s.", esp_err_to_name(ret));}
            }
            i2s_stop(I2S_NUM_0);
            free(i2s_write_buffer);
          }
          break;
          default:
            break;
      }
  }
}

/* Wifi 初始化以后，才可以初始化 ESP_NOW。*/
static void wifi_init(void){
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  //Wifi Long Range setting
  ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N) );//|WIFI_PROTOCOL_LR
}



/*数据准备 task。 准备好就set_bits 来提示其它 send_ESP_NOW_message_task 把准备好的数据发送，直到完成。 */
void voice_message_setup(void *params){
      esp_err_t ret =0;
      size_t bytes_read;
      int32_t scale = RESTING_SCALE; //音量比例初始值
//     //等待检查 有合格的 ESP_NOW_PEER_READY_BIT 确保有发送对象，开始本 task。
     xEventGroupWaitBits(ESP_NOW_Event_Group, ESP_NOW_PEER_READY_BIT, true, true, portMAX_DELAY);
    /*组织 INMP441 的测试初始化。*/
     static const i2s_config_t i2s_config_1 = {
        .mode = (I2S_MODE_MASTER  | I2S_MODE_RX),
        .sample_rate = 10000,
        .bits_per_sample = 32 ,                  // INMP441 要求
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                 //先调试用
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 2,     
        .dma_buf_len = 100,
        .use_apll = false
    };
    static const i2s_pin_config_t pin_config_1 = {
        // INMP441 连线方式
        .bck_io_num = 18,
        .ws_io_num = 19,
        .data_in_num = 23,
        //TTGO-T-Camera 板上 mic,好像只有把 channel_format 设定为 RIGH_LEFT 才能用，牵涉包含i2s_read_len的变化，以后有空再试。
        // .bck_io_num = 14,
        // .ws_io_num = 32,
        // .data_in_num = 33,
        // .data_out_num = -1,
    }; 
    ret = i2s_driver_install(I2S_NUM_1, &i2s_config_1, 0 ,NULL);
     ESP_LOGI(__func__,"i2s driver install %s ..", esp_err_to_name(ret));
    ret = i2s_set_pin(I2S_NUM_1, &pin_config_1);
    ESP_LOGI(__func__,"i2s pin set %s ..", esp_err_to_name(ret));
    i2s_zero_dma_buffer(I2S_NUM_1);
    //启动阶段数据
    int i2s_read_len = ESP_NOW_send_data_size * 4; //Mic是32位，发送的是8位，所以读取数 * 4。
    int8_t* i2s_read_buffer = (int8_t *) calloc( (i2s_read_len),sizeof(char));  // 注意是 int8_t。
    ret = i2s_read( I2S_NUM_1, i2s_read_buffer, i2s_read_len, &bytes_read,portMAX_DELAY);
    ret = i2s_read( I2S_NUM_1, i2s_read_buffer, i2s_read_len, &bytes_read,portMAX_DELAY);
    while (true){
      ESP_LOGI(__func__,"start recording ..."); 
           //录音 ESP_NOW_Package_Number 遍，汇总成 ESP_NOW_send_buff
      for( int i = 0; i < ESP_NOW_Package_Number; i++){
            ret = i2s_read( I2S_NUM_1, i2s_read_buffer, i2s_read_len, &bytes_read,portMAX_DELAY);
            if ( ret != ESP_OK){ESP_LOGW(__func__,"i2s0_read %s.", esp_err_to_name(ret));}            
            //disp_buf((uint8_t *)i2s_read_buffer,64);
            for(int k = 0; k < (ESP_NOW_send_data_size) ; k++){
              int8_t temp = i2s_read_buffer[ k * 4 +3];   //取MSB
              temp += 0x80;                               //变成 uint_8
              ESP_NOW_send_buff[ ESP_NOW_send_data_size * i + k] = temp; //存储
            }            
      }  
      //disp_buf((uint8_t *)ESP_NOW_send_buff,64);
        ESP_LOGI(__func__,"Sending buffer ready ...");      
            //  int16_t buffer16[ESP_NOW_send_data_size] = {0};
            //  for(int j = 0; j < ESP_NOW_send_data_size; j++){
            //     uint8_t mid = buffer32[j *4 + 2];                
            //     uint8_t msb = buffer32[j *4 + 3];
            //     uint16_t raw = (((uint32_t)msb << 8) + (uint16_t)mid);                //printf("%x \t", raw);
            //     memcpy(&buffer16[j], &raw, sizeof(raw));            //printf("%x %d \t", (uint16_t)buffer16[j], buffer16[j] );
            // }
            // //调节音量
            // int16_t max = 0;
            // for( int j =0; j < ESP_NOW_send_data_size; j++){
            //     int16_t val = buffer16[j];
            //     if(val < 0){ val = -val;}
            //     if(val > max){max = val;}
            // }
            // if(max > scale){scale = max;}
            // if(max < scale && scale > RESTING_SCALE) {scale -= 300;}
            // if(scale < RESTING_SCALE) {scale = RESTING_SCALE;}
            // //变成发送的8位数据（int8）,接收方收到后需要变成(uint8),才能DAC
            // int8_t buffer8[ESP_NOW_send_data_size] ={0};  //调整到 for 外面？？
            // for (int j =0; j < ESP_NOW_send_data_size; j ++){
            //     int32_t scaled = ((int32_t)buffer16[j]) * 127 / scale;
            //             if (scaled <= -127) {buffer8[j] = -127;}
            //             else if (scaled >= 127) {buffer8[j] = 127;} 
            //             else {buffer8[j] = scaled;}
            // }
            // //把相应段录音打包到发送包
            // for(int j = 0; j < ESP_NOW_send_data_size; j ++){
            //     ESP_NOW_send_buff[i * ESP_NOW_send_data_size +j] = buffer8[j];
            // }
      xEventGroupSetBits(ESP_NOW_Event_Group, ESP_NOW_DATA_READY_BIT);
      //延时重发以后需要换成button 或 touch 来触发。
      vTaskDelay(pdMS_TO_TICKS(20000));  //测试过，如果采用一边录制，一遍发送。接收端一遍收取，一遍播放，可以不要delay，不会触发WD。
    }    
}

void dac_setup(){
       static const i2s_config_t i2s_config_0 = {
        .mode = (I2S_MODE_MASTER  | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = 10000,
        .bits_per_sample = 16 ,                  //只有MSB 8 位有效
        .intr_alloc_flags = 0,
        .dma_buf_count = 2,     
        .dma_buf_len = 100,  //??
        .use_apll = false
    };
     i2s_driver_install(I2S_NUM_0, &i2s_config_0, 0 ,NULL);
    //init DAC pad
     i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN); //I2S_DAC_CHANNEL_BOTH_EN
     i2s_set_clk(I2S_NUM_0, 10000, 16, 1); //没有这个设置，貌似速度快一倍。慢慢研究。
}

void app_main(void)
{ 

  ESP_NOW_Event_Group = xEventGroupCreate();
  // nvs flash init
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init();
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_send_cb(on_ESP_NOW_sent));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(on_ESP_NOW_receive));

  /*先要 Add broadcast peer information to peer list. */
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  memset(peer.peer_addr, 0xff, ESP_NOW_ETH_ALEN);
  esp_now_add_peer(&peer);
  
  message_received_queue = xQueueCreate(3, sizeof(payload_ext_t));          //及时做queuereceive,就可以不用多个 queue.
  TimerHandle_t timer_handle = xTimerCreate("boardcast_Timer", pdMS_TO_TICKS(10000),true, NULL, broadcast_cb);    //每60s 广播一次。
  xTimerStart(timer_handle,0);
  #if defined(ESP_NOW_RECEIVER)
  dac_setup(); //, "dac_setup", 1024 * 2, NULL, 4, NULL);
  //xTaskCreate()
  #endif //  ESP_NOWRECEIVER

  xTaskCreate(message_received_task, "message_received_task", 1024 *4,  NULL, 3, NULL);
  
  #if defined(ESP_NOW_SENDER)
  xTaskCreate(voice_message_setup, "voice_message_setup", 1024*4, NULL, 4 ,NULL);
  xTaskCreate(send_ESP_NOW_message_task, "send_ESP_NOW_message_task", 1024*4,NULL,5,NULL);
  #endif // ESP_NOW_SENDER

  
}
