import time
import matplotlib.pyplot as plt
from adafruit_ble import BLERadio
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
from adafruit_ble.services.nordic import UARTService

NAME = "TEL320_Radar"


def main():
    ble = BLERadio()

    connection = None
    while connection is None:
        for adv in ble.start_scan(ProvideServicesAdvertisement, timeout=5):
            if UARTService in adv.services and adv.complete_name == NAME:
                connection = ble.connect(adv)
                break
        ble.stop_scan()

    uart = connection[UARTService]

    times, distances = [], []
    start = time.time()

    plt.ion()
    fig, ax = plt.subplots()
    (line_plot,) = ax.plot([], [])
    ax.set_xlabel("time (s)")
    ax.set_ylabel("distance (mm)")

    while connection.connected and plt.fignum_exists(fig.number):
        line = uart.readline()
        if line:
            try:
                distance, amplitude = line.decode().strip().split(",")
                times.append(time.time() - start)
                distances.append(int(distance))
            except ValueError:
                continue
            line_plot.set_data(times, distances)
            ax.relim()
            ax.autoscale_view()
        plt.pause(0.001)


main()
