#include <FastLED.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_LEDS 2
#define ADC_ENABLE_PIN 3
#define APA102_SDI_PIN 7
#define APA102_CLK_PIN 6
#define ADC_PIN 4
#define BUTTON_PIN 5

#define FPC_IO_1 2
#define FPC_IO_2 8
#define FPC_IO_BTN BUTTON_PIN

#define SDA_PIN FPC_IO_1
#define SCL_PIN FPC_IO_2

// NOTE: must match the wifi channel set in wifi router
#define WIFI_CHANNEL 2

CRGB leds[NUM_LEDS];

// ESPNOW packet structure. Can be modified but should be the same on the receivers side.
typedef struct struct_message
{
	int id;
	uint8_t local_mac[6];
	int value;
	int battery_level;
	int single_tap_duration;
} struct_message;

typedef struct struct_message_recv
{
	bool answer;
} struct_message_recv;

struct_message data;
struct_message_recv data_recv;

#define ESPNOW_ID 8888											   // Random number
uint8_t receiver_address[] = {0x08, 0x3A, 0xF2, 0x45, 0x40, 0x08}; // Mac address of the receiver.  08:3A:F2:45:40:08
uint8_t local_address[] = {0x60, 0x55, 0xF9, 0x53, 0x74, 0xB4};	   // 60:55:F9:53:74:B4

bool espnow_answer_received = false;

void on_data_recv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
	memcpy(&data_recv, incomingData, sizeof(data_recv));
	espnow_answer_received = true;
}

#define BAT_VOLT_MULTIPLIER 1.43
#define BAT_VOLT_OFFSET 0

unsigned long t_last_press = 0;

float get_battery_voltage()
{
	digitalWrite(ADC_ENABLE_PIN, LOW);
	delayMicroseconds(10);
	int sum = 0;
	for (int i = 0; i < 100; i++)
	{
		int raw_val = analogRead(ADC_PIN);
		// printf("ADC raw value=%i\r\n", raw_val);
		sum = sum + raw_val;
	}
	float result = sum / 100.0;
	// printf("result=%f\r\n", result);
	digitalWrite(ADC_ENABLE_PIN, HIGH);
	return result * BAT_VOLT_MULTIPLIER + BAT_VOLT_OFFSET;
}

void setup()
{
	btStop();
	WiFi.mode(WIFI_STA);

    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

	pinMode(ADC_ENABLE_PIN, OUTPUT);
	pinMode(ADC_PIN, INPUT);
	analogReadResolution(12);

	digitalWrite(ADC_ENABLE_PIN, HIGH);

	FastLED.addLeds<APA102, APA102_SDI_PIN, APA102_CLK_PIN, BGR>(leds, NUM_LEDS)
		.setCorrection(TypicalLEDStrip);
	FastLED.setBrightness(8);

	delay(500);
	printf("Start to init esp_now.\r\n");
	if (esp_now_init() != ESP_OK)
	{
		printf("Error initializing ESP-NOW\r\n");
		return;
	}

	leds[0] = CRGB::Blue;
	leds[1] = CRGB::Blue;
	FastLED.show();
	esp_now_peer_info_t peerInfo;

	memcpy(peerInfo.peer_addr, receiver_address, 6);
	peerInfo.channel = 0;
	peerInfo.encrypt = false;

	printf("Start to adding peer.\r\n");
	if (esp_now_add_peer(&peerInfo) != ESP_OK)
	{
		printf("Failed to add peer.\r\n");
		return;
	}
	printf("Adding peer is OK.\r\n");

	esp_now_register_recv_cb(on_data_recv);

	// Fill ESPNOW struct with values.
	data.id = ESPNOW_ID;
	memcpy(data.local_mac, local_address, 6);
	data.value = 1;
	data.battery_level = int(get_battery_voltage());
	data.single_tap_duration = 1000;

	esp_now_send(receiver_address, (uint8_t *)&data, sizeof(data));

	// wait on espnow answer
	unsigned long t_wait_answer_start = millis();
	while (!espnow_answer_received && millis() <= t_wait_answer_start + 300)
	{
		delayMicroseconds(1);
	}

	if (!espnow_answer_received)
	{
		printf("[WARN] Failed to receive esp now answer.\r\n");
	}

	// This will reduce power consumption.
	WiFi.mode(WIFI_OFF);
	setCpuFrequencyMhz(10);


	delay(500);
}

void set_fastled(CRGB c0)
{
	leds[0] = c0;
	leds[1] = c0;
	FastLED.show();
}

void loop()
{
	leds[0] = CRGB::Blue;
	FastLED.show();
	delay(30);
	leds[0] = CRGB::Black;
	FastLED.show();
	delay(30);
	leds[1] = CRGB::Red;
	FastLED.show();
	delay(30);
	leds[1] = CRGB::Black;
	FastLED.show();
	delay(30);

	float battery_voltage = get_battery_voltage();
	printf("Battery Voltage: %f\r\n", battery_voltage);
	// print mac address
	// String mac_addr = WiFi.macAddress();
	// printf("%s\r\n", mac_addr.c_str());

	// Change color if the button is pressed. After the eight click, generate random color. Press & hold for 1 second to turn off the device.
	// Device will turn off after 10 seconds not pressing the button.
	if (digitalRead(BUTTON_PIN) == 1)
	{
		unsigned long t_pressed = millis();
		while (digitalRead(BUTTON_PIN) == 1)
		{
			delay(10);
			if (millis() > t_pressed + 1000)
			{
				set_fastled(CRGB::Red);
				delay(1000);
				printf("Start to sleep since button is pressed!\r\n");
				esp_deep_sleep_start();
			}
		}
		t_last_press = millis();
	}

	if (millis() - t_last_press > 60000)
	{
		set_fastled(CRGB::Red);
		delay(1000);
		printf("Start to sleep since no button pressed in 60 secs!\r\n");
		esp_deep_sleep_start();
	}
}