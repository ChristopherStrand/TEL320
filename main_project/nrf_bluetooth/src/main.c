#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <bluetooth/services/nus.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LINE_LEN 32

struct line {
	char text[LINE_LEN];
	int len;
};

K_MSGQ_DEFINE(lines, sizeof(struct line), 10, 4);
static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_stm32_uart));
static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data scan_data[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void uart_isr(const struct device *dev, void *user_data){
	static struct line rx;
	char c;

	uart_irq_update(dev);

	while (uart_irq_rx_ready(dev) && uart_fifo_read(dev, &c, 1) == 1) {
		if (c == '\r') {
			continue;
		}

		rx.text[rx.len++] = c;

		if (c == '\n' || rx.len == LINE_LEN) {
			k_msgq_put(&lines, &rx, K_NO_WAIT);
			rx.len = 0;
		}
	}
}

int main(void){
	struct line line;
	uart_irq_callback_set(uart, uart_isr);
	uart_irq_rx_enable(uart);
	bt_enable(NULL);
	bt_nus_init(NULL);
	bt_le_adv_start(BT_LE_ADV_CONN, adv_data, ARRAY_SIZE(adv_data),
			scan_data, ARRAY_SIZE(scan_data));
	LOG_INF("Advertising as %s", CONFIG_BT_DEVICE_NAME);

	while (1) {
		k_msgq_get(&lines, &line, K_FOREVER);
		LOG_INF("STM32: %.*s", line.len - 1, line.text); 
		bt_nus_send(NULL, line.text, line.len);
	}
}
