# p2p穿透逻辑服务

### 约定
1. udp类型的服务数据包不超过512字节，内容为json，json内容键值只允许字符串。
2. tcp类型的服务使用sjsonb协议（不要在意名字这种细节），协议格式[magic number(4) | 协议版本(4) | 内容偏移(4) | 内容长度(4) | 校验码(4) | 可选扩展区(n) | 内容]，目前内容格式为json，json内容键值只允许字符串。


### 穿透流程
1. 客户端登录获取穿透从服务的地址，分别向这两个从服务发送指定数据包，以便服务器分析出客户端NAT类型并缓存。
2. 当需要做穿透时，调用getlinkinfo接口，获取本地与对端的穿透相关信息，然后根据各自的NAT类型决策如何穿透（或者使用talkto接口协商）。


### 接口列表(`！！！接口参数名称暂用，随时可能修改！！！`)
* 登录授权（暂用uid或机器人id）<br>
接口名称: login<br>
参数: auth<br>

* 注销登录
接口名称: login<br>
参数: auth<br>

* 消息转发
接口名称: talkto<br>
参数1: auth<br>
参数2: peerauth<br>
参数3: message<br>

* 获取穿透链路信息
接口名称: getlinkinfo<br>
参数1: auth<br>
参数2: peerauth<br>


### 接口返回值
示例： <br>
```
{"errno": "200", "errmsg": "success"}
{"errno": "200", "retinue_vice": "192.168.1.2", "errmsg": "success", "retinue_master": "192.168.1.1"}
```
