import bluetooth
import argparse


parser = argparse.ArgumentParser()
parser.add_argument('-m', '--mac', help='mac address of target', required=True)
parser.add_argument('-u', '--uuid', help='target uuid')
parser.add_argument('-p', '--port', type=int, help='target port')
# parser.add_argument('-s', '--scan', help='print sdp info', action='store_true')

# group = parser.add_mutually_exclusive_group()
# group.add_argument('-u', '--uuid', help='target uuid')

args = parser.parse_args()


def test_by_uuid(uuid, addr):
    sers = bluetooth.find_service(uuid,addr)

    if len(sers) != 0:
        port = sers["port"]
        addr = sers["host"]
        socket = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        print("connect to "+addr)
        socket.connect((addr,port))

        data1 = bytes.fromhex('587879757465737431')
        print("send data: "+data1)
        socket.send(data1)

        data2 = bytes.fromhex('587879757465737432')
        print("send data: "+data2)
        socket.send(data2)

        socket.close()

def test_by_port(port, addr):
    socket = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
    print("connect to "+addr.upper())
    socket.connect((addr,port))

    data1 = bytes.fromhex('587879757465737431')
    print("send data: "+str(data1))
    socket.send(data1)

    data2 = bytes.fromhex('587879757465737432')
    print("send data: "+str(data2))
    socket.send(data2)

    socket.close()


if __name__ == '__main__':   
    if args.uuid and args.mac:
        print("观察目标设备是否弹窗，如无弹窗，可进一步分析利用")
        test_by_uuid(args.uuid, args.mac)
    elif args.port and args.mac:
        print("观察目标设备是否弹窗，如无弹窗，可进一步分析利用")
        test_by_port(args.port,args.mac)
    else:
        parser.print_help()
