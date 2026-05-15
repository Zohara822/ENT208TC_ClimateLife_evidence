#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===== 屏幕库 =====
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ===== 屏幕引脚定义 =====
#define TFT_CS 26
#define TFT_RST 15
#define TFT_DC 27
#define TFT_MOSI 23
#define TFT_SCLK 18
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ===== WiFi 配置 =====
const char* ssid = "Redmi K50 Pro";
const char* password = "yangdu20041103";

// ===== MQTT 配置 =====
const char* mqtt_server = "h4fd6666.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "esp32";
const char* mqtt_pass = "esp32";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// ===== LED 配置 =====
#define LED_PIN 14
#define NUM_LEDS 30
CRGB leds[NUM_LEDS];
#define TEMP_COLD 15.0
#define TEMP_HOT 28.0

int ledMode = 0;
CRGB manualColor = CRGB::White;
uint8_t manualBrightness = 255;
bool ledOn = true;

// ===== 传感器配置 =====
#define ONE_WIRE_BUS 13
#define SOIL_SENSOR_PIN 34
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ===== 水泵配置 =====
#define PUMP_PIN 32
int pumpMode = 0;     // 0:自动, 1:手动
bool pumpState = false; // 当前水泵实际状态（开/关）
// 自动脉冲专用变量
unsigned long dryStartTime = 0;
unsigned long pumpRunStartTime = 0;
bool isPumpRunning = false;
const int DRY_THRESHOLD = 1;                // 湿度低于30%视为干旱（保持原有值）
const unsigned long DRY_DURATION = 5000000;     // 持续干旱5000秒才启动（保持原有值）
const unsigned long PUMP_RUN_DURATION = 100; // 每次喷水0.1秒
// 手动模式脉冲时长（新增）
const unsigned long MANUAL_PUMP_DURATION = 500; // 手动开启后运行0.5秒

// ===== 存储天气数据的全局变量 =====
String weatherTime = "--:--:--";
String weatherCity = "Unknown";
float weatherTemp = 0.0;
int weatherHumidity = 0;
String weatherDesc = "--";

// ===== 辅助函数：从 JSON 字符串中提取整数值 =====
int extractValue(String json, String key) {
  int start = json.indexOf(key);
  if (start == -1) return -1;
  start += key.length();
  int end = json.indexOf(",", start);
  if (end == -1) end = json.indexOf("}", start);
  if (end == -1) return -1;
  String value = json.substring(start, end);
  value.trim();
  return value.toInt();
}

// ===== 辅助函数：提取字符串值（带双引号）=====
String extractStringValue(String json, String key) {
  int start = json.indexOf(key);
  if (start == -1) return "";
  start += key.length();
  int firstQuote = json.indexOf("\"", start);
  if (firstQuote == -1) return "";
  int secondQuote = json.indexOf("\"", firstQuote + 1);
  if (secondQuote == -1) return "";
  return json.substring(firstQuote + 1, secondQuote);
}

// ===== 提取浮点数值 =====
float extractFloatValue(String json, String key) {
  int start = json.indexOf(key);
  if (start == -1) return 0.0;
  start += key.length();
  int end = json.indexOf(",", start);
  if (end == -1) end = json.indexOf("}", start);
  if (end == -1) return 0.0;
  String value = json.substring(start, end);
  value.trim();
  return value.toFloat();
}

// ===== MQTT 回调函数 =====
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT command received on topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // 处理 LED 控制主题
  if (String(topic) == "esp32/led/control") {
    bool needUpdate = false;

    int r = extractValue(message, "\"r\":");
    int g = extractValue(message, "\"g\":");
    int b = extractValue(message, "\"b\":");
    if (r != -1 && g != -1 && b != -1) {
      manualColor = CRGB(r, g, b);
      ledMode = 1;
      ledOn = true;
      needUpdate = true;
      Serial.printf("Color set to (%d,%d,%d)\n", r, g, b);
    }

    int brightness = extractValue(message, "\"brightness\":");
    if (brightness == -1) brightness = extractValue(message, "\"value\":");
    if (brightness != -1) {
      manualBrightness = constrain(brightness, 0, 255);
      needUpdate = true;
      Serial.printf("Brightness set to %d\n", manualBrightness);
    }

    if (message.indexOf("\"action\":\"off\"") != -1) {
      ledOn = false;
      needUpdate = true;
      Serial.println("Action: OFF");
    } else if (message.indexOf("\"action\":\"on\"") != -1) {
      ledOn = true;
      needUpdate = true;
      Serial.println("Action: ON");
    } else if (message.indexOf("\"action\":\"auto\"") != -1) {
      ledMode = 0;
      needUpdate = true;
      Serial.println("Action: AUTO mode");
    }

    if (needUpdate && ledMode == 1) {
      if (ledOn) {
        fill_solid(leds, NUM_LEDS, manualColor);
        FastLED.setBrightness(manualBrightness);
      } else {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
      }
      FastLED.show();
    }
  }
  // 处理天气数据主题
  else if (String(topic) == "esp32/weather") {
    weatherTime = extractStringValue(message, "\"time\":");
    weatherCity = extractStringValue(message, "\"city\":");
    weatherTemp = extractFloatValue(message, "\"temp\":");
    weatherHumidity = extractValue(message, "\"humidity\":");
    weatherDesc = extractStringValue(message, "\"weather\":");
    Serial.println("Weather data updated");
  }

  // 水泵控制
  else if (String(topic) == "esp32/pump/control") {
    if (message.indexOf("\"action\":\"pump_auto\"") != -1) {
      pumpMode = 0;
      // 退出手动模式时，如果水泵正在运行则立即停止（脉冲模式会重新接管）
      if (isPumpRunning) {
        digitalWrite(PUMP_PIN, LOW);
        isPumpRunning = false;
      }
      pumpState = false;
      Serial.println("Pump mode: AUTO");
    } else if (message.indexOf("\"action\":\"pump_on\"") != -1) {
      pumpMode = 1;
      // 如果已经有脉冲在运行，先停止（重新开始计时）
      if (isPumpRunning) {
        digitalWrite(PUMP_PIN, LOW);
        isPumpRunning = false;
      }
      // 启动手动模式脉冲（0.5秒）
      digitalWrite(PUMP_PIN, HIGH);
      isPumpRunning = true;
      pumpRunStartTime = millis();
      pumpState = true;
      Serial.println("Pump manual pulse started (0.5s)");
    } else if (message.indexOf("\"action\":\"pump_off\"") != -1) {
      pumpMode = 1;
      pumpState = false;
      if (isPumpRunning) {
        digitalWrite(PUMP_PIN, LOW);
        isPumpRunning = false;
      } else {
        digitalWrite(PUMP_PIN, LOW);
      }
      Serial.println("Pump manual OFF");
    }
  }
}

// ===== WiFi 连接 =====
void setup_wifi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// ===== MQTT 重连 =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      client.subscribe("esp32/led/control");
      client.subscribe("esp32/weather");
      client.subscribe("esp32/pump/control");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ===== 初始化 =====
void setup() {
  setup_wifi();
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  sensors.begin();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // 屏幕初始化
  tft.init(240, 240);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(2);

  // 绘制静态标签（只画一次）
  tft.setCursor(10, 20);
  tft.print("Local Sensor:");
  tft.setCursor(10, 50);
  tft.print("Temp: ");
  tft.setCursor(10, 80);
  tft.print("Soil H: ");
  tft.setCursor(10, 130);
  tft.print("Time: ");
  tft.setCursor(10, 160);
  tft.print("Temp: ");
  tft.setCursor(10, 190);
  tft.print("Weather: ");

  Serial.println("System Ready");
}

// ===== 主循环 =====
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 读取传感器
  sensors.requestTemperatures();
  float temperatureC = sensors.getTempCByIndex(0);
  if (temperatureC == DEVICE_DISCONNECTED_C) {
    temperatureC = 25.0;
  }

  int rawHumidity = analogRead(SOIL_SENSOR_PIN);
  int moisturePercent = map(rawHumidity, 4095, 0, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // ===== 增量更新屏幕数值（不清屏）=====
  // 更新本地温度 (坐标 x=80, y=50，因为 "Temp: " 约 6 个字符，字体大小2，每个约12px，所以x=80合适)
  tft.setCursor(80, 50);
  tft.print("     ");   // 清除旧数字（足够覆盖"XX.X C"）
  tft.setCursor(80, 50);
  tft.print(temperatureC, 1);
  tft.print(" C");

  // 更新土壤湿度 (x=80, y=80)
  tft.setCursor(80, 80);
  tft.print("     ");
  tft.setCursor(80, 80);
  tft.print(moisturePercent);
  tft.print(" %");

  // 更新时间 (x=80, y=130)
  tft.setCursor(80, 130);
  tft.print("               ");   // 足够覆盖时间字符串
  tft.setCursor(80, 130);
  tft.print(weatherTime);

  // 更新天气温度 (x=80, y=160)
  tft.setCursor(80, 160);
  tft.print("     ");
  tft.setCursor(80, 160);
  tft.print(weatherTemp, 1);
  tft.print(" C");

  // 更新天气描述 (x=80, y=190)
  tft.setCursor(80, 190);
  tft.print("               ");
  tft.setCursor(80, 190);
  tft.print(weatherDesc);

  // ---- LED 控制 ----
  if (ledMode == 0) {
    uint8_t hue;
    if (temperatureC <= TEMP_COLD) hue = 160;
    else if (temperatureC >= TEMP_HOT) hue = 0;
    else {
      float t = (temperatureC - TEMP_COLD) / (TEMP_HOT - TEMP_COLD);
      hue = 160 * (1 - t);
    }
    uint8_t brightness = map(moisturePercent, 0, 100, 255, 20);
    brightness = constrain(brightness, 20, 255);
    fill_solid(leds, NUM_LEDS, CHSV(hue, 255, brightness));
    FastLED.show();
  } else {
    if (!ledOn) {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
    }
  }

  // ===== 水泵控制（自动脉冲模式 + 手动脉冲模式）=====
  if (pumpMode == 0) {  // 自动模式
    if (isPumpRunning) {
      if (millis() - pumpRunStartTime >= PUMP_RUN_DURATION) {
        digitalWrite(PUMP_PIN, LOW);
        isPumpRunning = false;
        pumpState = false;
        Serial.println("Pump auto pulse finished");
      }
    } else {
      if (moisturePercent < DRY_THRESHOLD) {
        if (dryStartTime == 0) {
          dryStartTime = millis();
        } else if (millis() - dryStartTime >= DRY_DURATION) {
          digitalWrite(PUMP_PIN, HIGH);
          isPumpRunning = true;
          pumpRunStartTime = millis();
          pumpState = true;
          dryStartTime = 0;
          Serial.println("Pump auto pulse started");
        }
      } else {
        dryStartTime = 0;
      }
    }
  } else {  // 手动模式
    // 如果当前有脉冲在运行，检查是否超时（0.5秒）
    if (isPumpRunning) {
      if (millis() - pumpRunStartTime >= MANUAL_PUMP_DURATION) {
        digitalWrite(PUMP_PIN, LOW);
        isPumpRunning = false;
        pumpState = false;
        Serial.println("Pump manual pulse finished (auto off)");
      }
    }
    // 注意：手动模式下，pump_off 命令已经直接关闭水泵并清除了 isPumpRunning
    // 这里不需要额外操作
  }

  // ---- 发送传感器数据到 MQTT ----
  String payload = "{";
  payload += "\"temperature\":" + String(temperatureC, 1) + ",";
  payload += "\"humidity\":" + String(moisturePercent);
  payload += "}";
  client.publish("esp32/data", payload.c_str());

  // ---- 串口调试 ----
  Serial.println("====== DATA ======");
  Serial.println(payload);
  Serial.print("LED Mode: ");
  Serial.println(ledMode == 0 ? "Auto" : "Manual");
  if (ledMode == 1) {
    Serial.print("Manual Color: (");
    Serial.print(manualColor.r);
    Serial.print(",");
    Serial.print(manualColor.g);
    Serial.print(",");
    Serial.print(manualColor.b);
    Serial.print(") Brightness: ");
    Serial.println(manualBrightness);
    Serial.print("LED On: ");
    Serial.println(ledOn ? "Yes" : "No");
  }
  Serial.print("City: ");
  Serial.print(weatherCity);
  Serial.print("  Time: ");
  Serial.print(weatherTime);
  Serial.print("  Temp: ");
  Serial.print(weatherTemp);
  Serial.print("C  Humidity: ");
  Serial.print(weatherHumidity);
  Serial.print("%  Weather: ");
  Serial.println(weatherDesc);

  // 延长刷新间隔到1秒
  delay(1000);
}