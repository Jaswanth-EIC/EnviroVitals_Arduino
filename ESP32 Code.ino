#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "EnviroVitals_Cough_v2_inferencing.h"  // Your Edge Impulse model

// === WiFi Settings ===
const char* ssid = "Linksys01064";
const char* password = "dx6mffeehf";
const char* apiKey = "J8ZNJDGTR92SJ4RB";  // ThingSpeak API key

// === ThingSpeak Channel Field Mapping ===
// Field1: Test Value
// Field2: Temperature
// Field3: Humidity
// Field4: Cough
// Field5: PM2.5

// === I2S Mic (INMP441) ===
#define I2S_WS   25
#define I2S_SCK  26
#define I2S_SD   33

// === Serial From UNO ===
#define RXD2 16
#define TXD2 17

WiFiClient client;
String temperature = "0";
String humidity = "0";
String pm25 = "0";
int testValue = 1;

void setupMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("🔌 Booting...");
  WiFi.begin(ssid, password);
  Serial.print("🔗 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\n✅ WiFi connected.");

  Serial.println("🔧 Setting up microphone...");
  setupMic();
  Serial.println("🎙️ Microphone setup complete.");

  Serial.println("✅ Setup complete. Starting loop...");
}

void loop() {
  // === 1. Read data from UNO ===
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();

    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      temperature = line.substring(0, firstComma);
      humidity = line.substring(firstComma + 1, secondComma);
      pm25 = line.substring(secondComma + 1);

      Serial.println("🌡️ Temp: " + temperature + "°C | 💧 Humidity: " + humidity + "% | 🌫️ PM2.5: " + pm25 + " µg/m³");
    }
  }

  // === 2. Record audio from INMP441 ===
  signal_t signal;
  ei_impulse_result_t result = {0};
  bool success = microphone_inference_record(&signal);

  int coughDetected = 0;
  if (success && run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      Serial.printf("🔍 %s: %.3f\n", result.classification[i].label, result.classification[i].value);
      if (String(result.classification[i].label) == "cough" && result.classification[i].value > 0.8) {
        coughDetected = 1;
        Serial.println("💨 Cough detected!");
      }
    }
  }

  // === 3. Send to ThingSpeak ===
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=" + String(apiKey) +
                 "&field1=" + String(testValue++) +
                 "&field2=" + temperature +
                 "&field3=" + humidity +
                 "&field4=" + String(coughDetected) +
                 "&field5=" + pm25;

    http.begin(url);
    int httpCode = http.GET();
    http.end();

    Serial.println(httpCode == 200 ? "📤 ThingSpeak update success." : "❌ ThingSpeak update failed.");
  }

  delay(30000);  // Every 30 seconds
}

// === Audio capture ===
bool microphone_inference_record(signal_t* signal) {
  static int16_t samples[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
  size_t bytes_read;
  esp_err_t res = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);

  if (res != ESP_OK || bytes_read == 0) return false;

  static float signal_data[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
  for (size_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
    signal_data[i] = (float)samples[i] / 32768.0f;
  }

  signal->total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal->get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
    for (size_t i = 0; i < length; i++) {
      out_ptr[i] = signal_data[offset + i];
    }
    return 0;
  };

  return true;
}
