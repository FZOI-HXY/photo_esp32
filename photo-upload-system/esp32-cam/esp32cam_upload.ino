#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi配置
const char* ssid = "你的WiFi名称";
const char* password = "你的WiFi密码";

// 服务器配置
const char* serverName = "http://你的服务器IP:5000/api/upload";

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
    Serial.println("连接WiFi中...");
  }
  Serial.println("WiFi连接成功！");
  Serial.print("IP地址: ");
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
  config.frame_size = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 10;
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
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("拍照失败");
    delay(2000);
    return;
  }
  
  Serial.printf("照片大小: %zu 字节\n", fb->len);
  
  // 上传照片
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "multipart/form-data");
  
  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  
  String body = "";
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"image\"; filename=\"esp32cam.jpg\"\r\n";
  body += "Content-Type: image/jpeg\r\n\r\n";
  
  // 发送请求
  uint8_t* postData = (uint8_t*)malloc(body.length() + fb->len + 2 + boundary.length() + 4);
  if (!postData) {
    Serial.println("内存分配失败");
    esp_camera_fb_return(fb);
    delay(2000);
    return;
  }
  
  memcpy(postData, body.c_str(), body.length());
  memcpy(postData + body.length(), fb->buf, fb->len);
  memcpy(postData + body.length() + fb->len, (uint8_t*)"\r\n", 2);
  memcpy(postData + body.length() + fb->len + 2, (uint8_t*)("--" + boundary + "--").c_str(), boundary.length() + 4);
  
  int httpResponseCode = http.POST(postData, body.length() + fb->len + 2 + boundary.length() + 4);
  
  free(postData);
  esp_camera_fb_return(fb);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("上传成功，响应码: %d\n", httpResponseCode);
    Serial.println("响应内容: " + response);
  } else {
    Serial.printf("上传失败，错误: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
  
  // 等待5秒后再次拍照
  delay(5000);
}
