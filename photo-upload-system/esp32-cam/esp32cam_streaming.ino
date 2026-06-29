#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <BluetoothSerial.h>

// WiFi配置
const char* ssid = "test";
const char* password = "12345678";

// 服务器配置
const char* serverIP = "172.172.23.8";
const int serverPort = 5000;

// 摄像头配置
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

BluetoothSerial SerialBT;

void captureAndUpload() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("拍照失败");
    return;
  }
  
  Serial.printf("照片大小: %zu 字节\n", fb->len);
  
  // 上传照片
  WiFiClient client;
  if (client.connect(serverIP, serverPort)) {
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String header = "POST /api/upload HTTP/1.1\r\n";
    header += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
    header += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    header += "Content-Length: " + String(fb->len + boundary.length() * 2 + 40) + "\r\n";
    header += "Connection: close\r\n\r\n";
    
    client.print(header);
    client.print("--" + boundary + "\r\n");
    client.print("Content-Disposition: form-data; name=\"image\"; filename=\"esp32cam.jpg\"\r\n");
    client.print("Content-Type: image/jpeg\r\n\r\n");
    
    client.write(fb->buf, fb->len);
    
    client.print("\r\n--" + boundary + "--\r\n");
    
    // 等待响应
    while (client.connected()) {
      if (client.available()) {
        String line = client.readStringUntil('\r');
        Serial.println(line);
      }
    }
    client.stop();
  } else {
    Serial.println("无法连接到服务器");
  }
  
  esp_camera_fb_return(fb);
}

void sendFrame() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (fb) {
    // 上传画面
    WiFiClient client;
    if (client.connect(serverIP, serverPort)) {
      String header = "POST /api/raw HTTP/1.1\r\n";
      header += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
      header += "Content-Type: image/jpeg\r\n";
      header += "Content-Length: " + String(fb->len) + "\r\n";
      header += "Connection: close\r\n\r\n";
      
      client.print(header);
      client.write(fb->buf, fb->len);
      
      // 等待响应
      while (client.connected()) {
        if (client.available()) {
          client.read();
        }
      }
      client.stop();
    }
    esp_camera_fb_return(fb);
  }
}

void setup() {
  Serial.begin(115200);
  
  // 连接WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("正在连接WiFi...");
  }
  Serial.println("WiFi连接成功！");
  Serial.print("ESP32 IP地址: ");
  Serial.println(WiFi.localIP());
  
  // 初始化摄像头
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; // 320x240
  config.jpeg_quality = 12;
  config.fb_count = 2;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败: %d\n", err);
    return;
  }
  Serial.println("摄像头初始化成功！");
  
  // 初始化蓝牙
  SerialBT.begin("ESP32-CAM");
  Serial.println("蓝牙初始化完成，等待连接...");
  
  Serial.println("系统初始化完成");
}

void loop() {
  // 处理蓝牙数据
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    Serial.println("收到蓝牙命令: " + command);
    
    if (command == "capture") {
      Serial.println("执行拍照指令");
      captureAndUpload();
      SerialBT.println("拍照完成");
    } else if (command == "test") {
      SerialBT.println("测试命令收到");
    }
  }
  
  // 每500ms发送一帧画面
  static unsigned long lastFrameTime = 0;
  if (millis() - lastFrameTime > 500) {
    lastFrameTime = millis();
    sendFrame();
  }
}
