#include <DHT.h>

// DHT22 Setup
#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(9600);  // Communication with ESP32
  dht.begin();
  delay(1000);
  Serial.println("Arduino UNO Ready");
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  // Fake PM2.5 reading (random value between 5 and 50)
  int pm25 = random(5, 51);

  // If readings fail, send error values
  if (isnan(temp)) temp = -999;
  if (isnan(hum)) hum = -999;

  // Send to ESP32 in "temp,hum,pm25" format
  Serial.print(temp);
  Serial.print(",");
  Serial.print(hum);
  Serial.print(",");
  Serial.println(pm25);

  delay(30000);  // Send every 30 seconds
}
