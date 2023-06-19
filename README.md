# bt_exp

整个项目主要包括日常蓝牙测试的脚本和一些历史exp。

master分支包括

### Linux 脚本

    `printAllBTInfo.py`     扫描打印目标设备所有蓝牙信息，包括广播信息、蓝牙协议栈信息、SDP接口信息、GATT接口信息。

    `RfcommClientPoc.py`    输入uuid和mac，或port和mac，和目标rfcomm端口进行连接，通过目标设备是否弹窗来判断是否有应用层鉴权

#### 一些常用命令

    `sudo hcitool scan`     扫描附近的经典蓝牙设备
    `sudo hcitool lescan`    扫描附近BLE设备
    `sudo sdptool browse addr`    查看目标设备addr的sdp服务信息

### Windows

- sdp扫描
- 附件设备扫描

### obex应用

    测试目标：蓝牙pbap客户端/服务器、map服务器、FTP服务器、OPP服务器、HFP设备

    - 引用obex库进行pbap、map、hfp测试
    `python3 examples/pbapclient.py addr path` 拉取目标设备addr的通讯录内容到path路径下
    `python3 examples/mapclient.py addr path` 拉取目标设备addr的MAP信息内容到path路径下
    其他功能参考obex readme.md

### GattFuzz

    测试目标：BLE设备

    目录下运行main.py使用，可以实现对目标设备Gatt接口进行Fuzz输入，支持两种变异方式：
    1. 基于历史pcap的payload变异。把历史pcap包拖到目录下，输入地址进行变异。
    2. 随机变异。没有历史数据包时，支持随机变异。

### BTFuzz

引用braktooth项目进行bt测试


### 其他分支

编译方法参考btstack readme

- bypass pincode
    bypass pincode exp
    
- justwork
    几个justwork exp