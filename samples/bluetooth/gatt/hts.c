/** @file
 *  @brief HTS (Health Thermometer) Service sample
 */

/*
 * Copyright (c) 2016 Intel Corporation
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

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

static struct bt_gatt_ccc_cfg ct_ccc_cfg[CONFIG_BLUETOOTH_MAX_PAIRED] = {};
static struct bt_gatt_indicate_params ind_params;
static int hts_initialized = 0;


static void ct_ccc_cfg_changed(uint16_t value)
{
	/* TODO: Handle value */
}

static void indicate_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			uint8_t err)
{
	printk("Indication %s\n", err != 0 ? "fail" : "success");
}


/* Health Temperature Service Declaration */
static struct bt_gatt_attr attrs[] = {
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HTS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HTS_MEASUREMENT, BT_GATT_CHRC_INDICATE),
	BT_GATT_DESCRIPTOR(BT_UUID_HTS_MEASUREMENT, BT_GATT_PERM_READ, NULL,
			   NULL, NULL),
	BT_GATT_CCC(ct_ccc_cfg, ct_ccc_cfg_changed),

};

void hts_init()
{
	bt_gatt_register(attrs, ARRAY_SIZE(attrs));
	hts_initialized = 1;
}

void hts_indicate(uint32_t temperature)
{
	if (hts_initialized == 1) {
		uint8_t htm[5] = {};

		htm[0] = 0; /* flags bit 0 Celsius */
		memcpy(&htm[1], &temperature, sizeof(temperature));
		ind_params.attr = &attrs[2];
		ind_params.func = indicate_cb;
		ind_params.data = &htm;
		ind_params.len = sizeof(htm);

		bt_gatt_indicate(NULL, &ind_params);
	}
}
