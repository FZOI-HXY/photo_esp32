#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>

// WiFi配置
const char* ssid = "你的WiFi名称";
const char* password = "你的WiFi密码";

// 服务器配置 - 替换为你的服务器IP
const char* serverIP = "172.30.229.102";
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
  config.frame_size = FRAMESIZE_QVGA; // 320x240，更小的尺寸上传更快
  config.jpeg_quality = 12;
  config.fb_count = 2;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败: %d\n", err);
    return;
  }
  Serial.println("摄像头初始化成功！");
}

void loop() {
  // 拍照
  Serial.println("正在拍照...");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("拍照失败");
    delay(2000);
    return;
  }
  
  Serial.printf("照片大小: %zu 字节\n", fb->len);
  
  // 上传照片
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // 构建服务器URL - 使用raw端点直接上传原始JPEG数据
    String url = "http://" + String(serverIP) + ":" + String(serverPort) + "/api/raw";
    http.begin(url);
    
    // 设置Content-Type为image/jpeg
    http.addHeader("Content-Type", "image/jpeg");
    
    // 直接发送原始JPEG数据
    int httpResponseCode = http.POST(fb->buf, fb->len);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("上传成功，响应码: %d\n", httpResponseCode);
      Serial.println("服务器响应: " + response);
    } else {
      Serial.printf("上传失败，错误: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
  } else {
    Serial.println("WiFi未连接，无法上传");
  }
  
  // 释放帧缓冲区
  esp_camera_fb_return(fb);
  
  // 每10秒上传一次
  Serial.println("等待10秒后再次上传...");
  delay(10000);
}
