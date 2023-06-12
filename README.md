# bt_exp

整个项目主要包括日常蓝牙测试的脚本和一些历史exp。

master分支包括

### Linux 脚本
- printAllBTInfo.py
    扫描打印目标设备所有蓝牙信息，包括广播信息、蓝牙协议栈信息、SDP接口信息、GATT接口信息。

- RfcommClientPoc.py
    输入uuid和mac，或port和mac，和目标rfcomm端口进行连接，通过目标设备是否弹窗来判断是否有应用层鉴权

### Windows
- sdp扫描
- 附件设备扫描

### obex应用

- 引用obex库进行pbap、map、hfp测试

### GattFuzz


### BTFuzz

引用braktooth项目进行bt测试


### 其他分支

编译方法参考btstack readme

- bypass pincode
    bypass pincode exp
    
- justwork
    几个justwork exp