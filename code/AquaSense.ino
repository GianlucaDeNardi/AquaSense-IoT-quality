#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <EEPROM.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Pin definitions for the TFT display
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_SCLK 18  // Hardware SPI SCK
#define TFT_MOSI 23  // Hardware SPI MOSI

// Pin for the OneWire temperature sensor
#define ONE_WIRE_BUS 25  // GPIO 25

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// TDS Sensor pin
#define TDS_PIN 32  // GPIO 32

// Wi-Fi credentials
const char* ssid = "IoT Test";
const char* pass = "8febfa78";
const char* server = "api.thingspeak.com";
String apiKey = "JENFFVV7CKMGQ188";

WiFiClient client;

// Initialize the display
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

float temperature = 25.0;  // Store the temperature for compensation
float tdsValue = 0;  // Store the TDS value

const static char *TAG = "EXAMPLE";

#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_4
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_11

static int adc_raw;
static int voltage;
static bool do_calibration1_chan0;
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_chan0_handle = NULL;

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

// Funzione per mappare il valore letto dall'ADC al nuovo intervallo
float map_voltage(float voltage) {
    float min_input = 0.145;
    float max_input = 3.145;
    float min_output = 0.0;
    float max_output = 3.285;

    if (voltage >= min_input) {
      if (voltage <= max_input){
        return voltage;
      }
      else {
        return (voltage - min_input) * (max_output - min_output) / (max_input - min_input) + min_output;
      }
    } else {
        float voltage1 = (voltage - min_input) * (max_output - min_output) / (max_input - min_input) + min_output;
        return round(voltage1 * 100) / 100.0;
    }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(32);  // Initialize EEPROM

  // Initialize the display
  tft.initR(INITR_BLACKTAB);  // Initialize with the correct tab
  tft.setRotation(1);  // Set rotation, adjust as needed

  // Fill the screen with a color to check if the display works
  tft.fillScreen(ST7735_BLACK);

  // Set text color to white and size to 1.5
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1.5);

  // Print a message to the display to ensure it works
  tft.setCursor(10, 10);
  tft.print("Initializing...");

  // Initialize the temperature sensor
  sensors.begin();

  // Connect to Wi-Fi
  Serial.println("Connecting to WiFi...");
  tft.setCursor(10, 20);
  tft.print("Connecting to WiFi...");
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  tft.fillRect(10, 20, 128, 10, ST7735_BLACK);  // Clear WiFi message
  tft.setCursor(10, 20);
  tft.print("WiFi connected");

  // Print initialization complete message
  Serial.println("Initialization complete.");
  tft.fillRect(10, 10, 128, 10, ST7735_BLACK);  // Clear the initial message
  tft.setCursor(10, 10);
  tft.print("Temp: ");

  //-------------ADC1 Init---------------//
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
      .atten = EXAMPLE_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

  //-------------ADC1 Calibration Init---------------//
  do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);
}

void loop() {
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);

  // Leggi il valore ADC raw e calcola la tensione
  ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw));
  Serial.printf("ADC%d Channel[%d] Raw Data: %d\n", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw);
  float mapped_voltage = 0.0; // Dichiarazione di mapped_voltage
  if (do_calibration1_chan0) {
      ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, &voltage));
      float voltage_float = voltage / 1000.0; // convert mV to V
      mapped_voltage = map_voltage(voltage_float);
      Serial.printf("ADC%d Channel[%d] Cali Voltage: %.3f V, Mapped Voltage: %.3f V\n", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage_float, mapped_voltage);
  }

  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = mapped_voltage / compensationCoefficient;
  tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
              - 255.86 * compensationVoltage * compensationVoltage
              + 857.39 * compensationVoltage) * 0.5;

  // Update the display with temperature and TDS values
  tft.fillRect(10, 30, 128, 20, ST7735_BLACK);  // Clear the area where the temperature will be displayed
  tft.fillRect(10, 50, 128, 20, ST7735_BLACK);  // Clear the area where the TDS value will be displayed

  // Display the temperature
  tft.setCursor(10, 30);
  tft.setTextSize(1.5);  // Set text size to 1.5 for better visibility
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);  // Set text color to white with black background to clear old text
  tft.print("Temp: ");
  tft.print(temperature, 1);  // Display temperature with 1 decimal place
  tft.print(" C");

  // Display the TDS value
  tft.setCursor(10, 50);
  tft.setTextSize(1);
  tft.print("TDS: ");
  tft.print(tdsValue, 1);  // Display TDS value with 1 decimal place
  tft.print(" ppm");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("TDS Value: ");
  Serial.print(tdsValue);
  Serial.println(" ppm");

  // Send data to ThingSpeak
  if (client.connect(server, 80)) {
    String postStr = apiKey;
    postStr += "&field1=";
    postStr += String(temperature);
    postStr += "&field2=";
    postStr += String(tdsValue);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);

    delay(500);
  }
  client.stop();

  delay(2000);  // Update every 2 seconds
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        Serial.printf("calibration scheme version is %s\n", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        Serial.printf("calibration scheme version is %s\n", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        Serial.printf("Calibration Success\n");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        Serial.printf("eFuse not burnt, skip software calibration\n");
    } else {
        Serial.printf("Invalid arg or no memory\n");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    Serial.printf("deregister %s calibration scheme\n", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    Serial.printf("deregister %s calibration scheme\n", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
