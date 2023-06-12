from bluepy.btle import Peripheral,DefaultDelegate,Scanner
import os
import argparse
import bluetooth

# linux环境 

'''
connect to target device and print all info

v9.22 修改ble信息打印，添加ble adv信息扫描，直连打印gatt接口信息
'''

import functools
from concurrent import futures

executor = futures.ThreadPoolExecutor(1)

def timeout(seconds):
    def decorator(func):
        @functools.wraps(func)
        def wrapper(*args, **kw):
            future = executor.submit(func, *args, **kw)
            return future.result(timeout=seconds)
        return wrapper
    return decorator

parser = argparse.ArgumentParser()
parser.add_argument('-m', '--mac', help='mac address of target', required=True)
parser.add_argument('-a', '--all', help='print all BT info', action='store_true')
parser.add_argument('-i', '--info', help='print all adv info', action='store_true')
parser.add_argument('-s', '--sdp', help='print sdp info', action='store_true')
parser.add_argument('-b', '--ble', help='print ble info', action='store_true')

args = parser.parse_args()
target_mac = args.mac
if args.sdp:
    info_type = 'sdp'
elif args.ble:
    info_type = 'ble'
elif args.info:
    info_type = 'info'
else:
    info_type = 'all'

class ReceiveDelegate(DefaultDelegate):
    def __init__(self):
        super().__init__()
    
    def handleNotification(self, cHandle, data):
        print("handle: " + cHandle + "nofity" "----> ", data)

class BLE_control(): 
    def __init__(self):
        self._conn = None

    # connect to target mac
    def tar_con(self, tar_mac):
        # print(" Begin scan:")
        scanner = Scanner()
        devices = scanner.scan(timeout=10)
        for dev in devices:
            # print("发现设备：", dev.addr)
            if dev.addr==tar_mac:
                print("发现目标BLE设备:"+ tar_mac)
                # print(dev)
                # print("%-30s %-20s" % (dev.getValueText(9), dev.addr)) 
                print("               ---------广播信息------------             ")
                for (adtype, desc, value) in dev.getScanData():
                    print("%s = %s" % (desc, value))
                print("               ---------广播信息------------             ")
                self._mac = tar_mac
                for n in range(0,5):
                    try: 
                        print(" "*30 + "...龟速连接中，正在尝试*" + str(n) +'...', end='\r')
                        self._conn = Peripheral(dev.addr, dev.addrType)
                        break
                    except:
                        if n<4:
                            continue
                        else:
                            print('\n')
                            print("设备连接失败，查看设备状态后重试")
                            break
                if self._conn:
                    self._conn.setDelegate(ReceiveDelegate())
                    self._conn.setMTU(500)
                    self.print_char()


    def con_age(self, addr, addrType):
        self._conn = Peripheral(addr, addrType )
        self._conn.setDelegate(ReceiveDelegate())
        self._conn.setMTU(500)

    def print_char(self):
        # Get service & characteristic
        print("设备支持BLE, GATT接口如下：")  
        print('\n')
        services = self._conn.getServices()
        print(60*'-')
        for svc in services:
            print("[+]        Service: ", svc.uuid)
            characteristics = svc.getCharacteristics()
            for charac in characteristics:
                uu = charac.uuid
                Properties = charac.propertiesToString()
                print("    Characteristic: ", uu)
                print("        Properties: ", Properties)
                print("            handle: ", charac.getHandle())

                # read
                if 'READ' in Properties:
                    try:
                        self.read_char(charac)
                    except:
                        continue
            print(60*'-')
        os._exit(0)
        #self._conn.disconnect()
        

    @timeout(3)
    def read_char(self, charac):
        value = charac.read()
        print("             Value: ", value)
        print("            charac: ", charac)

class inqInfo():

    def __init__(self):

        hciInfo = "hcitool info " + target_mac
        self.os_hciInfo = os.popen(hciInfo).read()


class sdpInfo():

    def __init__(self):
        self.info = bluetooth.find_service(address=target_mac)

class scanClaDevs():
    def __init__(self):
        clascan = 'hcitool scan --class --oui --flush'
        self.os_clascan = os.popen(clascan).read()
        


if __name__ == '__main__':

    devType = "hcitool inq"

    devTypeInfo = ''
    os_devtype = os.popen(devType).read()
    print('\n')
    print(' '*30+"准备起飞...(*￣０￣)ノ"+" "*30)
    print('\n')
    print("*"*24+"开始扫描"+"*"*24)
    print('\n')
    
    for inq_info in os_devtype.split('\n'):
        if inq_info.startswith('\t'+target_mac):
            devTypeInfo = inq_info
            print(inq_info)         #打印设备类型信息和时钟偏移量

    if devTypeInfo == '':
        print("设备不支持经典蓝牙或未扫描到设备")

    
    classic_str = 'BR/EDR'
    ble_str = 'LE'

    inq_info = inqInfo()

    if info_type == 'info' or info_type == 'all':
        print('\n')
        print("*"*60) 
        print('\n')
        print(inq_info.os_hciInfo)     

    if info_type == 'sdp' or info_type == 'all':
        if classic_str in inq_info.os_hciInfo:
            sdp_info = sdpInfo()
            print('\n')
            print("*"*60) 
            print("设备支持BR/EDR, SDP服务如下：")
            print('\n')
            print(sdp_info.info)
        else:
            print('\n')
            print("*"*60)
            print('\n')
            print("设备不支持BR/EDR")
    
    if info_type == 'ble' or info_type == 'all':
            print('\n')
            print("*"*60)
            print('\n')
            mBle_con = BLE_control()
            mBle_con.tar_con(target_mac.lower())
