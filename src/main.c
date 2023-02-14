
#include <zephyr/sys/printk.h>

//devicetree ++ gpio includes
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

//bluetooth include files
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

//Max6675 sensor include files
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#define DEVICE_NAME "Temperature Monitoring Device"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME)-1)

int subsc = 0;

//Bluetooth advertising data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

//define the custom services and characteristics

//Service: simple service UUID B9A4A778-4BD5-4FC9-9885-FEC0C5531307
static struct bt_uuid_128 temperature_service_uuid =
    BT_UUID_INIT_128(0x07, 0x13, 0x53, 0xC5, 0xC0, 0xFE, 0x85, 0x98, 0xC9, 0x4F, 0xD5, 0x4B, 0x78, 0xA7, 0xA4, 0xB9);

//Charasteristic: LED1 Charasteristic UUID B9A4A779-4BD5-4FC9-9885-FEC0C5531307
static struct bt_uuid_128 temp_value_uuid =
    BT_UUID_INIT_128(0x07, 0x13, 0x53, 0xC5, 0xC0, 0xFE, 0x85, 0x98, 0xC9, 0x4F, 0xD5, 0x4B, 0x79, 0xA7, 0xA4, 0xB9);

static uint8_t temp_value = 0;

//LED1 read value function
static ssize_t read_temp(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
    printk("Read value of LED1: 0x%02x\n", temp_value);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &temp_value, sizeof(temp_value));
	
}

void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch(value)
    {
        case BT_GATT_CCC_NOTIFY: 
            subsc = 1;
            break;

        case BT_GATT_CCC_INDICATE: 
            subsc = 0;
            // Start sending stuff via indications
            break;

        case 0: 
            subsc = 0;
            // Stop sending stuff
            break;
        
        default: 
            printk("Error, CCCD has been set to an invalid value");     
    }
}


//Instantiate the service and charasteristics
BT_GATT_SERVICE_DEFINE(
    //service
    temperature_service, BT_GATT_PRIMARY_SERVICE(&temperature_service_uuid),

    //LED1 charasteristic
    //properties:read, write
    BT_GATT_CHARACTERISTIC(&temp_value_uuid,
                                BT_GATT_CHRC_NOTIFY,
                                BT_GATT_PERM_READ,
                                NULL,
                                NULL,
                               NULL),
		BT_GATT_CCC(on_cccd_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void my_service_send(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    /* 
    The attribute for the TX characteristic is used with bt_gatt_is_subscribed 
    to check whether notification has been enabled by the peer or not.
    Attribute table: 0 = Service, 1 = Primary service, 2 = RX, 3 = TX, 4 = CCC.
    */
    const struct bt_gatt_attr *attr = &temperature_service.attrs[1]; 

    struct bt_gatt_notify_params params = 
    {
        .uuid   = &temp_value_uuid,
        .attr   = attr,
        .data   = data,
        .len    = len,
        //.func   = on_sent
    };

    // Check whether notifications are enabled or not
    if(bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) 
    {
        // Send the notification
	    if(bt_gatt_notify_cb(conn, &params))
        {
            printk("Error, unable to send notification\n");
        }
    }
    else
    {
        printk("Warning, notification not enabled on the selected attribute\n");
    }
}

struct bt_conn *my_connection;
int connected = 0;

//Connected callback function
static void connected_cb(struct bt_conn *conn, uint8_t err){
	my_connection = conn;
	connected = 1;
    if(err)
    {
        printk("Connection failed with err %d\n", err);
    }
    else{
        printk("Connection succesfull!");
    }
}

//Disconnected callback function
static void disconnected_cb(struct bt_conn *conn, uint8_t reason){
    printk("Disconnected with reason %d\n");

}
//Connection callback structure
static struct bt_conn_cb conn_callbacks = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

void main(void)
{

	const struct device *const dev = DEVICE_DT_GET_ONE(maxim_max6675);
	struct sensor_value val;

	if (!device_is_ready(dev)) {
		printk("sensor: device not ready.\n");
		return;
	}
	int err;
	//Initialize the bluetooth stack
    err = bt_enable(NULL);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
    printk("Bluetooth stack init succeess \n");

    //Register for connection callbacks
    bt_conn_cb_register(&conn_callbacks);
    if (err)
    {
        printk("Conn CB registration failed (err %d)\n", err);
        return;
    }

    //start for advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if(err){
        printk("Advertising start failed with err %d\n", err);
        return;
    }

    printk("Bluetooth advertising started \n");
	uint32_t number = 0;
	
	while(1){
        int ret;

		ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_AMBIENT_TEMP);
		if (ret < 0) {
			printf("Could not fetch temperature (%d)\n", ret);
			return;
		}

		ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
		if (ret < 0) {
			printf("Could not get temperature (%d)\n", ret);
			return;
		}

		printf("Temperature: %.2f C\n", sensor_value_to_double(&val));

        
        int temp = val.val1;
        printf("sensor raw: %d \n", temp);
		if(connected){
			//my_service_send(my_connection, (uint8_t*)val.val1, sizeof(val.val1));
            printf("subsc value %d", subsc);
            if(subsc){
                bt_gatt_notify(NULL, &temperature_service.attrs[2], &temp, sizeof(temp));
            }
            
			printk("Hello!");
			number++;

		}
		k_sleep(K_MSEC(3000) );
	

	}
}
