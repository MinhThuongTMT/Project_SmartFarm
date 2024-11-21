#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <EEPROM.h>


const char* mqtt_server = "485dac0774f84a84af21f6b392c46069.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "tu_mqtt";
const char* mqtt_password = "#30032003Tu";

#define EEPROM_SIZE 10
#define sensor 34  
#define wet 210

WebServer server(80);  

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);
DHTesp dht;
  
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

#define relay1 13
#define relay2 12
#define relay3 14
#define DO_PIN 27
#define DHT_PIN 4

bool relay1State = false;
bool relay2State = false;
bool relay3State = false;

float temperature = 0;
float humidity = 0;
unsigned long previousMillis = 0;
int displayMode = 0;
unsigned long eepromUpdateMillis = 0;

enum Mode { MANUAL, AUTO, SCHEDULE };
Mode currentMode = MANUAL;

struct Schedule {
    int hour;
    int minute;
};

Schedule relay1Schedule = {0, 0};
Schedule relay2Schedule = {0, 0};
Schedule relay3Schedule = {0, 0};

Schedule relay1OffSchedule = {0, 0};
Schedule relay2OffSchedule = {0, 0};
Schedule relay3OffSchedule = {0, 0};

int pumpStartTime, pumpEndTime, pumpStartMinute, pumpEndMinute;
int fanStartTime, fanEndTime, fanStartMinute, fanEndMinute;
int lightStartTime, lightEndTime, lightStartMinute, lightEndMinute;
bool scheduleMode = false;

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);  
    lcd.begin();
    lcd.backlight();
    lcd.clear();
    
    pinMode(relay1, OUTPUT);
    pinMode(relay2, OUTPUT);
    pinMode(relay3, OUTPUT);
    pinMode(DO_PIN, INPUT);

    EEPROM.begin(EEPROM_SIZE);
    readStateFromEEPROM();

    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    WiFiManager wifiManager;
    if (!wifiManager.autoConnect("ESP32_AP")) {
        Serial.println("Failed to connect WiFi, restarting...");
        delay(3000);
        ESP.restart();
    }

    dht.setup(DHT_PIN, DHTesp::DHT11);
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // In địa chỉ IP lên LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IP Address: ");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());  // Hiển thị địa chỉ IP

    timeClient.begin();
    espClient.setInsecure();
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);

    // Đăng ký các đường dẫn cho máy chủ HTTP
    server.on("/index.html", HTTP_GET, handleIndex);
    server.on("/style.css", HTTP_GET, handleStyles);
    server.on("/services.js", HTTP_GET, handleScript2);
    server.on("/dark-mode-service.js", HTTP_GET, handleScript3);
    server.on("/motor", HTTP_GET, handleRelay1);
    server.on("/fan", HTTP_GET, handleRelay2);
    server.on("/lamp", HTTP_GET, handleRelay3);
    server.on("/motor/status", HTTP_GET, handleRelay1Status);
    server.on("/fan/status", HTTP_GET, handleRelay2Status);
    server.on("/lamp/status", HTTP_GET, handleRelay3Status);
    server.on("/temperature", HTTP_GET, handleTemperatureHumidity); 
    server.on("/soil", HTTP_GET, handleSoilData);                
    server.on("/rain", HTTP_GET, handleRainData); 
    server.on("/checkScheduleMode", HTTP_GET, handleSetScheduleMode);
    server.on("/setMode", HTTP_GET, []() {
        String mode = server.arg("mode"); // Lấy chế độ từ yêu cầu
        if (mode == "AUTO") {
            currentMode = AUTO;
        } else if (mode == "MANUAL") {
            currentMode = MANUAL;
        } else if (mode == "SCHEDULE") {
            currentMode = SCHEDULE;
        }

        saveStateToEEPROM();  

        server.send(200, "text/plain", "Chế độ đã được thay đổi thành: " + mode);
        Serial.print("Chế độ đã được thay đổi thành: ");
        Serial.println(mode);
    });
    
    server.on("/", HTTP_GET, handleIndex);
    server.on("/setTimer", HTTP_GET, handleSetTimer);
    server.on("/auto_off.png", HTTP_GET, auto_off);
    server.on("/auto_on.png", HTTP_GET, auto_on);
    server.on("/fan_off.png", HTTP_GET, fan_off);
    server.on("/fan_on.png", HTTP_GET, fan_on);
    server.on("/lamp_off.png", HTTP_GET, lamp_off);
    server.on("/lamp_on.png", HTTP_GET, lamp_on);
    server.on("/motor_off.png", HTTP_GET, motor_off);
    server.on("/motor_on.png", HTTP_GET, motor_on);
    server.on("/rain.png", HTTP_GET, handlerain);
    server.on("/sun.png", HTTP_GET, sun);
    server.on("/soil.png", HTTP_GET, soil);
    server.on("/temperature.png", HTTP_GET, temperature_pic);
    server.on("/t1_logo.jpg", HTTP_GET, t1_logo);
    server.on("/T1logo_square.webp", HTTP_GET, T1logo_square);

    server.begin();
    Serial.println("HTTP server started");

    digitalWrite(relay1, relay1State ? HIGH : LOW);
    digitalWrite(relay2, relay2State ? HIGH : LOW);
    digitalWrite(relay3, relay3State ? HIGH : LOW);
}

void loop() {
    timeClient.update();
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();
    timeClient.update();
    server.handleClient();
    readEnvironment(temperature);

    lcd_theme();
    soil_sensor();
    rain();

    switch (currentMode) {
        case AUTO:
            autoControlRelay1();
            autoControlRelay2();
            break;
        case SCHEDULE:
            scheduleControl();
            handleScheduledTasks(); 
            break;
        case MANUAL:
            controlRelay1();
            controlRelay2();
            controlRelay3();
            break;
        default:
            break;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - eepromUpdateMillis >= 60000) {
        eepromUpdateMillis = currentMillis;
        saveStateToEEPROM();
    }
    Serial.println(currentMode == AUTO ? "Tự động" : (currentMode == MANUAL ? "Thủ công" : "Lịch trình"));
    delay(500);
}

void saveStateToEEPROM() {
    EEPROM.write(0, currentMode);
    EEPROM.write(1, relay1State);
    EEPROM.write(2, relay2State);
    EEPROM.write(3, relay3State);
    EEPROM.commit();
    Serial.println("State saved to EEPROM");
}

void readStateFromEEPROM() {
    currentMode = (Mode)EEPROM.read(0);
    relay1State = EEPROM.read(1);
    relay2State = EEPROM.read(2);
    relay3State = EEPROM.read(3);
    
    
    digitalWrite(relay1, relay1State);
    digitalWrite(relay2, relay2State);
    digitalWrite(relay3, relay3State);
    Serial.println("State read from EEPROM");
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);

    message.trim();
    if (String(topic) == "garden/mode") {
        if (message == "AUTO") {
            currentMode = AUTO;
        } else if (message == "MANUAL") {
            currentMode = MANUAL;
        } else if (message == "SCHEDULE") {
            currentMode = SCHEDULE;
        }
        
        saveStateToEEPROM();  // Lưu trạng thái sau khi thay đổi chế độ
        Serial.print("Chế độ thay đổi: ");
        Serial.println(currentMode == AUTO ? "Tự động" : (currentMode == MANUAL ? "Thủ công" : "Lịch trình"));
    }
    if (String(topic) == "garden/get_mode") {
      String modeMessage = (currentMode == AUTO) ? "AUTO" : (currentMode == MANUAL ? "MANUAL" : "SCHEDULE");
      mqttClient.publish("garden/current_mode", modeMessage.c_str());
      Serial.print("Phản hồi chế độ hiện tại: ");
      Serial.println(modeMessage);
}
    if (currentMode == MANUAL) {
        if (String(topic) == "garden/motor") {
            relay1State = (message == "ON");
            digitalWrite(relay1, relay1State ? HIGH : LOW);
            Serial.print("Relay 1 đã được bật ");
            Serial.println(relay1State ? "ON" : "OFF");
        }
    }
    
    if (String(topic) == "garden/fan") {
        relay2State = (message == "ON"); // Update relay2State
        digitalWrite(relay2, relay2State ? HIGH : LOW);
        Serial.print("Relay 2 has been turned ");
        Serial.println(relay2State ? "ON" : "OFF");
    }
    if (String(topic) == "garden/lamp") {
        relay3State = (message == "ON"); // Update relay2State
        digitalWrite(relay3, relay3State ? HIGH : LOW);
        Serial.print("Relay 3 has been turned ");
        Serial.println(relay3State ? "ON" : "OFF");
    }
    if (String(topic) == "garden/schedule/motor") {
        parseSchedule(message, relay1Schedule);
    }
    if (String(topic) == "garden/schedule/fan") {
        parseSchedule(message, relay2Schedule);
    }
    if (String(topic) == "garden/schedule/lamp") {
        parseSchedule(message, relay3Schedule);
    }
    if (String(topic) == "garden/schedule/motor_off") {
        parseSchedule(message, relay1OffSchedule);
    }
    if (String(topic) == "garden/schedule/fan_off") {
        parseSchedule(message, relay2OffSchedule);
    }
    if (String(topic) == "garden/schedule/lamp_off") {
        parseSchedule(message, relay3OffSchedule);
    }
}

void reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESP8266Client-";
        clientId += String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("connected");
            mqttClient.subscribe("garden/motor");
            mqttClient.subscribe("garden/fan");
            mqttClient.subscribe("garden/lamp"); 
            mqttClient.subscribe("garden/mode"); 
            mqttClient.subscribe("garden/schedule/motor");
            mqttClient.subscribe("garden/schedule/fan");
            mqttClient.subscribe("garden/schedule/lamp");
            mqttClient.subscribe("garden/schedule/motor_off");
            mqttClient.subscribe("garden/schedule/fan_off");
            mqttClient.subscribe("garden/schedule/lamp_off");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }

    }
}
// cảm biến DHT11
void readEnvironment(float &temperature) {
    temperature = dht.getTemperature();
    mqttClient.publish("garden/temperature", String(temperature).c_str());

    if (isnan(temperature)) {
        Serial.println(F("Failed to read temperature from DHT sensor!"));
        temperature = 33; // Giá trị mặc định nếu đọc thất bại
    }
}


void soil_sensor() {
    int value = analogRead(sensor);
    int moisturePercentage = map(value, wet, 4095, 100, 0);
    Serial.print("Soil Moisture (%): ");
    Serial.println(moisturePercentage);
    mqttClient.publish("garden/soil", String(moisturePercentage).c_str());
}
void lcd_theme() {
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= (displayMode == 0 ? 8000 : 15000)) {
        previousMillis = currentMillis;
        displayMode = !displayMode; 
    }
    
    lcd.clear();
    if (displayMode == 0) {
        int hours = hour(timeClient.getEpochTime());
        int minutes = minute(timeClient.getEpochTime());
        int seconds = second(timeClient.getEpochTime());
        int days = day(timeClient.getEpochTime());
        int months = month(timeClient.getEpochTime());
        int years = year(timeClient.getEpochTime());

        String timeString = (hours < 10 ? "0" : "") + String(hours) + ":" +
                            (minutes < 10 ? "0" : "") + String(minutes) + ":" +
                            (seconds < 10 ? "0" : "") + String(seconds);
                            
        String dateString = (days < 10 ? "0" : "") + String(days) + "/" +
                            (months < 10 ? "0" : "") + String(months) + "/" +
                            String(years);

        lcd.setCursor(0, 0);
        lcd.print("Gio:  " + timeString);
        lcd.setCursor(0, 1);
        lcd.print("Ngay: " + dateString);
    } else {
        lcd.setCursor(0, 0);
        lcd.print("Nhiet do: ");
        lcd.print(temperature, 1);
        lcd.print((char)223);
        lcd.print("C");

        lcd.setCursor(0, 1);
        lcd.print("Do am dat: ");
        
        int soilMoistureValue = analogRead(sensor);
        int soilMoisturePercentage = map(soilMoistureValue, wet, 4095, 100, 0);
        
        lcd.print(soilMoisturePercentage);
        lcd.print("%");
    }
}

void controlRelay1() {
    digitalWrite(relay1, relay1State ? HIGH : LOW);
    Serial.print("Relay 1 set to: ");
    Serial.println(relay1State ? "ON" : "OFF");
}
void autoControlRelay1() {
    int moisture = analogRead(sensor);
    int moisturePercentage = map(moisture, wet, 4095, 100, 0);
    
    Serial.print("Soil Moisture (%): ");
    Serial.println(moisturePercentage);

    if (moisturePercentage < 50 && !relay1State) {
        relay1State = true;
        digitalWrite(relay1, HIGH);
        Serial.println("Relay 1 activated (Motor)");
    }
    else if (moisturePercentage > 75 && relay1State) {
        relay1State = false;
        digitalWrite(relay1, LOW);
        Serial.println("Relay 1 deactivated (Motor)");
    }
}

void controlRelay2() {
    digitalWrite(relay2, relay2State ? HIGH : LOW);
    Serial.print("Relay 2 set to: ");
    Serial.println(relay2State ? "ON" : "OFF");
}
void autoControlRelay2() {
    if (temperature >= 35 && !relay2State) {
        relay2State = true;
        digitalWrite(relay2, HIGH);
        Serial.println("Relay 2 activated (Fan)");
    } else if (temperature < 29 && relay2State) {
        relay2State = false;
        digitalWrite(relay2, LOW);
        Serial.println("Relay 2 deactivated (Fan)");
    }
}
void controlRelay3() {
    digitalWrite(relay3, relay3State ? HIGH : LOW);
    Serial.print("Relay 3 set to: ");
    Serial.println(relay3State ? "ON" : "OFF");
}
void parseSchedule(const String& message, Schedule &schedule) {
    int separatorIndex = message.indexOf(':');
    if (separatorIndex != -1) {
        String timeString = message.substring(separatorIndex + 1);
        int hour = timeString.substring(0, 2).toInt();
        int minute = timeString.substring(3, 5).toInt();
        if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
            schedule.hour = hour;
            schedule.minute = minute;
            Serial.print("Scheduled time set to: ");
            Serial.print(schedule.hour);
            Serial.print(":");
            Serial.println(schedule.minute);
        } else {
            Serial.println("Invalid time format.");
        }
    }
}

void scheduleControl() {
    unsigned long currentMillis = millis();
    int currentHour = hour(timeClient.getEpochTime());
    int currentMinute = minute(timeClient.getEpochTime());

    // Kiểm tra Relay 1
    if (currentHour == relay1Schedule.hour && currentMinute == relay1Schedule.minute && !relay1State) {
        relay1State = true;
        digitalWrite(relay1, HIGH);
        Serial.println("Relay 1 ON");
    } else if (currentHour == relay1OffSchedule.hour && currentMinute == relay1OffSchedule.minute && relay1State) {
        relay1State = false;
        digitalWrite(relay1, LOW);
        Serial.println("Relay 1 OFF");
    }

    // Kiểm tra Relay 2
    if (currentHour == relay2Schedule.hour && currentMinute == relay2Schedule.minute && !relay2State) {
        relay2State = true;
        digitalWrite(relay2, HIGH);
        Serial.println("Relay 2 ON");
    } else if (currentHour == relay2OffSchedule.hour && currentMinute == relay2OffSchedule.minute && relay2State) {
        relay2State = false;
        digitalWrite(relay2, LOW);
        Serial.println("Relay 2 OFF");
    }

    // Kiểm tra Relay 3
    if (currentHour == relay3Schedule.hour && currentMinute == relay3Schedule.minute && !relay3State) {
        relay3State = true;
        digitalWrite(relay3, HIGH);
        Serial.println("Relay 3 ON");
    } else if (currentHour == relay3OffSchedule.hour && currentMinute == relay3OffSchedule.minute && relay3State) {
        relay3State = false;
        digitalWrite(relay3, LOW);
        Serial.println("Relay 3 OFF");
    }
}


void rain() {
    int rainValue = digitalRead(DO_PIN);
    if (rainValue == HIGH) {
      Serial.println("Trời nắng");
      mqttClient.publish("garden/rainsensor", "Nắng chói chang");
    }else {
      Serial.println("Trời mưa");
      mqttClient.publish("garden/rainsensor", "Mưa hoài....");
    }
    if (rainValue == LOW) {
        relay1State = false;
        digitalWrite(relay1, LOW);
        Serial.println("Rain detected! All relays turned off.");
    }

}

void handleRelay1() {
    String action = server.arg("action"); // Lấy tham số action từ request
    if (action == "ON") {
        relay1State = true;
    } else if (action == "OFF") {
        relay1State = false;
    }
    digitalWrite(relay1, relay1State ? HIGH : LOW);
    server.send(200, "text/plain", "Relay 1 is now " + String(relay1State ? "ON" : "OFF"));
}

void handleRelay2() {
    String action = server.arg("action"); // Lấy tham số action từ request
    if (action == "ON") {
        relay2State = true;
    } else if (action == "OFF") {
        relay2State = false;
    }
    digitalWrite(relay2, relay2State ? HIGH : LOW);
    server.send(200, "text/plain", "Relay 2 is now " + String(relay2State ? "ON" : "OFF"));
}

void handleRelay3() {
    String action = server.arg("action"); // Lấy tham số action từ request
    if (action == "ON") {
        relay3State = true;
    } else if (action == "OFF") {
        relay3State = false;
    }
    digitalWrite(relay3, relay3State ? HIGH : LOW);
    server.send(200, "text/plain", "Relay 3 is now " + String(relay3State ? "ON" : "OFF"));
}

void handleTemperatureHumidity() {
    String response = "{\"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";
    server.send(200, "application/json", response);
}


void handleSoilData() {
    int soilMoistureValue = analogRead(sensor);
    int soilMoisturePercentage = map(soilMoistureValue, wet, 4095, 100, 0);
    String response = "{\"soilMoisture\": " + String(soilMoisturePercentage) + "}";
    server.send(200, "application/json", response);
}


void handleRainData() {
    int rain_state = digitalRead(DO_PIN);
    String jsonResponse = "{\"rain_state\": " + String(rain_state) + "}";
    server.send(200, "application/json", jsonResponse);
}

void handleRelay1Status() {
  String status = (digitalRead(relay1) == HIGH) ? "ON" : "OFF";
  server.send(200, "text/plain", status);
}
void handleRelay2Status() {
  String status = (digitalRead(relay2) == HIGH) ? "ON" : "OFF";
  server.send(200, "text/plain", status);
}
void handleRelay3Status() {
  String status = (digitalRead(relay3) == HIGH) ? "ON" : "OFF";
  server.send(200, "text/plain", status);
}

void handleIndex() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}
void handleStyles() {
  File file = LittleFS.open("/style.css", "r");
  if (!file) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "text/css");
  file.close();
}


void handleScript2() {
  File file = LittleFS.open("/services.js", "r");
  if (!file) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}
void handleScript3() {
  File file = LittleFS.open("/dark-mode-service.js", "r");
  if (!file) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}

void auto_off() {
    File auto_off = LittleFS.open("/auto_off.png", "r");
    if (!auto_off){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(auto_off, "image/png");
    auto_off.close();
}
void auto_on() {
    File auto_on = LittleFS.open("/auto_on.png", "r");
    if (!auto_on){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(auto_on, "image/png");
    auto_on.close();
}
void fan_off() {
    File fan_off = LittleFS.open("/fan_off.png", "r");
    if (!fan_off){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(fan_off, "image/png");
    fan_off.close();
}
void fan_on() {
    File fan_on = LittleFS.open("/fan_on.png", "r");
    if (!fan_on){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(fan_on, "image/png");
    fan_on.close();
}
void  t1_logo() {
    File t1_logo = LittleFS.open("/t1_logo.jpg", "r");
    if (!t1_logo){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(t1_logo, "image/jpg");
    t1_logo.close();
}
void  T1logo_square() {
    File T1logo_square = LittleFS.open("/T1logo_square.webp", "r");
    if (!T1logo_square){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(T1logo_square, "image/webp");
    T1logo_square.close();
}
void lamp_off() {
    File lamp_off = LittleFS.open("/lamp_off.png", "r");
    if (!lamp_off){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(lamp_off, "image/png");
    lamp_off.close();
}
void lamp_on() {
    File lamp_on = LittleFS.open("/lamp_on.png", "r");
    if (!lamp_on){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(lamp_on, "image/png");
    lamp_on.close();
}
void motor_off() {
    File motor_off = LittleFS.open("/motor_off.png", "r");
    if (!motor_off){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(motor_off, "image/png");
    motor_off.close();
}
void motor_on() {
    File motor_on = LittleFS.open("/motor_on.png", "r");
    if (!motor_on){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(motor_on, "image/png");
    motor_on.close();
}
void handlerain() {
    File rain = LittleFS.open("/rain.png", "r");
    if (!rain){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(rain, "image/png");
    rain.close();
}
void sun() {
    File sun = LittleFS.open("/sun.png", "r");
    if (!sun){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(sun, "image/png");
    sun.close();
}
void temperature_pic() {
    File temperature = LittleFS.open("/temperature.png", "r");
    if (!temperature){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(temperature, "image/png");
    temperature.close();
}
void soil() {
    File soil = LittleFS.open("/soil.png", "r");
    if (!soil){
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    server.streamFile(soil, "image/png");
    soil.close();
}
void handleSetMode() {
    String mode = server.arg("mode");
    if (mode == "AUTO") {
        currentMode = AUTO;
    } else if (mode == "MANUAL") {
        currentMode = MANUAL;
    } else if (mode == "SCHEDULE") {
        currentMode = SCHEDULE;
    }

    saveStateToEEPROM(); 
    server.send(200, "text/plain", "Mode set to " + mode);
    Serial.print("Current mode: ");
    Serial.println(mode);
}
void handleScheduledTasks() {
    int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  // Ensure schedule mode is active
  if (!scheduleMode) {
    return;  // Do nothing if schedule mode is not enabled
  }

  // Check for pump timer
  if ((currentHour == pumpStartTime && currentMinute >= pumpStartMinute) && 
      (currentHour < pumpEndTime || (currentHour == pumpEndTime && currentMinute < pumpEndMinute))) {
    digitalWrite(relay1, HIGH);  // Turn on pump
  } else if ((currentHour > pumpEndTime) || 
             (currentHour == pumpEndTime && currentMinute >= pumpEndMinute) ||
             currentHour < pumpStartTime || 
             (currentHour == pumpStartTime && currentMinute < pumpStartMinute)) {
    digitalWrite(relay1, LOW);   // Turn off pump
  }

  // Similar checks for fan and light timers
  if ((currentHour == fanStartTime && currentMinute >= fanStartMinute) && 
      (currentHour < fanEndTime || (currentHour == fanEndTime && currentMinute < fanEndMinute))) {
    digitalWrite(relay2, HIGH);  // Turn on fan
  } else if ((currentHour > fanEndTime) || 
             (currentHour == fanEndTime && currentMinute >= fanEndMinute) ||
             currentHour < fanStartTime || 
             (currentHour == fanStartTime && currentMinute < fanStartMinute)) {
    digitalWrite(relay2, LOW);   // Turn off fan
  }

  if ((currentHour == lightStartTime && currentMinute >= lightStartMinute) && 
      (currentHour < lightEndTime || (currentHour == lightEndTime && currentMinute < lightEndMinute))) {
    digitalWrite(relay3, HIGH);  // Turn on light
  } else if ((currentHour > lightEndTime) || 
             (currentHour == lightEndTime && currentMinute >= lightEndMinute) ||
             currentHour < lightStartTime || 
             (currentHour == lightStartTime && currentMinute < lightStartMinute)) {
    digitalWrite(relay3, LOW);   // Turn off light
  }
}

void handleSetTimer() {
  String device = server.arg("device");
  String startTime = server.arg("startTime");
  String endTime = server.arg("endTime");

  int startHour, startMin, endHour, endMin;
  
  // Parse start and end times
  sscanf(startTime.c_str(), "%d:%d", &startHour, &startMin);
  sscanf(endTime.c_str(), "%d:%d", &endHour, &endMin);

  // Validate time format and logical order (start time < end time)
  if (startHour >= 0 && startHour < 24 && startMin >= 0 && startMin < 60 &&
      endHour >= 0 && endHour < 24 && endMin >= 0 && endMin < 60) {

    if ((endHour < startHour) || (endHour == startHour && endMin <= startMin)) {
      server.send(400, "text/plain", "End time must be later than start time.");
      return;
    }

    // Update the scheduled time for the device
    if (device == "pump") {
      pumpStartTime = startHour;
      pumpEndTime = endHour;
      pumpStartMinute = startMin;
      pumpEndMinute = endMin;
    } else if (device == "fan") {
      fanStartTime = startHour;
      fanEndTime = endHour;
      fanStartMinute = startMin;
      fanEndMinute = endMin;
    } else if (device == "light") {
      lightStartTime = startHour;
      lightEndTime = endHour;
      lightStartMinute = startMin;
      lightEndMinute = endMin;
    }
    server.send(200, "text/plain", "Timer set successfully!");
  } else {
    server.send(400, "text/plain", "Invalid time format.");
  }
}

void handleSetScheduleMode() {
  scheduleMode = true;  
  Serial.println("Schedule mode activated");  
  server.send(200, "text/plain", "Schedule mode active");
}