const app = getApp()
/***这一堆是控制需要的变量定义 */
const ctx = wx.createCanvasContext('myCanvas')
var A_x = 80
var A_y = 30
var B_x = 150
var B_y = 100
var C_x = 10
var C_y = 300
var D_x = 250
var D_y = 350
var touch_Count = 0
var old_pwm_out_L =0
var old_pwm_out_R = 0
var pwm_out_L = 0
var pwm_out_R = 0

/***从index.js 复制必要的连接程序 */
var readDataTimerID = 0       //定时器 ID，用于关闭定时器
var control_ble_Timer_ID = 0    //用于开启实时控制时，启动定时器，定期write 指令到 prepheral
var waitDiscoveryTimeOutID = 0  //用于 discover 阶段控制超时
Page({
  data: {
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
    is_touch_control_enabled: false,
  },

    
  onLoad(){
    ctx.setFillStyle('red')
    ctx.fillRect(A_x,A_y,B_x-A_x,B_y-A_y)
    ctx.setFillStyle('#FAEBD7')
    ctx.fillRect(A_x-150,A_y+90,250,100)
    
    const grd = ctx.createLinearGradient(C_x,C_y,D_x-C_x,D_y-C_y)
    grd.addColorStop(0.5,'red')
    grd.addColorStop(0,'blue')
    ctx.setFillStyle(grd)
    ctx.fillRect(C_x,C_y,D_x-C_x,D_y-C_y)
    ctx.setStrokeStyle('green')
    ctx.setLineDash([10,20],5)
    ctx.moveTo(C_x,C_y - 50)
    ctx.lineTo(D_x,C_y -50)
    ctx.moveTo(D_x,D_y + 50)
    ctx.lineTo(C_x,D_y +50)
    ctx.stroke()
    ctx.rotate(90* Math.PI /180)
    ctx.setFontSize(30)
    ctx.setFillStyle('black')
    ctx.fillText('激活',30,-180)
    ctx.fillText('设置速度和转向',220,-280)
    ctx.setFontSize(20)
    ctx.fillText('输出',150,-200)
    ctx.draw()
    ctx.rotate(-90* Math.PI /180)
  },
  /***开始touch */
  start (e) {
    touch_Count++
    if(e.touches[0].x > A_x && e.touches[0].x < B_x && e.touches[0].y >A_y && e.touches[0].y < B_y){
      console.log('valid touch! To set active')
      this.setData({is_touch_control_enabled:true})
      ctx.setFillStyle('green')
      ctx.fillRect(A_x,A_y,B_x-A_x,B_y-A_y)
      ctx.draw(true)
      console.log(touch_Count)
    }
  },
  /***停止 touch */
  end (e) {
    touch_Count--
    if(e.changedTouches[0].identifier == 0){
      if(this.data.is_touch_control_enabled){
      console.log('valid touch lost! To set inactive')
      this.setData({is_touch_control_enabled:false})
      ctx.setFillStyle('red')
      ctx.fillRect(A_x,A_y,B_x-A_x,B_y-A_y)
      ctx.draw(true)
    }
    }
    if(e.changedTouches[0].identifier == 1){
      if(old_pwm_out_L != 0 || old_pwm_out_R != 0){
        ctx.setFillStyle('#FAEBD7')
        ctx.fillRect(C_x,C_y - 175,old_pwm_out_L,5)
        ctx.fillRect(C_x,C_y - 85, old_pwm_out_R,5)
        ctx.draw(true)
        old_pwm_out_L = 0
        old_pwm_out_R = 0
        pwm_out_R = 0
        pwm_out_L = 0
        console.log('output turned off')//第二个touch 无效，关闭输出，关闭本地显示
    }
    }
  },
  /*** 移动touch 过程中 */
  move (e) {
     if (this.data.is_touch_control_enabled){

      if(e.touches[0].x > A_x && e.touches[0].x < B_x && e.touches[0].y >A_y && e.touches[0].y < B_y){  //确保enable保持

        if(e.touches[1] != undefined){                                                //发现有第二个touch
          if(e.touches[1].y > C_y - 50 && e.touches[1].y < D_y + 50 && e.touches[1].x < 150){                 //确保touch的范围正确
            pwm_out_L = e.touches[1].x - (e.touches[1].y -(C_y+25))
            pwm_out_R = e.touches[1].x + (e.touches[1].y -(C_y+25)) 
            //var temp = Math.abs((old_pwm_out - pwm_out_R))
            if(Math.abs(pwm_out_L-old_pwm_out_L) > 10 || Math.abs(pwm_out_R-old_pwm_out_R) > 10 ){     
              this.qbdz_ble_control_send(pwm_out_L,pwm_out_R)
              ctx.setFillStyle('#FAEBD7')
              ctx.fillRect(C_x,C_y - 175,old_pwm_out_L,5)
              ctx.fillRect(C_x,C_y - 85, old_pwm_out_R,5)
              ctx.setFillStyle('green')
              ctx.fillRect(C_x,C_y - 175,pwm_out_L,5)
              ctx.fillRect(C_x,C_y - 85,pwm_out_R,5)
              ctx.draw(true)
            old_pwm_out_L = pwm_out_L
            old_pwm_out_R = pwm_out_R

          }
          }
          else if(old_pwm_out_L != 0 || old_pwm_out_R != 0){
            ctx.setFillStyle('#FAEBD7')
            ctx.fillRect(C_x,C_y - 175,old_pwm_out_L,5)
            ctx.fillRect(C_x,C_y - 85, old_pwm_out_R,5)
            ctx.draw(true)
              old_pwm_out_L = 0
              old_pwm_out_R = 0
              pwm_out_R = 0
              pwm_out_L = 0
              console.log('output turned off')//第二个touch 无效，关闭输出，关闭本地显示
          }
        } 
      }else{
        console.log('hold touch lost! To set inactive')
        this.setData({is_touch_control_enabled:false})
        ctx.setFillStyle('red')
        ctx.fillRect(A_x,A_y,B_x-A_x,B_y-A_y)
        ctx.draw(true)
      }
  }
  },
  onUnload(){
    
    wx.redirectTo({
      url: '../index/index',
    })
  },
  onShow(){
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
  thsd_ble_On_Off:function(){
    console.log("f")
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
/*** 如果找到制定SERVICE UUID的device，到这里*/
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

    }
  })
},
/*** 被循环调用的发送 ble characteristic的指令 */
qbdz_ble_control_send: function(data_L,data_R){
  console.log("alarm_setting to be send is ",data_L,data_R)
  var array_ToSend = new Uint8Array(3)
  array_ToSend[0] =0xFF                       //实时控制指令标志
  array_ToSend[1] = (data_L/2) //this.data.alarm_Setting   //数据
  array_ToSend[2] = (data_R/2)
  var buffer = array_ToSend.buffer     //貌似转换成可多种解读的buffer 后，才能发送
  console.log(buffer)
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
})
