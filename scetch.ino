#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// ============ SOZLAMALAR ============

// WiFi — Wokwi bepul WiFi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT Broker
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// MQTT Topiclar
#define TOPIC_SENSORS       "smart_home/uz/sensors"
#define TOPIC_LIGHT_STATUS  "smart_home/uz/light/status"
#define TOPIC_LIGHT_CONTROL "smart_home/uz/light/control"
#define TOPIC_DOOR_CONTROL  "smart_home/uz/door/control"
#define TOPIC_DOOR_STATUS   "smart_home/uz/door/status"
#define TOPIC_MOTION        "smart_home/uz/motion"
#define TOPIC_SYSTEM        "smart_home/uz/system"

// Pinlar
#define DHTPIN      4
#define DHTTYPE     DHT22
#define RELAY_PIN   26
#define LED_PIN     2
#define PIR_PIN     14
#define LDR_PIN     34
#define BUZZER_PIN  27
#define SERVO_PIN   13

// ============ OBYEKTLAR ============
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqtt(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

// ============ O'ZGARUVCHILAR ============
bool lightState = false;
bool doorLocked = true;
bool motionDetected = false;
unsigned long lastSensorRead = 0;
unsigned long lastLCDSwitch = 0;
const long sensorInterval = 3000;
int lcdPage = 0;

float currentTemp = 0;
float currentHum = 0;
int currentLight = 0;

// LCD maxsus belgilar
byte thermIcon[8] = {0x04,0x0A,0x0A,0x0E,0x1F,0x1F,0x1F,0x0E};
byte dropIcon[8]  = {0x04,0x04,0x0A,0x0A,0x11,0x11,0x11,0x0E};
byte lockIcon[8]  = {0x0E,0x11,0x11,0x1F,0x1B,0x1B,0x1F,0x00};
byte homeIcon[8]  = {0x04,0x0E,0x1F,0x11,0x15,0x15,0x1F,0x00};
byte wifiIcon[8]  = {0x00,0x0E,0x11,0x04,0x0A,0x00,0x04,0x00};
byte sunIcon[8]   = {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00,0x00};
byte moonIcon[8]  = {0x00,0x0C,0x10,0x10,0x10,0x0C,0x00,0x00};
byte bellIcon[8]  = {0x04,0x0E,0x0E,0x0E,0x1F,0x00,0x04,0x00};

// ============ WiFi ULANISH ============
void connectWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(4); // wifi icon
  lcd.print(" WiFi...");
  
  Serial.print("WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  int dots = 0;
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(dots % 16, 1);
    lcd.print(".");
    dots++;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  Serial.println("\n✅ WiFi OK! IP: " + WiFi.localIP().toString());
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(4);
  lcd.print(" WiFi OK!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  
  digitalWrite(LED_PIN, HIGH);
}

// ============ MQTT CALLBACK ============
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("📩 [%s]: %s\n", topic, message.c_str());
  
  // 💡 Chiroq boshqaruvi
  if (String(topic) == TOPIC_LIGHT_CONTROL) {
    if (message == "ON") {
      lightState = true;
    } else if (message == "OFF") {
      lightState = false;
    } else if (message == "TOGGLE") {
      lightState = !lightState;
    }
    
    digitalWrite(RELAY_PIN, lightState ? HIGH : LOW);
    mqtt.publish(TOPIC_LIGHT_STATUS, lightState ? "ON" : "OFF");
    
    // Ovozli signal
    tone(BUZZER_PIN, lightState ? 2000 : 1000, 100);
    
    Serial.printf("💡 Chiroq: %s\n", lightState ? "YOQILDI" : "O'CHIRILDI");
  }
  
  // 🔒 Eshik boshqaruvi
  if (String(topic) == TOPIC_DOOR_CONTROL) {
    if (message == "LOCK") {
      doorLocked = true;
      doorServo.write(0);     // Qulflash
    } else if (message == "UNLOCK") {
      doorLocked = false;
      doorServo.write(90);    // Ochish
    } else if (message == "TOGGLE") {
      doorLocked = !doorLocked;
      doorServo.write(doorLocked ? 0 : 90);
    }
    
    mqtt.publish(TOPIC_DOOR_STATUS, doorLocked ? "LOCKED" : "UNLOCKED");
    
    // Ovozli signal
    if (doorLocked) {
      tone(BUZZER_PIN, 800, 200);
    } else {
      tone(BUZZER_PIN, 1200, 100);
      delay(150);
      tone(BUZZER_PIN, 1500, 100);
    }
    
    Serial.printf("🔒 Eshik: %s\n", doorLocked ? "QULFLANDI" : "OCHILDI");
  }
}

// ============ MQTT ULANISH ============
void connectMQTT() {
  while (!mqtt.connected()) {
    lcd.clear();
    lcd.print("MQTT ulanmoqda");
    Serial.print("MQTT...");
    
    String clientId = "SmartHome_" + String(random(0xffff), HEX);
    
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" ✅ OK!");
      
      // Obuna bo'lish
      mqtt.subscribe(TOPIC_LIGHT_CONTROL);
      mqtt.subscribe(TOPIC_DOOR_CONTROL);
      
      // Online status
      mqtt.publish(TOPIC_SYSTEM, "ONLINE");
      mqtt.publish(TOPIC_LIGHT_STATUS, lightState ? "ON" : "OFF");
      mqtt.publish(TOPIC_DOOR_STATUS, doorLocked ? "LOCKED" : "UNLOCKED");
      
      lcd.clear();
      lcd.print("MQTT OK!");
      tone(BUZZER_PIN, 2000, 200);
      delay(1000);
      
    } else {
      Serial.printf(" ❌ Xato (rc=%d)\n", mqtt.state());
      lcd.setCursor(0, 1);
      lcd.print("Xato! 5s...");
      delay(5000);
    }
  }
}

// ============ SENSOR O'QISH VA YUBORISH ============
void publishSensorData() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int ldr = analogRead(LDR_PIN);
  int lightPct = map(ldr, 0, 4095, 0, 100);
  
  if (isnan(temp) || isnan(hum)) {
    Serial.println("⚠️ DHT xato!");
    return;
  }
  
  currentTemp = temp;
  currentHum = hum;
  currentLight = lightPct;
  
  // JSON tayyorlash
  StaticJsonDocument<300> doc;
  doc["temperature"]  = round(temp * 10.0) / 10.0;
  doc["humidity"]     = round(hum * 10.0) / 10.0;
  doc["light_level"]  = lightPct;
  doc["light_state"]  = lightState ? "ON" : "OFF";
  doc["door_state"]   = doorLocked ? "LOCKED" : "UNLOCKED";
  doc["motion"]       = motionDetected;
  doc["wifi_rssi"]    = WiFi.RSSI();
  doc["uptime"]       = millis() / 1000;
  
  char json[300];
  serializeJson(doc, json);
  
  mqtt.publish(TOPIC_SENSORS, json);
  Serial.printf("📤 %s\n", json);
}

// ============ HARAKAT TEKSHIRISH ============
void checkMotion() {
  bool current = digitalRead(PIR_PIN);
  
  if (current && !motionDetected) {
    motionDetected = true;
    mqtt.publish(TOPIC_MOTION, "DETECTED");
    Serial.println("🚶 Harakat aniqlandi!");
    
    // Ogohlantirish
    tone(BUZZER_PIN, 1500, 300);
  } 
  else if (!current && motionDetected) {
    motionDetected = false;
    mqtt.publish(TOPIC_MOTION, "CLEAR");
    Serial.println("✅ Harakat to'xtadi");
  }
}

// ============ LCD YANGILASH ============
void updateLCD() {
  unsigned long now = millis();
  
  // Har 4 sekundda sahifa almashtirish
  if (now - lastLCDSwitch >= 4000) {
    lastLCDSwitch = now;
    lcdPage = (lcdPage + 1) % 3;
    lcd.clear();
  }
  
  switch (lcdPage) {
    case 0: // Harorat va Namlik
      lcd.setCursor(0, 0);
      lcd.write(0); // therm
      lcd.print(" Harorat:");
      lcd.setCursor(0, 1);
      lcd.print("  ");
      lcd.print(currentTemp, 1);
      lcd.print((char)223); // degree
      lcd.print("C  ");
      lcd.write(1); // drop
      lcd.print(currentHum, 0);
      lcd.print("%");
      break;
      
    case 1: // Chiroq va Eshik
      lcd.setCursor(0, 0);
      lcd.write(5); // sun
      lcd.print(" Chiroq: ");
      lcd.print(lightState ? "ON " : "OFF");
      lcd.setCursor(0, 1);
      lcd.write(2); // lock
      lcd.print(" Eshik: ");
      lcd.print(doorLocked ? "QULF" : "OCHIQ");
      break;
      
    case 2: // Harakat va WiFi
      lcd.setCursor(0, 0);
      lcd.write(7); // bell
      lcd.print(" Harakat: ");
      lcd.print(motionDetected ? "BOR!" : "YO'Q");
      lcd.setCursor(0, 1);
      lcd.write(4); // wifi
      lcd.print(" WiFi: ");
      lcd.print(WiFi.RSSI());
      lcd.print("dBm");
      break;
  }
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  Serial.println("\n🏠 === SMART HOME SYSTEM ===\n");
  
  // Pin sozlash
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT_PULLDOWN);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  
  // Servo
  doorServo.attach(SERVO_PIN);
  doorServo.write(0); // Boshlang'ich: qulflangan
  
  // LCD
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, thermIcon);
  lcd.createChar(1, dropIcon);
  lcd.createChar(2, lockIcon);
  lcd.createChar(3, homeIcon);
  lcd.createChar(4, wifiIcon);
  lcd.createChar(5, sunIcon);
  lcd.createChar(6, moonIcon);
  lcd.createChar(7, bellIcon);
  
  // Salomlash
  lcd.setCursor(0, 0);
  lcd.write(3); // home
  lcd.print(" SMART HOME");
  lcd.setCursor(0, 1);
  lcd.print("  v1.0 - ESP32");
  
  // Melodiya
  tone(BUZZER_PIN, 523, 150); delay(200);
  tone(BUZZER_PIN, 659, 150); delay(200);
  tone(BUZZER_PIN, 784, 200); delay(300);
  
  delay(2000);
  
  // DHT
  dht.begin();
  
  // WiFi & MQTT
  connectWiFi();
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  connectMQTT();
  
  Serial.println("✅ Sistema tayyor!\n");
}

// ============ LOOP ============
void loop() {
  // Ulanishlarni tekshirish
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  // Harakat sensori
  checkMotion();
  
  // Sensorlar
  unsigned long now = millis();
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    publishSensorData();
  }
  
  // LCD
  updateLCD();
}