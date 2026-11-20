import statistics
import time

import matplotlib.pyplot as plt
from adafruit_ble import BLERadio
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
from adafruit_ble.services.nordic import UARTService

NAME = "TEL320_Radar"
RATE_HZ = 30


def connect():
    ble = BLERadio()
    connection = None
    while connection is None:
        for adv in ble.start_scan(ProvideServicesAdvertisement, timeout=5):
            if UARTService in adv.services and adv.complete_name == NAME:
                connection = ble.connect(adv)
                break
        ble.stop_scan()
    return connection


def read_samples(connection, seconds):
    uart = connection[UARTService]
    samples = []
    start = time.time()
    while connection.connected and time.time() - start < seconds:
        line = uart.readline()
        if not line:
            continue
        try:
            distance, amplitude = line.decode().strip().split(",")
            distance, amplitude = int(distance), int(amplitude)
            samples.append((time.time() - start, distance, amplitude))
        except ValueError:
            pass
    return samples


def test_accuracy(connection, distances_m=(0.25, 0.5, 1.0, 1.5, 2.0), seconds=10):
    for true_m in distances_m:
        input(f"Place the flat plate at {true_m} m")
        d = [s[1] for s in read_samples(connection, seconds) if s[1] > 0] 
        mean = statistics.mean(d)
        std = statistics.stdev(d)
        print(f"true {true_m * 1000:.0f} mm, mean {mean:.1f} mm,  "
              f"error {mean - true_m * 1000:+.1f} mm, std {std:.1f} mm, n={len(d)}")


def test_drone(connection, seconds=30):
    samples = read_samples(connection, seconds)
    plt.plot([s[0] for s in samples], [s[1] if s[1] > 0 else float("nan") for s in samples])
    plt.xlabel("time (s)")
    plt.ylabel("distance (mm)")
    plt.title("Drone distance over time")
    plt.show()


def test_packet_loss(connection, distance_m, seconds=300):
    samples = read_samples(connection, seconds)
    expected = RATE_HZ * seconds
    loss = 100 * (1 - len(samples) / expected)
    print(f"received {len(samples)} of {expected} expected, loss {loss:.2f}%")


def stream(connection):
    uart = connection[UARTService]
    while connection.connected:
        line = uart.readline()
        if line:
            print(line.decode(errors="replace").strip())


def main():
    connection = connect()
    #test_accuracy(connection)
    stream(connection)



main()
