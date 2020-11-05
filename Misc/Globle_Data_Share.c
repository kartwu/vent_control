/*
这段程序运行时，在刚开始阶段发现第一次从TEMPR task转到HMI task 的时候，有Char Array 乱码。之后的运行不再有这个问题。以后有debug工具后研究。
I (0) TEMPR: ventMachine name from HMI is QBDZ-001
I (10) TEMPR: In_Air_Temperature is 25.000000
W (200) HMI: ventMachine name from XXXX is �
��G�?|H�?
W (200) HMI: In_Air_Temperature is 20.100000
*/
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
      ESP_LOGI(TAG,"ventMachine name from HMI is %s\n",myMachine->Name );
      strcpy(myMachine->Name, "wqjian");
      //ESP_LOGI(TAG,"ventMachine name is %s\n",myMachine->Name );
      ESP_LOGI(TAG, "In_Air_Temperature is %f\n", myMachine->In_Air_Temperature);
      myMachine->In_Air_Temperature = 20.1 ;
      vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
void HMI(void *params)
{
  Vent_machine * myMachine = (Vent_machine *) params;
    //从*params传递过来的是void *,新定义本task局部变量为pointer型，需要把 void * 型cast to 变量的type_define相同后，才能传递。
  uint32_t pointADD = (int )myMachine; //新定义变量，用于检查前一步声明的指针的里面指明的地址。
  ESP_LOGI("HMI"," myMachine adds is %x", pointADD); 
  while(true){
  ESP_LOGW("HMI","ventMachine name from XXXX is %s\n",myMachine->Name );
  strcpy(myMachine->Name, "QBDZ-001");
  ESP_LOGW("HMI","In_Air_Temperature is %f\n", myMachine->In_Air_Temperature);
  myMachine->In_Air_Temperature = 10.5 ;
  vTaskDelay(pdMS_TO_TICKS(3000));
  }
}
void app_main(void)
{

        int *ptr ;   // 声明一个指针
    int myNumber =3; //声明一个变量
    ptr = &myNumber;  //把变量的地址取出，赋值给指针变量，作为指针的地址
    ESP_LOGI(TAG,"*ptr = %d \n",*ptr); // log 指针变量的值
    ESP_LOGI(TAG,"Adds of *ptr is %x \n", (int)ptr); //LOG 指针变量的地址，需要 cast to “int”，才符合 “%x” 的形式要求。
    Vent_machine  ventMachine;          //声明struct
    uint32_t ventMachineAdds = (int) &ventMachine;  //新声明变量，取struct的地址，并cast to “int” 后，赋值给变量
    ESP_LOGI("Main","ventMachineAdd is %x. \n",ventMachineAdds); //log 变量
     Vent_machine  ventMachine;
    strcpy(ventMachine.Name, "QBDZ-001");
    ventMachine.Fan_Power_Control = 1;
    ventMachine.In_Air_Temperature =25;
    xTaskCreate(&temperatureMonitoring_control,"Temp_Meas",2048, &ventMachine,1,NULL);
    vTaskDelay(pdMS_TO_TICKS(200));
    xTaskCreate(&HMI,"HMI", 2048, &ventMachine, 1 , NULL );
}