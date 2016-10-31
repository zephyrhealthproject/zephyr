/* main_arc.c - main source file for ARC app */

/*
 * Copyright (c) 2016 Intel Corporation.
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

#include <zephyr.h>
#include "heartrate.h"
//#include "temp.h"

#include <ipm.h>
#include <ipm/ipm_quark_se.h>
#include <init.h>
#include <device.h>
#include <gpio.h>
#include <misc/byteorder.h>
#include <adc.h>
#include <i2c.h>
#include <stdio.h>
#include <string.h>
#include <display/grove_lcd.h>

#include <misc/printk.h>
#define DBG	printk

/* measure every 2ms */
#define INTERVAL 	2
#define SLEEPTICKS MSEC(INTERVAL)

#define ADC_DEVICE_NAME "ADC_0"
#define ADC_CHANNEL_HR 	13   /* AD3 */
#define ADC_CHANNEL_TEMP 14  /* AD4 */
#define ADC_BUFFER_SIZE 4

/* ID of IPM channel */
#define HRS_ID		99
#define HTM_ID		100
#define SPO2_ID		101
#define RESP_ID		102

#define GPIO_INT_PIN        2 /* AD0 */
#define GPIO_OUT_PIN_AD1	3 /* AD1 */
#define GPIO_OUT_PIN_AD2    4 /* AD2 */
#define GPIO_NAME	"GPIO_SS_"
#define GPIO_DRV_NAME "GPIO_0"

unsigned char RR = 0;

QUARK_SE_IPM_DEFINE(hrs_ipm, 0, QUARK_SE_IPM_OUTBOUND);
volatile static int spO2_counter = 0;
static int spO2 = 0;

static int red_intensity = 0;
static int ir_intensity = 0;

static int fadeRate = 0;
static const uint8_t color[4][3] = {{  0,   0, 255},	/* blue */
		{  0, 255,   0},	/* green */
		{255, 255,   0},	/* yellow */
		{255,   0,   0}};	/* red */

/* buffer for the ADC data.
 * here only 4 bytes are used to store the analog signal
 */
static uint8_t seq_buffer_temp[ADC_BUFFER_SIZE];
static uint8_t seq_buffer_hr[ADC_BUFFER_SIZE];

static struct adc_seq_entry samples[2] = {
		{
			.sampling_delay = 12,
			.channel_id = ADC_CHANNEL_HR,
			.buffer = seq_buffer_hr,
			.buffer_length = ADC_BUFFER_SIZE
		},
		{
			.sampling_delay = 12,
			.channel_id = ADC_CHANNEL_TEMP,
			.buffer = seq_buffer_temp,
			.buffer_length = ADC_BUFFER_SIZE
		}
};

static struct adc_seq_table table_hr = {
		.entries = samples,
		.num_entries = 2,
};


static struct device *glcd = NULL;

static struct gpio_callback gpio_cb;

/* Display the heartbeat by fading out the LCD with color. The index
 * stands for color: blue (0), green (1), yellow(2) and red(3).
 * Blue for heartrate lower than 60bpm, green for the range 60 - 79 bpm,
 * yellow for 80 - 99 bpm, and red for 100 bpm and above.
 */
static void show_heartbeat_using_fade_effect(int index)
{
	uint8_t red, green, blue;
	if (!glcd || fadeRate < 0 || index < 0 || index > 3) {
		return;
	}

	red = (color[index][0] * fadeRate / 255) & 0xff;
	green = (color[index][1] * fadeRate / 255) & 0xff;
	blue = (color[index][2] * fadeRate / 255) & 0xff;
	glcd_color_set(glcd, red, green, blue);

	fadeRate -= 15;
}

uint32_t encodeTemperature(int mantissa, int exponent)
{
	uint32_t result = (exponent << 24) | (mantissa & 0xFFFFFF);
	return result;
}

void printLCD(int hr, int rr, int temp, int spO2)
{
	char str[20];

	memset(str, 0, 20);

	if (glcd) {
		glcd_clear(glcd);
		glcd_cursor_pos_set(glcd, 0, 0);
		sprintf(str, "HR: %3d RR:  %3d", hr, rr);
		glcd_print(glcd, str, strlen(str));
		glcd_cursor_pos_set(glcd, 0, 1);
		memset(str, 0, 20);
		sprintf(str, "TMP:%3d SPO2:%3d", temp, spO2);
		glcd_print(glcd, str, strlen(str));
	}
}

static inline int sendToHost(struct device *ipm, uint32_t id, const void *data, int size )
{
	int ret = 0;
	if (ipm) {
		ret = ipm_send(ipm, 1, id, data, size);
		if (ret) {
			printk("Failed to send IPM message, error (%d)\n", ret);
		}
	}
	return ret;
}


void gpio_callback(struct device *port,
		   struct gpio_callback *cb, uint32_t pins)
{
	spO2_counter++;
}


void main(void)
{
	static double milliVoltsRunningAverage = 0;
	struct device *adc, *ipm;
	struct nano_timer timer;
	double degreesC = 25;
	uint32_t data[2] = {0, 0};
	int count;
	int checkTime, tempTime;
	int index;
	int value;
	int hr = 0;
	int ret;
	int red_led_on = 1, ir_led_on = 0;
	bool isValid;
	uint8_t ipm_value;
	double degreesF=0;
	struct device *gpio_dev;

	nano_timer_init(&timer, data);

	DBG("IPM: main called.\n");
#define GPIO_ENABLED
#ifdef GPIO_ENABLED
	/* Setup GPIO subsystem */
	gpio_dev = device_get_binding(GPIO_DRV_NAME);
	if (!gpio_dev) {
		printk("Cannot find %s!\n", GPIO_DRV_NAME);
	}

	/* Setup GPIO output */
	ret = gpio_pin_configure(gpio_dev, GPIO_OUT_PIN_AD1, (GPIO_DIR_OUT));
	if (ret) {
		printk("Error configuring " GPIO_NAME "%d!\n", GPIO_OUT_PIN_AD1);
	}

	ret = gpio_pin_configure(gpio_dev, GPIO_OUT_PIN_AD2, (GPIO_DIR_OUT));
	if (ret) {
		printk("Error configuring " GPIO_NAME "%d!\n", GPIO_OUT_PIN_AD2);
	}

	/* Setup GPIO input, and triggers on rising edge. */
	ret = gpio_pin_configure(gpio_dev, GPIO_INT_PIN,
			(GPIO_DIR_IN | GPIO_INT | GPIO_INT_EDGE
			 | GPIO_INT_ACTIVE_HIGH | GPIO_INT_DEBOUNCE));
	if (ret) {
		printk("Error configuring " GPIO_NAME "%d!\n", GPIO_INT_PIN);
	}

	gpio_init_callback(&gpio_cb, gpio_callback, BIT(GPIO_INT_PIN));

	ret = gpio_add_callback(gpio_dev, &gpio_cb);
	if (ret) {
		printk("Cannot setup callback!\n");
	}

	ret = gpio_pin_enable_callback(gpio_dev, GPIO_INT_PIN);
	if (ret) {
		printk("Error enabling callback!\n");
	}

	printk("Done initializing GPIO pins.\n");
#endif
	/* Initialize the IPM */
	ipm = device_get_binding("hrs_ipm");
	if (!ipm) {
		DBG("IPM: Device not found.\n");
	}

	/* Initialize the ADC */
	adc = device_get_binding(ADC_DEVICE_NAME);
	if (!adc) {
		DBG("ADC Controller: Device not found.\n");
		return;
	}
	adc_enable(adc);

#define LCD_ENABLED
#ifdef LCD_ENABLED
	/* Initialize the Grove LCD */
	glcd = device_get_binding(GROVE_LCD_NAME);
	if (!glcd) {
		DBG("Grove LCD: Device not found.\n");
	}
	else {
		/* Now configure the LCD the way we want it */

		uint8_t set_config = GLCD_FS_ROWS_2
				| GLCD_FS_DOT_SIZE_LITTLE
				| GLCD_FS_8BIT_MODE;

		glcd_function_set(glcd, set_config);
		set_config = GLCD_DS_DISPLAY_ON;
		glcd_display_state_set(glcd, set_config);
		glcd_color_set(glcd, 0, 0, 0);
	}

	if (glcd) {
		glcd_clear(glcd);
		glcd_cursor_pos_set(glcd, 0, 0);
		glcd_print(glcd, "HR: BEGIN", 9);
	}
#endif
	count = 0;
	tempTime = 1000;
	checkTime = 2000;
	index = 0;
	spO2 = 0;
	spO2_counter = 0;

	while (1) {
		// temp
		uint32_t rawTempValue = (uint32_t) seq_buffer_temp[0]
		                                        | (uint32_t) seq_buffer_temp[1] << 8
		                                        | (uint32_t) seq_buffer_temp[2] << 16
		                                        | (uint32_t) seq_buffer_temp[3] << 24;

		if (rawTempValue) {

		  double milliVolts = (rawTempValue / 4096.0 * 3630.0);  // millivolts = 12 bit resolution ADC at 3.63 ref voltage
		  milliVoltsRunningAverage = (milliVoltsRunningAverage) ? (milliVoltsRunningAverage * 49 + milliVolts) / 50.0 : milliVolts;
		}


		// heart rate
		isValid = false;
		if (adc_read(adc, &table_hr) == 0) {
			uint32_t signal = (uint32_t) seq_buffer_hr[0]
			                                        | (uint32_t) seq_buffer_hr[1] << 8
			                                        | (uint32_t) seq_buffer_hr[2] << 16
			                                        | (uint32_t) seq_buffer_hr[3] << 24;
			for (int i=0; i < ADC_BUFFER_SIZE; i++) {
				seq_buffer_hr[i] = 0;
			}

			value = measure_heartrate(signal);
			if (value > 0) {
				/* normal heartbeat */
				if (value > 50 && value < 120) {
					isValid = true;
				}
				/* for abnormal heartbeat, check if it is stable for two seconds */
				else {
					checkTime -= INTERVAL;
					if (checkTime < 0) {
						isValid = true;
					}
				}
				if (isValid) {
					hr = value;
					DBG("HR(BPM) = %d\n", hr);

					/* print to LCD screen */
					printLCD(hr, RR, (int) degreesF, spO2);

					/* send data over ipm to x86 side */
					if (ipm) {
						ipm_value = (uint8_t) value;
						ret = ipm_send(ipm, 1, HRS_ID, &ipm_value, sizeof(ipm_value));
						if (ret) {
							printk("Failed to send IPM message, error (%d)\n", ret);
						}
					}

					/* reset fading effect and check time for abnormal heart beat */
					fadeRate = 255;
					checkTime = 2000;
				}

				/* set the color code for heartbeat */
				if (value < 60)
					index = 0;
				else if (value < 80)
					index = 1;
				else if (value < 100)
					index = 2;
				else
					index = 3;
			}
		}
		/* blink the LCD to show the heartbeat */
		count ++;
		if (count >= 10) {
			show_heartbeat_using_fade_effect(index);
			count = 0;
		}

		tempTime -= INTERVAL;

		if (tempTime < 0) {
			if (red_led_on) {
				red_intensity = spO2_counter;
				printk("Setting red_intensity to %d\n", (int) red_intensity);
			} else {
				ir_intensity = spO2_counter;
				printk("Setting ir_intensity to %d\n", (int) ir_intensity);
			}

			printk("red_intensity = %d\n", red_intensity);
			printk("ir_intensity = %d\n", ir_intensity);
			if ((red_intensity > 0) && (ir_intensity > 0)) {
				spO2 =  (118 - (25 * red_intensity / (1.42 * ir_intensity))) * 10;
				if (spO2 > 1000) spO2 = 1000;
				if (spO2 < 850) spO2 = 0;
				printk("SpO2(1) = %d\n", spO2);
			}

			printk("SpO2(2) = %d\n", spO2);

			double milliVolts = milliVoltsRunningAverage;

#define MCP9700
#if defined(MCP9700)
			// Lily Pad Temperature Sensor
			degreesC = (milliVolts - 500) * 0.100;
#elif defined(TMP35)
			// TMP35  - 0 mV offset, scale 10 mV/degree C
			degreesC = (milliVolts - 0) * 0.100;
#elif defined(TMP36)
			// TMP36  - 500 mV offset, scale 10 mV/degree C
			degreesC = (milliVolts - 500) * 0.100;
#else // default
			// TMP37  - 0 mV offset, scale 20 mV/degree C
			degreesC = (milliVolts - 0) * 0.05;
#endif
			degreesF = (degreesC * 1.8) + 32.0;

			DBG("TEMP(F) = %d\n", (int) degreesF);

			int IEEE_Temp = encodeTemperature((int) (degreesC * 100), -2);

			/* print to LCD screen */
			printLCD(hr, RR, (int) degreesF, (int) spO2);

			/* send data over ipm to x86 side */
			sendToHost(ipm, HTM_ID, &IEEE_Temp, sizeof(IEEE_Temp));
			sendToHost(ipm, SPO2_ID, &spO2, sizeof(spO2));
			sendToHost(ipm, RESP_ID, &RR, sizeof(RR));

#ifdef GPIO_ENABLED
			/* toggle LED's for SpO2 sensor */
			printk("Writing to RED LED = %d\n", red_led_on);
			ret = gpio_pin_write(gpio_dev, GPIO_OUT_PIN_AD1, red_led_on);
			if (ret) {
				printk("Error set " GPIO_NAME "%d!\n", GPIO_OUT_PIN_AD1);
			}

			printk("Writing to IR LED = %d\n", ir_led_on);
			ret = gpio_pin_write(gpio_dev, GPIO_OUT_PIN_AD2, ir_led_on);
			if (ret) {
				printk("Error set " GPIO_NAME "%d!\n", GPIO_OUT_PIN_AD2);
			}
			spO2_counter = 0;

			// Toggle between the two LED's
			if (red_led_on) {
				red_led_on = 0;
				ir_led_on = 1;
			} else {
				red_led_on = 1;
				ir_led_on = 0;
			}
#endif
			tempTime = 1000;
		}

		nano_timer_start(&timer, SLEEPTICKS);
		nano_timer_test(&timer, TICKS_UNLIMITED);

	}
	adc_disable(adc);
}
