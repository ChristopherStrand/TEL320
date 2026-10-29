import asyncio
from bleak import BleakClient, BleakScanner

NAME = "TEL320_Radar"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def got_data(sender, data):
    distance, amplitude = data.decode().strip().split(",")
    print(f"distance: {distance}mm  amplitude: {amplitude})")


async def main():
    device = await BleakScanner.find_device_by_name(NAME)
    if device is None:
        return

    async with BleakClient(device) as client:
        await client.start_notify(TX_UUID, got_data)
        while client.is_connected:
            await asyncio.sleep(1)


asyncio.run(main())
