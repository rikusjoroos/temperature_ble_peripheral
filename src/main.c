/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

//For rounding
#include <math.h>

//bluetooth include files
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>

// Max6675 sensor include files
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>



// Device name and device name length used in advertising data
#define DEVICE_NAME "Temperature Monitoring Device"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

//GPIO
#include <zephyr/drivers/gpio.h>

#define LED1_NODE DT_ALIAS(ledext)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

// Bluetooth advertising data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

// Service: temperature service UUID B9A4A778-4BD5-4FC9-9885-FEC0C5531307
static struct bt_uuid_128 temperature_service_uuid =
    BT_UUID_INIT_128(0x07, 0x13, 0x53, 0xC5, 0xC0, 0xFE, 0x85, 0x98, 0xC9, 0x4F, 0xD5, 0x4B, 0x78, 0xA7, 0xA4, 0xB9);

// Charasteristic: Temperature value UUID Charasteristic UUID B9A4A779-4BD5-4FC9-9885-FEC0C5531307
static struct bt_uuid_128 temp_value_uuid =
    BT_UUID_INIT_128(0x07, 0x13, 0x53, 0xC5, 0xC0, 0xFE, 0x85, 0x98, 0xC9, 0x4F, 0xD5, 0x4B, 0x79, 0xA7, 0xA4, 0xB9);

// When notification status is changed this function is called
int subsc = 0;
void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch (value)
    {
    case BT_GATT_CCC_NOTIFY:

        subsc = 1;
        printk("bt_gatt sub %d", subsc);
        break;

    case 0:
        subsc = 0;
        printk("bt_gatt unsub %d", subsc);
        // Stop sending stuff
        break;

    default:
        printk("Error, CCCD has been set to an invalid value");
    }
}

// Instantiate the service and charasteristics
BT_GATT_SERVICE_DEFINE(
    // service
    temperature_service, BT_GATT_PRIMARY_SERVICE(&temperature_service_uuid),

    // Temperature value characteristic
    // properties:read
    BT_GATT_CHARACTERISTIC(&temp_value_uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           NULL,
                           NULL,
                           NULL),
    BT_GATT_CCC(on_cccd_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );



struct bt_conn *my_connection;

// Connected callback function called when connected to device
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    my_connection = conn;

    if (err)
    {
        printk("Connection failed with err %d\n", err);
    }
    else
    {
        printk("Connection succesfull!");
    }
}

// Disconnected callback function
static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected with reason %d\n", reason);
}

// Connection callback structure
static struct bt_conn_cb conn_callbacks = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

// Measure ten values and return average of values
double averageMeasurement(const struct device *const dev){
        double avg = 0;
        double sum = 0;
        double values[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        double tempd;
        int ret;


        //Struct for sensor value
        struct sensor_value val;

        for(int i = 0; i < 10; i++){
            // Fetching sample
            ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_AMBIENT_TEMP);
            if (ret < 0)
            {
                printk("Could not fetch temperature (%d)\n", ret);

            }

            // Getting value
            ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
            if (ret < 0)
            {
                printk("Could not get temperature (%d)\n", ret);

            }

            tempd = sensor_value_to_double(&val);
            values[i] = tempd;
            k_sleep(K_SECONDS(1));
        }

        for (int i = 0; i < 10; i++){
            sum = sum + values[i];

        }
        avg = sum / 10;
        return avg;
}

void main(void)
{
    // Temperature sensor (amplifier)
    const struct device *const dev = DEVICE_DT_GET_ONE(maxim_max6675);

    //For checking that temperature sensor is ready
    if (!device_is_ready(dev))
    {
        printk("Sensor: device not ready.\n");

    }

	int err;
	// Initialize the bluetooth stack
    err = bt_enable(NULL);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
    printk("Bluetooth stack init succeess \n");

    // Register for connection callbacks
    bt_conn_cb_register(&conn_callbacks);
    if (err)
    {
        printk("Conn CB registration failed (err %d)\n", err);
        return;
    }

    // Start for advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        printk("Advertising start failed with err %d\n", err);
        return;
    }

    printk("Bluetooth advertising started \n");

    //Configure led
    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (err<0)
    {
        return;
    }



    while (1)
    {

        //If notifications are subsribed then send data
        if (subsc)
        {
            double AvgTempDouble = 0;
            int AvgInt = 0;

            //turn led on when measuring is started
            gpio_pin_set_dt(&led, 1);

            //Temperature average as double
            AvgTempDouble = averageMeasurement(dev);

            //Rounded temperature
            AvgInt = round(AvgTempDouble);
            printk("Rounded temperature: %d, Average temperature: %f \n\r",AvgInt, AvgTempDouble);
            
            //Send notification
            bt_gatt_notify(NULL, &temperature_service.attrs[2], &AvgInt, sizeof(AvgInt));

            //Turn led off when data is sent
            gpio_pin_set_dt(&led, 0);
        }
        
        //Sleep 50 seconds, measuring takes 10 seconds so total 60 second interval
        k_sleep(K_SECONDS(50));
    }
    
}
