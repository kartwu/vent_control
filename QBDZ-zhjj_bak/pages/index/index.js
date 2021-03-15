
const app = getApp()
const util = require('../../utils/util.js')             //获取时间日期等标准常用函数
var mqtt = require('../../utils/mqtt.min.js')           // MQTT 库
const crypto = require('../../utils/hex_hmac_sha1.js')  //MQTT 库需要的SHA算法
var client                                              //MQTT 实例
const deviceConfig = {
  productKey: "a1m81zhi11W",  //同和测控-云数据的三元组
  deviceName: "THSD_Data",
  deviceSecret: "32kSDyw5lXLbpFsIgHai8NgSCl7RwwGV",
  regionId: "cn-shanghai"
};


var readDataTimerID = 0       //定时器 ID，用于关闭定时器
var control_ble_Timer_ID = 0    //用于开启实时控制时，启动定时器，定期write 指令到 prepheral
var waitDiscoveryTimeOutID = 0  //用于 discover 阶段控制超时

/*** 开始读取数据后，开启timer，对应的回调函数，发起ble read 指令（有可能可以放在 page 里面） */
function read_BLE_Data(deviceID,serviceId,characteristicId){    
  wx.readBLECharacteristicValue({
    deviceId: deviceID,
    serviceId: serviceId,
    characteristicId:characteristicId,
    success(res){
      console.log('readBLECharacteristicValue:', res.errCode)
    },
    complete(res){
      console.log('readBLECharacteristicValue:', res)
    }
  })
}





Page({

  data: {
      input_Wifi_SSID:null,
      input_Wifi_PassWord:null,
      input_info:null,

      alarm_Setting: 0,       //从滑条取出的设置值，是发送到 prepheral 的参数
      Device_Type: [ 'YLX-B','QBDZ','可选设备','设备间'],
      selected_Device_Type_Name: 'YLX-B',
      // BLE Data
      THSD_SERVICE_UUID:"91110106-0627-7460-1000-911101080019",        //自定义专用 UUID
      THSD_CHAR_DATA_UUID:"91110106-0627-7460-0002-911101080019",   //自定义专用 UUID
      THSD_CHAR_COMMAND_UUID:"91110106-0627-7460-0001-911101080019",
      thsd_DeviceId: '',                        //每个不同的设备的 DeviceID 不一样，程序运行中读取并使用
      //thsd_DeviceName: '输入指令内容',     //每个不同设备的 DeviceName 都设成一样的，共程序搜索时判断是否相关设备
      textToEsp32: 'Hello World!',              // 手机向 ESP32 发送的数据
      textFromEsp32: '',                        // 手机接收到的 ESP32 发送来的数据
      is_ble_connected: false,                  //BLE 连接后为 True。通过这个连接状态，选择UI 中的显示不同内容
      is_ble_target_device_confirmed: false,    //找到目标设备标签，用于后续程序逻辑
      is_Start_Data_Read:false,                 // BLE 有数据接收到 为 true，用于控制 UI 的按钮“读取特征数据” 
      is_Start_Realtime_Control: false,         // BLE 开始实时控制 为 true，用于控制 UI 的按钮“开始实时控制”
      
      //按object组织数据
      //最多每次测量处理五个参数显示、保存等
       display_Result:                 //最多每次测量处理三个参数显示、保存等
       [{  name:"",                    //名称，和阿里云产品中属性对应
           value:"",                   //数值
           unit:"",
           ALY_Property_Name:""                     //数值的单位
        },{ name:"",                    //名称，和阿里云产品中属性对应
            value:"",                   //数值
            unit:"" ,
            ALY_Property_Name:""                    //数值的单位
        },{ name:"",                    //名称，和阿里云产品中属性对应
            value:"",                   //数值
            unit:"",
            ALY_Property_Name:""                     //数值的单位
        },{ name:"",                    //名称，和阿里云产品中属性对应
            value:"",                   //数值
            unit:"",
            ALY_Property_Name:""                     //数值的单位
        },{ name:"",                    //名称，和阿里云产品中属性对应
            value:"",                   //数值
            unit:"",
            ALY_Property_Name:""                     //数值的单位
         }],
    //  MQTT 参数  
    is_MQTT_Connected:false,            //MQTT上线后 为 true。通过这个连接状态，选择UI 中的显示不同内容  
    aly_upload_count: 0                 // 手机通过 MQTT 向阿里云发送数据量统计
  },

  onShow: function () {
    
  },
  /***切换到Canvas 控制页 */
  canvas_Control:function(e){
    wx.redirectTo({
      url: '../control/control',
    })
  },
  /***获取对ble设备更新的新 Wifi SSID */
    bind_Input_Info:function(e){
      this.setData({
        input_info: e.detail.value
      })
    },
  /***获取对ble设备更新的新 Wifi SSID */
  bind_Input_SSID:function(e){
    this.setData({
      input_Wifi_SSID: e.detail.value
    })
  },
  /***获取对ble设备更新的新 Wifi Password */
  bind_Input_Pass:function(e){
    this.setData({
      input_Wifi_PassWord: e.detail.value
    })
  },
/*** 滑条对应取值 */
  sliderchanging:function(e){
    this.data.alarm_Setting = e.detail.value
    console.log(this.data.alarm_Setting)
  },
/***选择设备的 picker 对应取值 */
  bindpickerChange: function (e) {
    const val = e.detail.value
    this.setData({
      selected_Device_Type_Name: this.data.Device_Type[val[0]]
    })
    //this.data.selected_Device_Type_Name = this.data.Device_Type[val[0]]
    console.log(this.data.selected_Device_Type_Name)
  },
/***开始（停止）读取数据切换 */
  toggleReadBLEData:function(){
    if(this.data.is_ble_connected){
      if (!this.data.is_Start_Data_Read){
        this.startReadBLEData()
      }
      else
      this.stopReadBLEData()  
    }
    
  },
/***开始（停止）实时控制切换 */
  toggle_Control_BLE: function(){
    if(this.data.is_ble_connected){
      if(!this.data.is_Start_Realtime_Control){
        this.startControl()
      }
      else
      this.stopControl()
    }
  },
/***开始实时控制 */
  startControl:function(){
    console.log("start real time control")
    control_ble_Timer_ID = setInterval(this.qbdz_ble_control_send, 300)
    console.log(control_ble_Timer_ID)
    this.setData({is_Start_Realtime_Control:true})
  },
/***停止实时控制 */
  stopControl:function(){
    console.log("timer off",control_ble_Timer_ID)
    clearInterval(control_ble_Timer_ID)
    this.setData({is_Start_Realtime_Control:false})
  },
/***开始读取数据 */
  startReadBLEData: function(){                    //点击启动定时器，执行回调函数，读取BLE数据
    console.log("start Read")
    const deviceID = this.data.thsd_DeviceId
    const serviceId = this.data.THSD_SERVICE_UUID
    const characteristicId = this.data.THSD_CHAR_DATA_UUID
    console.log("device info:",deviceID,serviceId,characteristicId);
    readDataTimerID = setInterval(read_BLE_Data,1000,deviceID,serviceId,characteristicId);  
    //设置定时器（回调函数，时间，传递到回调函数的参数
    //readDataTimerID = setInterval(test_Interval,500); 
    console.log(readDataTimerID)
    this.setData({is_Start_Data_Read: true})
  },
/***停止读取数据 */
  stopReadBLEData: function(){
    console.log("Timer off ",readDataTimerID)
    clearInterval(readDataTimerID)
    this.setData({is_Start_Data_Read: false})
  },
/***搜索（断开）ble 设备切换 */
  thsd_ble_On_Off:function(){
    if(this.data.is_ble_connected){
      if(this.data.is_Start_Data_Read){
        this.stopReadBLEData();
      }
      if(this.data.is_Start_Realtime_Control){
        this.stopControl();
      }
      this.closeBluetoothAdapter();
    }
    else{
      this.openBluetoothAdapter();
    }
  },
/***只要 onload, 就监听ble状态是否有变化，并执行 */
  onLoad: function (options) {
    wx.onBLEConnectionStateChange((res) => {
      var that = this
      if (!res.connected) {
        console.log('BLE device disconnected', res)        
        that.setData({ is_ble_connected: false })        //BLE 连接成功进行辅助显示
        //that.setData({is_Start_Data_Read: false})
        that._discoveryStarted = false                //标记本参数False，为了下次重新搜索的
        wx.showToast({
        title: '连接关闭',
        duration: 2000
        })
        wx.closeBluetoothAdapter()
      }
      else if (res.connected){
        console.log('BLE 已连接')
        wx.showToast({
          title: '已连接',
          duration: 2000
        })
      }
    })
  },
////////////////////////  BLE program
closeBluetoothAdapter(){
  wx.closeBluetoothAdapter()         //关闭蓝牙，会触发 onLoad: 中wx.onBLEConnectionStateChange() 执行相应设置参数动作
},
openBluetoothAdapter() {
  wx.openBluetoothAdapter({
    mode:'peripheral',
    success: (res) => {
      console.log('openBluetoothAdapter success', res)
      wx.showToast({
        title: 'BLE打开 ',
        icon: 'success',
        duration: 500
      })
      this.startBluetoothDevicesDiscovery()
    },
    fail: (res) => {
      if (res.errCode === 10001) {
        console.log('openBluetoothAdapter failed 10001', res)
        wx.showModal({
          title: '提示：',
          content: '请打开手机蓝牙！',
        })
      }
    }
  })
},
startBluetoothDevicesDiscovery() {
  if (this._discoveryStarted) {
    return
  }
  this._discoveryStarted = true
  wx.startBluetoothDevicesDiscovery({
    //services:[this.data.THSD_SERVICE_UUID], //为节约资源和调试方便，只查找本小程序指定的UUID的BLE。
    allowDuplicatesKey: false,     // true 允许多次发现会重复调用 wx.onBluetoothDeviceFound。经测试没有意义
    success: (res) => {
      console.log("start bT discovery OK.",res)
      wx.showToast({
        title: '寻找设备...',
        icon: 'loading',
        duration: 2000
      })
      waitDiscoveryTimeOutID = setTimeout(this.DiscoveryTimeout,2000)
      console.log("timeout ID",waitDiscoveryTimeOutID)
      this.onBluetoothDeviceFound()
    },
    fail:(res) => {
    console.log(res)
    this.stopBluetoothDevicesDiscovery()
    this.closeBluetoothAdapter()
    }
  })
},
/*** 如果找到device（指定SERVICE UUID的要求因 set fields最多31 bytes 不够用，暂时取消），到这里*/
onBluetoothDeviceFound() {      
  wx.onBluetoothDeviceFound((res) => { 
    console.log("discovery get device.")
    clearTimeout(waitDiscoveryTimeOutID)         //终止执行 timeout
      res.devices.forEach(device => {            //确认是否是目标device名称
      if (device.name.search(this.data.selected_Device_Type_Name) === 0) {         //检查BLE 设备名称，如果是，确认为正确设备。
        console.log("find target Device",res)
        this.data.thsd_DeviceId = device.deviceId       //每个不同的Device 可能有不同的DeviceID，但是会有相同的DeviceName 和 UUIDs。所以只确认DeviceName后， 记录Device ID给后面的 API 调用。
        console.log("Device ID & Name saved",this.data.thsd_DeviceId,this.data.selected_Device_Type_Name)
        this.setData({selected_Device_Type_Name:device.name })    //更新确切设备名称
        this.data.is_ble_target_device_confirmed = true 
      }
      else{
        console.log("no match device found!")
        this.data.is_ble_target_device_confirmed = false
      }
    })
    if(this.data.is_ble_target_device_confirmed){
      console.log("to connect ble device!")
      this.stopBluetoothDevicesDiscovery()
      this._discoveryStarted = false
      this.createBLEConnection(this.data.thsd_DeviceId)      
    }
    else{
      console.log(" to stop BLE adapter.")
      this.stopBluetoothDevicesDiscovery()
      this._discoveryStarted = false
      this.closeBluetoothAdapter()
      wx.showToast({
        title: '未发现设备',
        icon: 'loading',
        duration: 2000
      })
    }
  })  
},
/*** discovery 超时时，恢复到搜索 ble 前的重新状态 */
DiscoveryTimeout:function(){
  console.log("Discovery timeout.")
  wx.showToast({
    title: '没找到设备',
    icon: 'loading',
    duration: 3000
  })
  //clearTimeout(waitDiscoveryTimeOutID)
  this.stopBluetoothDevicesDiscovery()
  this._discoveryStarted = false
  this.closeBluetoothAdapter()
  },

stopBluetoothDevicesDiscovery(){
  wx.stopBluetoothDevicesDiscovery({
    success (res) {
      console.log(res)
    }
  })
  
},

createBLEConnection(deviceId) {
  var that = this
    wx.createBLEConnection({
    deviceId,
    success: (res) => {
      console.log('createBLEConnection success', res)     
      this.setData({ is_ble_connected: true })        //BLE 连接成功进行辅助显示
      wx.showToast({
        title: '已连接',
        icon: 'success',
        duration: 2000
      })
/*** 连接后，监听特征值变化。用定时器的wxwx.readBLECharacteristicValue({})来触发 */
      wx.onBLECharacteristicValueChange(function (res) {    //监听启用设备特诊变化
        var newReceiveText = app.buf2string(res.value)
        //that.setData({is_Start_Data_Read: true})
        console.log('接收到数据：' + newReceiveText)
        //console.log('thsdParseInput : ',this.thsdParseInput(newReceiveText))  
        that.setData({
            textFromEsp32:newReceiveText
        })
        that.thsdParseInput(newReceiveText)     //调试期间先停止功能
      })
    }
  })
},
/***解读read 后收到的Json 格式数据，准备显示和初步阿里云 payload 格式 */
thsdParseInput: function(newReceivedText){
    //解析接收 JSON 格式的数据，满足标签条件，对应显示变量中赋值
    var i = 0                                                       //用于找到每一个合法标志时，给display_Result 的数组做 index
    var temp_display_Result = this.data.display_Result
    JSON.parse(newReceivedText,function(key,value){

      if (key == "TO"){
        temp_display_Result[i].name = "目标温度"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = '°C'
        temp_display_Result[i].ALY_Property_Name = 'Temperature_Object'
        i ++
      }
      if (key == "TA"){
        temp_display_Result[i].name = "环境温度"
        temp_display_Result[i].value = value
        temp_display_Result[i].unitHR = '°C'
        temp_display_Result[i].ALY_Property_Name = 'Temperature_Ambient'
        i ++
      }
      if (key == "HR"){
        temp_display_Result[i].name = "湿度"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = '%'
        temp_display_Result[i].ALY_Property_Name = 'RelativeHumidity'
        i ++
      }
      if (key == "PA"){
        temp_display_Result[i].name = "气压"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = 'HPa'
        temp_display_Result[i].ALY_Property_Name = 'Pressure_Air'
        i ++
      }
      if (key == "DP"){
        temp_display_Result[i].name = "露点"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = '°C'
        temp_display_Result[i].ALY_Property_Name = 'DewPoint'
        i ++
      }
      if (key == "HI"){
        temp_display_Result[i].name = "体感温度"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = '°C'
        temp_display_Result[i].ALY_Property_Name = 'ApparentTemperature'
        i ++
      }
      if (key == "CM"){
        temp_display_Result[i].name = "电流"
        temp_display_Result[i].value = value 
        temp_display_Result[i].unit = 'mA'
        temp_display_Result[i].ALY_Property_Name = 'Current_Mini_Amper'
        i ++
        } 
      if (key == "PW"){
        temp_display_Result[i].name = "功率"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = 'mW'
        temp_display_Result[i].ALY_Property_Name = 'Power_Mini_Watt'
        i ++
      }
      if (key == "TR"){
        temp_display_Result[i].name = "参考温度"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = '°C'
        temp_display_Result[i].ALY_Property_Name = 'ReferTemperature'
        i ++
      }
      if (key == "tVoC"){
        temp_display_Result[i].name = "挥发气体"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = 'PPB'
        temp_display_Result[i].ALY_Property_Name = 'tVoC'//待修改
        i ++
      }      
      if (key == "eCO2"){
        temp_display_Result[i].name = "二氧化碳"
        temp_display_Result[i].value = value
        temp_display_Result[i].unit = 'PPM'
        temp_display_Result[i].ALY_Property_Name = 'eCO2'//待修改OK
        i ++
      }
      //可以继续添加其它
   })
    console.log(temp_display_Result)
    this.setData({display_Result:temp_display_Result})


},
/***发送 ble characteristic的指令,更新Wifi cred */
qbdz_ble_send_wifi:function(){
  console.log("to try write ble characeristc WIFI cred.")
  if(this.data.input_Wifi_SSID != null){
  if(this.data.input_Wifi_SSID.length >= 3 && this.data.is_ble_connected ){
    var text_ToSend = this.data.input_Wifi_SSID;
    var buffer = new ArrayBuffer(text_ToSend.length + 2);
    let text_ToSend_Uint8_view = new Uint8Array(buffer);
    for (let i =0; i < text_ToSend.length ; i++){
      text_ToSend_Uint8_view[i+1] = text_ToSend.charCodeAt(i);
    }
    text_ToSend_Uint8_view[0] = 0x81;
    wx.writeBLECharacteristicValue({
      deviceId:this.data.thsd_DeviceId,
      serviceId:this.data.THSD_SERVICE_UUID,
      characteristicId:this.data.THSD_CHAR_COMMAND_UUID,
      value: buffer,
      success (res) {
        console.log('writeBLECharacteristicValue success', res.errMsg)
      },
      fail (res){
        console.log('writeBLECharacteristicValue success', res.errMsg)
      }
    })
  }
  }
  if(this.data.input_Wifi_PassWord != null){
  if(this.data.input_Wifi_PassWord.length >= 3 && this.data.is_ble_connected ){
    var text_ToSend = this.data.input_Wifi_PassWord;
    var buffer = new ArrayBuffer(text_ToSend.length + 2);
    let text_ToSend_Uint8_view = new Uint8Array(buffer);
    for (let i =0; i < text_ToSend.length ; i++){
      text_ToSend_Uint8_view[i+1] = text_ToSend.charCodeAt(i);
    }
    text_ToSend_Uint8_view[0] = 0x82;
    wx.writeBLECharacteristicValue({
      deviceId:this.data.thsd_DeviceId,
      serviceId:this.data.THSD_SERVICE_UUID,
      characteristicId:this.data.THSD_CHAR_COMMAND_UUID,
      value: buffer,
      success (res) {
        console.log('writeBLECharacteristicValue success', res.errMsg)
      },
      fail (res){
        console.log('writeBLECharacteristicValue success', res.errMsg)
      }
    })
  }
  }
},
/***发送 ble characteristic的信息指令,单次发送指令 */
qbdz_ble_send_info:function(){
console.log("to try write ble characeristc info")
if(this.data.input_info != null){
if(this.data.input_info.length > 0 && this.data.is_ble_connected ){
  var text_ToSend = this.data.input_info;
  var buffer = new ArrayBuffer(text_ToSend.length + 1);
  let text_ToSend_Uint8_view = new Uint8Array(buffer);
  for (let i =0; i < text_ToSend.length ; i++){
    text_ToSend_Uint8_view[i] = text_ToSend.charCodeAt(i);
  }
  //text_ToSend_Uint8_view[0] = 0x81;
  wx.writeBLECharacteristicValue({
    deviceId:this.data.thsd_DeviceId,
    serviceId:this.data.THSD_SERVICE_UUID,
    characteristicId:this.data.THSD_CHAR_COMMAND_UUID,
    value: buffer,
    success (res) {
      console.log('writeBLECharacteristicValue success', res.errMsg)
    },
    fail (res){
      console.log('writeBLECharacteristicValue success', res.errMsg)
    }
  })
}
}
},
/*** 被循环调用的发送 ble characteristic的指令 */
qbdz_ble_control_send: function(){
  console.log("alarm_setting is ",this.data.alarm_Setting)
  var array_ToSend = new Uint8Array(2)
  array_ToSend[0] =0xFF                       //实时控制指令标志
  array_ToSend[1] = this.data.alarm_Setting   //数据
  var buffer = array_ToSend.buffer     //貌似转换成可多种解读的buffer 后，才能发送
  wx.writeBLECharacteristicValue({
    deviceId:this.data.thsd_DeviceId,
    serviceId:this.data.THSD_SERVICE_UUID,
    characteristicId:this.data.THSD_CHAR_COMMAND_UUID,
    value: buffer,
    success (res) {
      console.log('writeBLECharacteristicValue success', res.errMsg)
    },
    fail (res){
      console.log('writeBLECharacteristicValue success', res.errMsg)
    }
  })
},
onTouchHelp:function(event){
  console.log(event);
  wx.showToast({
    title: '请联系开发者',
    icon: 'loading',
    duration: 2000
  })
  // wx.navigateTo({
  //   url: '../help/help',
  // })
},

//////////// MQTT 阿里云 程序的成熟部分。 待调试部分总是放在程序开头部分

 ///////上下线
 thsd_mqtt_On_Off_Line:function(){                   
  if(this.data.is_MQTT_Connected){
    client.end()  // 关闭连接
    console.log('服务器连接断开')
    wx.showToast({
      title: '下线成功',
      icon:"none",
      duration:500
    })
    this.setData({
      is_MQTT_Connected:false 
    })
  }
  else{
    this.doConnect()
  }
  
},

//MQTT 建立连接

  /*
    生成基于HmacSha1的password
    参考文档：https://help.aliyun.com/document_detail/73742.html?#h2-url-1
  */

//  doConnect() {
//   var that = this;
//   const options = this.initMqttOptions(deviceConfig);

//   console.log(options)
//   client = mqtt.connect('wxs://productKey.iot-as-mqtt.cn-shanghai.aliyuncs.com', options)
//   client.on('connect', function () {
//     console.log('连接服务器成功')
//     wx.showToast({
//       title: '上线成功',
//       icon:"none",
//       duration:500
//     })
//     that.setData({
//      is_MQTT_Connected:true 
//     })
//   })
// },

  //IoT平台mqtt连接参数初始化
  // initMqttOptions(deviceConfig) {
  //   const params = {
  //     productKey: deviceConfig.productKey,
  //     deviceName: deviceConfig.deviceName,
  //     timestamp: Date.now(),
  //     clientId: Math.random().toString(36).substr(2),
  //   }
  //   //CONNECT参数
  //   const options = {
  //     keepalive: 60, //60s
  //     clean: true, //cleanSession不保持持久会话
  //     protocolVersion: 4 //MQTT v3.1.1
  //   }
  //   //1.生成clientId，username，password
  //   options.password = this.signHmacSha1(params, deviceConfig.deviceSecret);
  //   options.clientId = `${params.clientId}|securemode=2,signmethod=hmacsha1,timestamp=${params.timestamp}|`;
  //   options.username = `${params.deviceName}&${params.productKey}`;

  //   return options;
  // },
  // signHmacSha1(params, deviceSecret) {
  //   let keys = Object.keys(params).sort();
  //   // 按字典序排序
  //   keys = keys.sort();
  //   const list = [];
  //   keys.map((key) => {
  //     list.push(`${key}${params[key]}`);
  //   });
  //   const contentStr = list.join('');
  //   return crypto.hex_hmac_sha1(deviceSecret, contentStr);
  // },

/////上报数据
// thsd_MQTT_Post:function(){
//   var that = this;
//   //如果已经“上线”，循环检查显示数据的数组，如果数组中 value 不是初始值 “”，则判断为该数据有效。按阿里云格式组织数据，并上报数据

//   if(that.data.is_MQTT_Connected) {

//     for(let i =0; i< this.data.display_Result.length; i++ ){
//         if(this.data.display_Result[i].value != "" ){
//           console.log(this.data.display_Result[i].value)
//         let topic = `/sys/${deviceConfig.productKey}/${deviceConfig.deviceName}/thing/event/property/post`;
//         // 注意用`符号，不是' ！！！！！
//         let JSONdata = this.getPostData(i)
//         console.log("===postData\n topic=" + topic)
//         console.log("payload=" + JSONdata)
//         client.publish(topic, JSONdata)
//         let temp_Count = this.data.aly_upload_count + 1;
//         this.setData({aly_upload_count:temp_Count})
//         }
//       }
//     }
//   else{
//     wx.showToast({
//       title: '离线或无有效数据',
//       icon:"none",
//       duration:500
//     })
//   }
// },

///////这部分程序仅上报三项指标，以后开发再按照合理逻辑，逐步添加其它指标
//  getPostData: function(index) {
//   var payload_Params = `{"${this.data.display_Result[index].ALY_Property_Name}":${this.data.display_Result[index].value}}`
//   var payload_Params_JSON = JSON.parse(payload_Params)
//   const payloadJson = {
//     id: Date.now(),
//     params: payload_Params_JSON,
//     method: "thing.event.property.post"
//   }
//   return JSON.stringify(payloadJson);
// },


  /**
   * 生命周期函数--监听页面初次渲染完成
   */
  onReady: function () {

  },

  /**
   * 生命周期函数--监听页面显示
   */
 

  /**
   * 生命周期函数--监听页面隐藏
   */
  onHide: function () {

  },

  /**
   * 生命周期函数--监听页面卸载
   */
  onUnload: function () {

  },

  /**
   * 页面相关事件处理函数--监听用户下拉动作
   */
  onPullDownRefresh: function () {

  },

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {

  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {

  }
})
/*
onLoad:function(){
  var filtered_x =0 
  var sample_Sum = 0
  var sample_Number = 5
  var x =[] 
   console.log(x)
   for (let i =0; i< sample_Number; i++){
      x[i]=0
    }
  //   console.log(x) 
  wx.startAccelerometer({
    interval: 'normal',
    success: (res) => {},
    fail: (res) => {},
    complete: (res) => {},
  })
  wx.onAccelerometerChange((result) => {
    sample_Sum += result.x 
    sample_Sum -= x[0] 
    filtered_x = sample_Sum /sample_Number
    console.log(filtered_x)
    for (let i =0; i< (sample_Number-1); i++){
      x[i]=x[i+1]
    }
    x[(sample_Number-1)]= result.x

     
     //console.log(result.x)
    }) */