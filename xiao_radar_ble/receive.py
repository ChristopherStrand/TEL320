import asyncio
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "A111 Radar"
UART_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

buf = ""


def on_data(_, data):
    global buf
    buf += data.decode()
    while "\n" in buf:
        line, buf = buf.split("\n", 1)
        try:
            dist, amp = line.split(",")
            print(f"distance: {dist} mm   amplitude: {amp}")
        except ValueError:
            print(line)


async def main():
    print("Looking for", DEVICE_NAME)
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if device is None:
        print("Not found")
        return

    async with BleakClient(device) as client:
        print("Connected")
        await client.start_notify(UART_TX, on_data)
        while client.is_connected:
            await asyncio.sleep(1)


asyncio.run(main())
