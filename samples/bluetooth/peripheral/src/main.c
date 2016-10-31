/* main.c - Application main entry point */

/*
 * Copyright (c) 2015 Intel Corporation
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

#include <gatt/gap.h>
#include <gatt/hrs.h>
#include <gatt/dis.h>
#include <gatt/bas.h>
#include <gatt/cts.h>

#define DEVICE_NAME		"Test peripheral"
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)
#define HEART_RATE_APPEARANCE	0x0341

/* Custom Service Variables */
static struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(
	0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static struct bt_uuid_128 vnd_enc_uuid = BT_UUID_INIT_128(
	0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static struct bt_uuid_128 vnd_auth_uuid = BT_UUID_INIT_128(
	0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static uint8_t vnd_value[] = { 'V', 'e', 'n', 'd', 'o', 'r' };

static ssize_t read_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 strlen(value));
}

static ssize_t write_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(vnd_value)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}

static struct bt_gatt_ccc_cfg vnd_ccc_cfg[CONFIG_BLUETOOTH_MAX_PAIRED] = {};
static uint8_t simulate_vnd;
static uint8_t indicating;
static struct bt_gatt_indicate_params ind_params;

static void vnd_ccc_cfg_changed(uint16_t value)
{
	simulate_vnd = (value == BT_GATT_CCC_INDICATE) ? 1 : 0;
}

static void indicate_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			uint8_t err)
{
	printk("Indication %s\n", err != 0 ? "fail" : "success");
	indicating = 0;
}

#define MAX_DATA 74
static struct vnd_long_value {
	/* TODO: buffer needs to be per connection */
	uint8_t buf[MAX_DATA];
	uint16_t prep_data_len;
	uint8_t data[MAX_DATA];
	uint16_t data_len;
	uint8_t prep_write_err;
} vnd_long_value = {
	.data = { 'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '1',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '2',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '3',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '4',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '5',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '6',
		  '.', ' ' },
	.data_len = MAX_DATA,
};

static ssize_t read_long_vnd(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	struct vnd_long_value *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value->data,
				 value->data_len);
}

static ssize_t write_long_vnd(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset)
{
	struct vnd_long_value *value = attr->user_data;

	if (value->prep_write_err) {
		return len;
	}

	if (offset > value->data_len) {
		value->prep_write_err = BT_ATT_ERR_INVALID_OFFSET;
		return len;
	}

	if (offset + len > MAX_DATA) {
		value->prep_write_err = BT_ATT_ERR_INVALID_ATTRIBUTE_LEN;
		return len;
	}

	/* Copy to buffer */
	value->prep_data_len += len;
	memcpy(value->buf + offset, buf, len);

	return len;
}

static ssize_t flush_long_vnd(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, uint8_t flags)
{
	struct vnd_long_value *value = attr->user_data;
	uint16_t flushed_data_len = 0;

	if (!value->prep_data_len && !value->prep_write_err) {
		return flushed_data_len;
	}

	if (value->prep_write_err) {
		uint8_t err = value->prep_write_err;

		memset(value->buf, 0, value->prep_data_len);
		value->prep_data_len = 0;
		value->prep_write_err = 0;

		return BT_GATT_ERR(err);
	}

	flushed_data_len = value->prep_data_len;

	switch (flags) {
	case BT_GATT_FLUSH_SYNC:
		/* Sync buffer to data */
		memcpy(value->data, value->buf, value->prep_data_len);
		value->data_len = value->prep_data_len;
		/* Fallthrough */
	case BT_GATT_FLUSH_DISCARD:
		memset(value->buf, 0, value->prep_data_len);
		value->prep_data_len = 0;
		return flushed_data_len;
	}

	return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
}

static const struct bt_uuid_128 vnd_long_uuid = BT_UUID_INIT_128(
	0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static struct bt_gatt_cep vnd_long_cep = {
	.properties = BT_GATT_CEP_RELIABLE_WRITE,
};

static int signed_value;

static ssize_t read_signed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(signed_value));
}

static ssize_t write_signed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(signed_value)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}

static const struct bt_uuid_128 vnd_signed_uuid = BT_UUID_INIT_128(
	0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x13,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x13);

/* Vendor Primary Service Declaration */
static struct bt_gatt_attr vnd_attrs[] = {
	/* Vendor Primary Service Declaration */
	BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_enc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_INDICATE),
	BT_GATT_DESCRIPTOR(&vnd_enc_uuid.uuid,
			   BT_GATT_PERM_READ_ENCRYPT |
			   BT_GATT_PERM_WRITE_ENCRYPT,
			   read_vnd, write_vnd, vnd_value),
	BT_GATT_CCC(vnd_ccc_cfg, vnd_ccc_cfg_changed),
	BT_GATT_CHARACTERISTIC(&vnd_auth_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE),
	BT_GATT_DESCRIPTOR(&vnd_auth_uuid.uuid,
			   BT_GATT_PERM_READ_AUTHEN |
			   BT_GATT_PERM_WRITE_AUTHEN,
			   read_vnd, write_vnd, vnd_value),
	BT_GATT_CHARACTERISTIC(&vnd_long_uuid.uuid, BT_GATT_CHRC_READ |
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP),
	BT_GATT_LONG_DESCRIPTOR(&vnd_long_uuid.uuid,
			   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			   read_long_vnd, write_long_vnd, flush_long_vnd,
			   &vnd_long_value),
	BT_GATT_CEP(&vnd_long_cep),
	BT_GATT_CHARACTERISTIC(&vnd_signed_uuid.uuid, BT_GATT_CHRC_READ |
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_AUTH),
	BT_GATT_DESCRIPTOR(&vnd_signed_uuid.uuid,
			   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			   read_signed, write_signed, &signed_value),
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      0x0d, 0x18, 0x0f, 0x18, 0x05, 0x18),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
		      0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);
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
	cts_init();
	dis_init(CONFIG_SOC, "Manufacturer");
	bt_gatt_register(vnd_attrs, ARRAY_SIZE(vnd_attrs));

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
};

void main(void)
{
	int err;

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);

	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	while (1) {
		task_sleep(sys_clock_ticks_per_sec);

		/* Current Time Service updates only when time is changed */
		cts_notify();

		/* Heartrate measurements simulation */
		hrs_notify();

		/* Battery level simulation */
		bas_notify();

		/* Vendor indication simulation */
		if (simulate_vnd) {
			if (indicating) {
				continue;
			}

			ind_params.attr = &vnd_attrs[2];
			ind_params.func = indicate_cb;
			ind_params.data = &indicating;
			ind_params.len = sizeof(indicating);

			if (bt_gatt_indicate(NULL, &ind_params) == 0) {
				indicating = 1;
			}
		}
	}
}