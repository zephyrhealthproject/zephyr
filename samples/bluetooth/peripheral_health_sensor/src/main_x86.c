/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <misc/printk.h>
#include <misc/byteorder.h>
#include <zephyr.h>
#include <gpio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <gatt/gap.h>
#include <gatt/hrs.h>
#include <gatt/dis.h>
#include <gatt/bas.h>
#include <gatt/hts.h>
#include <gatt/spo2.h>

#include <ipm.h>
#include <ipm/ipm_quark_se.h>

QUARK_SE_IPM_DEFINE(hrs_ipm, 0, QUARK_SE_IPM_INBOUND);

#define DEVICE_NAME		"Zephyr Health Sensor"
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)
#define HEART_RATE_APPEARANCE	0x0341
#define HRS_ID			99
#define HTM_ID			100
#define SPO2_ID			101
#define RESP_ID			102

#define GPIO_OUT_PIN	2
#define GPIO_NAME	"GPIO_"
#define GPIO_DRV_NAME "GPIO_0"

struct bt_conn *default_conn;

static uint8_t resp_rate = 0;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x05, 0x18, 0x09, 0x18, 0x22, 0x18),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
	} else {
		default_conn = bt_conn_ref(conn);
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	gap_init(DEVICE_NAME, HEART_RATE_APPEARANCE);
	hrs_init(0x01);
	bas_init();
	hts_init();
	spo2_init();
	dis_init(CONFIG_SOC, "Manufacturer");

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

void hrs_ipm_callback(void *context, uint32_t id, volatile void *data)
{
	switch (id) {
	/* only accept value from defined HRS channel */
		case HRS_ID:
		{
			if (default_conn) {
				uint8_t hr = *(volatile uint8_t *) data;
				printk("hrs_ipm_callback - value = %d\n", hr);
				hrs_notify(hr, resp_rate);
			}
		}
		break;

    case RESP_ID:
    {
      uint8_t resp_rate_update = *(volatile uint8_t *) data;
      printk("htm_ipm_callback - respiratory rate value = %d\n", resp_rate_update);
      resp_rate = resp_rate_update;
    }
    break;


		case HTM_ID:
		{
			if (default_conn) {
				uint32_t temp = *(volatile uint32_t *) data;
				printk("htm_ipm_callback - temp value = %d\n", temp);
				hts_indicate(temp);
			}
		}
		break;

		case SPO2_ID:
		{
			if (default_conn) {
				int spo2 = *(volatile int32_t *) data;
				printk("htm_ipm_callback - spo2 value = %d\n", spo2);
				spo2_indicate(spo2);
			}
		}
		break;


	default:
		break;
	}
}

void main(void)
{
	struct device *ipm, *gpio_dev;

	int err, ret, toggle;

	ipm = device_get_binding("hrs_ipm");
	if (!ipm) {
		printk("IPM: Device not found.\n");
		return;
	}

	ipm_set_enabled(ipm, 1);
	ipm_register_callback(ipm, hrs_ipm_callback, NULL);

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);

	gpio_dev = device_get_binding(GPIO_DRV_NAME);
	if (!gpio_dev) {
		printk("Cannot find %s!\n", GPIO_DRV_NAME);
	}

	/* Setup GPIO output */
	ret = gpio_pin_configure(gpio_dev, GPIO_OUT_PIN, (GPIO_DIR_OUT));
	if (ret) {
		printk("Error configuring " GPIO_NAME "%d!\n", GPIO_OUT_PIN);
	}


	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	toggle = 0;

	while (1) {
		task_sleep(sys_clock_ticks_per_sec);
		/* Battery level simulation */
		bas_notify();

		ret = gpio_pin_write(gpio_dev, GPIO_OUT_PIN, toggle);
		if (ret) {
			printk("Error set " GPIO_NAME "%d!\n", GPIO_OUT_PIN);
		}

		if (toggle) {
			toggle = 0;
		} else {
			toggle = 1;
		}

		printk(".");
	}
}
