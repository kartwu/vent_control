

//20200717: 修改搜索device 时，只要找到以“THSD-CK”开头的DeviceName 设备，就连接，并更新DeviceID 和 DeviceName

//20200630：基本调通BLE 部分和 MQTT阿里云上报部分。存档备注备份。 使用阿里云API未遂，先不玩它了。

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

Page({
  data: {
    // BLE Data
    thsd_ServiceUUID:"91110106-0627-7460-1000-911101080019",        //自定义专用 UUID
    thsd_CharacteristicId:"91110106-0627-7460-0001-911101080019",   //自定义专用 UUID
    thsd_DeviceId: '',              //每个不同的设备的 DeviceID 不一样，程序运行中读取并使用
    thsd_DeviceName: 'THSD-CK',     //每个不同设备的 DeviceName 都设成一样的，共程序搜索时判断是否相关设备
    textToEsp32: 'Hello World!',    // 手机向 ESP32 发送的数据
    textFromEsp32: '',              // 手机接收到的 ESP32 发送来的数据
    is_ble_connected: false,            //BLE 连接后为 True。通过这个连接状态，选择UI 中的显示不同内容
    data_Received:false,            // BLE 有数据接收到 为 true
    // Wechat display data
    objectTemperature: 0,
    ambientTemperature: 0,
    referenceTemperature:0,
    relativeHumidity: 0,
    airPressure: 0,
    dewPoint:0,
    apparentTemperature:0,
    current_AC: 0,
    votage_AC:0,
    speed: 0,
    force: 0,
    //......

    //  MQTT 参数  
    is_MQTT_Connected:false,        //MQTT上线后 为 true。通过这个连接状态，选择UI 中的显示不同内容  
    aly_upload_count: 0            // 手机通过 MQTT 向阿里云发送数据量统计
  },

  onShow: function() {
    //console.log(this.myGetPostData(this.data.object_Temperature_Name, this.data.objectTemperature))
  },

  //待开发：按照选择的数据，执行阿里云数据上报

  // myGetPostData( MyName , value){
  // const payloadJson = {
  //   id: Date.now(),
  //   params: {
  //     MyName : value,
  //     2:    MyName
  //   },
  //   method: "thing.event.property.post"
  // }
  // return JSON.stringify(payloadJson);
  // },

  onLoad: function() {
    wx.onBLEConnectionStateChange((res) => {
      var that = this
      if (!res.connected) {
        console.log('No connected BLE device', res)
        wx.showModal({
          title: '提示：',
          content: '丢失BLE连接！',
          success (res) {
            if (res.confirm) {
              that.setData({ is_ble_connected: false })        //BLE 连接成功进行辅助显示
              that.setData({data_Received: false})
              that._discoveryStarted = false                //标记本参数False，为了下次重新搜索的
              wx.closeBluetoothAdapter()
              console.log('用户点击确定')
            } else if (res.cancel) {
              console.log('用户点击取消')
            }
          }
        })
      }
      else if (res.connected){
        console.log('BLE 已连接')
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
    //services:this.data.thsd_ServiceUUID, //专门查找本UUID的BLE,不成功，可能是专业 UUID 才行。
    allowDuplicatesKey: false,     // true 允许多次发现会重复调用 wx.onBluetoothDeviceFound。经测试没有意义
    success: (res) => {
      wx.showToast({
        title: '开始寻找',
        icon: 'success',
        duration: 500
      })
      this.onBluetoothDeviceFound()
    },
    fail:(res) => {
    console.log(res)
    }
  })
},
onBluetoothDeviceFound() {
  wx.onBluetoothDeviceFound((res) => {
      res.devices.forEach(device => {
      if (device.name.search("THSD-CK") === 0) {         //检查BLE 设备是不是以 “THSD-CK”开头，如果是，确认为正确设备。
        console.log("find THSD-CK Device",res)
        this.data.thsd_DeviceId = device.deviceId       //每个不同的Device 可能有不同的DeviceID，但是会有相同的DeviceName 和 UUIDs。所以只确认DeviceName后， 记录Device ID给后面的 API 调用。
        console.log("THSD-CK Device ID & Name saved",this.data.thsd_DeviceId,this.data.thsd_DeviceName)
        this.setData({thsd_DeviceName:device.name })    //更新确切设备名称
        this.stopBluetoothDevicesDiscovery()
        this.createBLEConnection(this.data.thsd_DeviceId)
      }
      
    })
  })
},

stopBluetoothDevicesDiscovery(){
  wx.stopBluetoothDevicesDiscovery()
},

createBLEConnection(deviceId) {
  var that = this
  const serviceId = this.data.thsd_ServiceUUID
  const characteristicId = this.data.thsd_CharacteristicId
    wx.createBLEConnection({
    deviceId,
    success: (res) => {
      console.log('createBLEConnection success', res)     
      this.setData({ is_ble_connected: true })        //BLE 连接成功进行辅助显示
      wx.showToast({
        title: '已连接 ',
        icon: 'success',
        duration: 500
      })
      /*
      通常（原例程）操作，需要先getservice,再getCharactoristic，再NotifyBLE...。因本程序所支持的BLE device 已经配属专用 Service UUID 和 Characteristic UUID，故免去查询，直接调用下面的 Notify API。
      */
      wx.notifyBLECharacteristicValueChange({       //启用设备特征值变化时的 notify 功能
        deviceId,
        serviceId,
        characteristicId,
        state: true,                              //启用 notify
        complete: function (res) {
          wx.showToast({
            title: '启用通知',
            icon: 'success',
            duration: 500
          })
        }                                             //当前能运行的程序显示不成功，因为无 descriptor。但是notify还能正常接收。以后研究。
        
      })
      wx.onBLECharacteristicValueChange(function (res) {    //监听启用设备特诊变化
        var newReceiveText = app.buf2string(res.value)
        that.setData({data_Received: true})
        console.log('接收到数据：' + newReceiveText)
        //console.log('thsdParseInput : ',this.thsdParseInput(newReceiveText))  
        that.setData({
            textFromEsp32:newReceiveText
        })
        that.thsdParseInput(newReceiveText)
      })
    }
  })
},
//发送特定characteristic的指令，在本Page不需要，代码转移到Manual_Test page 中。


///////////////////////////// BLE 接收到数据解析，以后继续开发时，可以做成 Jason 数据格式，更通用。

thsdParseInput: function(inputString){
  let myNewStringArr = inputString.split(",")
  for(let i = 0; i < myNewStringArr.length; i++) {
    //解析目标温度   TO = Temperature Object
    if(myNewStringArr[i].startsWith("TO") && myNewStringArr[i].endsWith("OT")) {
      let tempTemperature = parseFloat(myNewStringArr[i].slice(2).replace(/OT/g, ""))
      this.setData({objectTemperature : tempTemperature})
    } 
    //解析环境温度   TA = Temperature Ambient
    else if(myNewStringArr[i].startsWith("TA") && myNewStringArr[i].endsWith("AT")) {
      let tempTemperature = parseFloat(myNewStringArr[i].slice(2).replace(/AT/g, ""))
      this.setData({ambientTemperature : tempTemperature})
    } 
    //解析环境温度   TR = Temperature Reference
    else if(myNewStringArr[i].startsWith("TR") && myNewStringArr[i].endsWith("RT")) {
      let tempTemperature = parseFloat(myNewStringArr[i].slice(2).replace(/RT/g, ""))
      this.setData({referenceTemperature : tempTemperature})
    } 
    // 解析相对湿度  HA = Humidity Relative
    else if(myNewStringArr[i].startsWith("HR") && myNewStringArr[i].endsWith("RH")) {
      let tempHumidity = parseFloat(myNewStringArr[i].slice(2).replace(/RH/g, ""))
      this.setData({relativeHumidity : tempHumidity})
    } 
    // 解析空气压力 PA = Pressure Air
    else if(myNewStringArr[i].startsWith("PA") && myNewStringArr[i].endsWith("AP")) {
      let tempPressure = parseFloat(myNewStringArr[i].slice(2).replace(/AP/g, ""))
      this.setData({airPressure : tempPressure})
    } 
    // 解析露点 DP = Dew Point
    else if(myNewStringArr[i].startsWith("DP") && myNewStringArr[i].endsWith("PD")) {
      let tempDewPoint = parseFloat(myNewStringArr[i].slice(2).replace(/PD/g, ""))
      this.setData({dewPoint : tempDewPoint})
    } 
    // 解析体感温度apparentTemperature HI = Heat Index
    else if(myNewStringArr[i].startsWith("HI") && myNewStringArr[i].endsWith("IH")) {
      let tempapparentTemperature = parseFloat(myNewStringArr[i].slice(2).replace(/IH/g, ""))
      this.setData({apparentTemperature : tempapparentTemperature})
    } 
  }
  return "complete"
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

  doConnect() {
    var that = this;
    const options = this.initMqttOptions(deviceConfig);

    console.log(options)
    client = mqtt.connect('wxs://productKey.iot-as-mqtt.cn-shanghai.aliyuncs.com', options)
    client.on('connect', function () {
      console.log('连接服务器成功')
      wx.showToast({
        title: '上线成功',
        icon:"none",
        duration:500
      })
      that.setData({
       is_MQTT_Connected:true 
      })
    })
  },
  //IoT平台mqtt连接参数初始化
  initMqttOptions(deviceConfig) {
    const params = {
      productKey: deviceConfig.productKey,
      deviceName: deviceConfig.deviceName,
      timestamp: Date.now(),
      clientId: Math.random().toString(36).substr(2),
    }
    //CONNECT参数
    const options = {
      keepalive: 60, //60s
      clean: true, //cleanSession不保持持久会话
      protocolVersion: 4 //MQTT v3.1.1
    }
    //1.生成clientId，username，password
    options.password = this.signHmacSha1(params, deviceConfig.deviceSecret);
    options.clientId = `${params.clientId}|securemode=2,signmethod=hmacsha1,timestamp=${params.timestamp}|`;
    options.username = `${params.deviceName}&${params.productKey}`;

    return options;
  },
  signHmacSha1(params, deviceSecret) {
    let keys = Object.keys(params).sort();
    // 按字典序排序
    keys = keys.sort();
    const list = [];
    keys.map((key) => {
      list.push(`${key}${params[key]}`);
    });
    const contentStr = list.join('');
    return crypto.hex_hmac_sha1(deviceSecret, contentStr);
  },

  /////上报数据
  thsd_MQTT_Post:function(){
    var that = this;
    //如果已经“上线”，且 有有效数据（非0），上报数据
    if(that.data.is_MQTT_Connected&& that.data.objectTemperature != 0) {
      let topic = `/sys/${deviceConfig.productKey}/${deviceConfig.deviceName}/thing/event/property/post`;
      // 注意用`符号，不是' ！！！！！
      let JSONdata = this.getPostData()
      console.log("===postData\n topic=" + topic)
      console.log("payload=" + JSONdata)
      client.publish(topic, JSONdata)
      let temp_Count = this.data.aly_upload_count + 1;
      this.setData({aly_upload_count:temp_Count})
    }
    else{
      wx.showToast({
        title: '离线或无有效数据',
        icon:"none",
        duration:500
      })
    }
  },

  ///////这部分程序仅上报三项指标，以后开发再按照合理逻辑，逐步添加其它指标
   getPostData: function() {
    const payloadJson = {
      id: Date.now(),
      params: {
        Object_Temperature: this.data.objectTemperature,   
        Air_Pressure: this.data.airPressure,
        Relative_Humidity: this.data.relativeHumidity        
      },
      method: "thing.event.property.post"
    }
    return JSON.stringify(payloadJson);
  },

/////////////////用户点击“帮助”后，跳转到帮助信息页面
onTouchHelp:function(event){
  console.log(event);
  wx.navigateTo({
    url: '../help/help',
  })
},
////////////////////用户点击“手动”后，跳转到手动测量功能页面
onTouchManual:function(event){
  console.log(event);
  wx.navigateTo({
    url: '../manual_Test/manual_Test',
  })
}

  
})

