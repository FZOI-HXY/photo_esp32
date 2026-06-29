#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// WiFi配置
const char* ssid = "test";
const char* password = "12345678";

// 服务器配置
const char* serverAddress = "http://172.30.227.134:5000";

// 任务句柄
TaskHandle_t xTaskCore0 = NULL;
TaskHandle_t xTaskCore1 = NULL;

// 信号量
SemaphoreHandle_t xCaptureSemaphore = NULL;
SemaphoreHandle_t xPreviewSemaphore = NULL;

// 状态标志
bool g_bIsCapturing = false;
bool g_bPreviewRunning = false;
bool g_bNeedCapture = false;  // 新增：标记是否需要拍照
bool g_bFlashEnabled = false;  // 闪光灯状态

// 摄像头引脚定义 - AI-Thinker ESP32-CAM
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

// 闪光灯控制引脚
#define FLASH_GPIO_NUM    4

// 初始化摄像头
esp_err_t initCamera(bool highResolution) {
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
  
  if (highResolution) {
    config.frame_size = FRAMESIZE_QXGA; // 2048x1536 (OV3660最高分辨率)
    config.jpeg_quality = 6; // 降低质量以减少内存使用
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM; // 高分辨率使用PSRAM
  } else {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 20; // 预览模式降低质量以提高流畅度
    config.fb_count = 2; // 使用2个缓冲区平衡内存和流畅度
    config.fb_location = CAMERA_FB_IN_DRAM; // 预览模式使用内部RAM
  }
  
  // 初始化摄像头
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败: %d\n", err);
    return err;
  }
  
  // 启用坏像素矫正与自动白平衡
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    // 启用自动白平衡
    s->set_awb_gain(s, 1);
    // 启用坏像素矫正
    s->set_bpc(s, 1);
    // 设置白平衡为自动模式
    s->set_wb_mode(s, 0); // 0 = 自动白平衡
    Serial.println("已启用坏像素矫正与自动白平衡");
  }
  
  Serial.println("摄像头初始化成功");
  return ESP_OK;
}

// 上传图像到服务器
void uploadImage(camera_fb_t* fb, bool isHighResolution) {
  if (!fb) return;
  
  WiFiClient client;
  HTTPClient http;
  
  String url = String(serverAddress) + (isHighResolution ? "/api/raw" : "/api/raw");
  
  if (http.begin(client, url)) {
    // 设置请求头
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Resolution", isHighResolution ? "high" : "preview");
    http.addHeader("X-ESP32-IP", WiFi.localIP().toString());
    
    // 增加超时时间，高分辨率照片需要更长时间上传
    http.setTimeout(30000); // 30秒超时
    
    Serial.printf("开始上传 %s 图像，大小: %zu 字节\n", isHighResolution ? "高分辨率" : "预览", fb->len);
    
    // 发送图像数据
    unsigned long startTime = millis();
    int httpCode = http.POST((uint8_t*)fb->buf, fb->len);
    unsigned long uploadTime = millis() - startTime;
    
    if (httpCode > 0) {
      String response = http.getString();
      Serial.printf("上传成功，状态码: %d，耗时: %lu ms\n", httpCode, uploadTime);
      Serial.printf("上传响应: %s\n", response.c_str());
    } else {
      Serial.printf("上传失败: %s，耗时: %lu ms\n", http.errorToString(httpCode).c_str(), uploadTime);
    }
    
    http.end();
  } else {
    Serial.println("无法连接到服务器");
  }
}

// Core 0 任务：处理预览和上传
void vTaskCore0(void *pvParameters) {
  Serial.println("Core 0 任务启动：处理预览和上传");
  
  // 初始化摄像头为预览模式
  if (initCamera(false) != ESP_OK) {
    vTaskDelete(NULL);
    return;
  }
  
  g_bPreviewRunning = true;
  
  while (1) {
    // 检查是否需要拍照
    if (g_bNeedCapture) {
      g_bNeedCapture = false;
      Serial.println("Core 0: 开始执行拍照任务");
      g_bIsCapturing = true;
      g_bPreviewRunning = false;
      
      // 停止预览
      esp_camera_deinit();
      vTaskDelay(pdMS_TO_TICKS(100));
      
      // 重新初始化摄像头为高分辨率模式
      if (initCamera(true) == ESP_OK) {
        // 重新设置白平衡参数（高分辨率模式需要重新配置）
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
          s->set_awb_gain(s, 1);      // 启用自动白平衡增益
          s->set_wb_mode(s, 0);       // 0 = 自动白平衡模式
          s->set_aec2(s, 1);          // 启用高级自动曝光
          s->set_gain_ctrl(s, 1);     // 启用自动增益控制
          Serial.println("Core 0: 已重新配置白平衡和曝光参数");
        }
        
        // 等待自动白平衡稳定
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // 根据闪光灯状态决定是否打开
        if (g_bFlashEnabled) {
          digitalWrite(FLASH_GPIO_NUM, HIGH);
          Serial.println("Core 0: 闪光灯已打开（用户开启）");
          vTaskDelay(pdMS_TO_TICKS(200)); // 等待闪光灯稳定
        } else {
          Serial.println("Core 0: 闪光灯保持关闭（用户未开启）");
        }
        
        // 丢弃前几帧，让传感器适应新的光照条件
        for (int i = 0; i < 3; i++) {
          camera_fb_t *temp_fb = esp_camera_fb_get();
          if (temp_fb) {
            esp_camera_fb_return(temp_fb);
          }
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        Serial.println("Core 0: 已丢弃适应帧，白平衡已稳定");
        
        // 获取高分辨率图像
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
          Serial.printf("Core 0: 拍摄成功，图像大小: %zu 字节\n", fb->len);
          // 上传高分辨率图像
          uploadImage(fb, true);
          esp_camera_fb_return(fb);
          Serial.println("Core 0: 拍照完成并已上传");
        } else {
          Serial.println("Core 0: 拍照失败");
        }
        
        // 关闭闪光灯（如果之前打开了）
        if (g_bFlashEnabled) {
          digitalWrite(FLASH_GPIO_NUM, LOW);
          Serial.println("Core 0: 闪光灯已关闭");
        }
      } else {
        Serial.println("Core 0: 高分辨率模式初始化失败");
      }
      
      // 恢复预览模式
      esp_camera_deinit();
      vTaskDelay(pdMS_TO_TICKS(100));
      initCamera(false);
      
      g_bIsCapturing = false;
      g_bPreviewRunning = true;
      Serial.println("Core 0: 重新开始预览");
      continue;
    }
    
    // 预览模式：获取图像并上传
    if (g_bPreviewRunning && !g_bIsCapturing) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        uploadImage(fb, false);
        esp_camera_fb_return(fb);
      } else {
        Serial.println("Core 0: 获取预览帧失败");
      }
      
      // 30ms发送一帧，实现约30fps
      vTaskDelay(pdMS_TO_TICKS(30));
    } else {
      // 短暂延迟，避免CPU占用过高
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// 闪光灯状态
bool g_bFlashEnabled = false;

// HTTP服务器处理函数
void handleCapture(WiFiClient client) {
  Serial.println("Core 1: 收到拍照请求");
  
  // 检查是否正在拍照
  if (g_bIsCapturing) {
    client.println("HTTP/1.1 503 Service Unavailable");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"success\": false, \"message\": \"正在拍照中，请稍后再试\"}");
    return;
  }
  
  // 设置拍照标志，让Core 0执行拍照
  g_bNeedCapture = true;
  
  // 立即发送响应
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"success\": true, \"message\": \"拍照指令已接收，正在处理中...\"}");
  
  Serial.println("Core 1: 已设置拍照标志，Core 0将执行拍照");
}

// 处理闪光灯控制请求
void handleFlashControl(WiFiClient client, String& requestBody) {
  Serial.println("Core 1: 收到闪光灯控制请求");
  
  // 解析JSON请求体
  bool enable = false;
  if (requestBody.indexOf("\"enable\":true") != -1 || requestBody.indexOf("\"enable\": true") != -1) {
    enable = true;
  }
  
  // 更新闪光灯状态
  g_bFlashEnabled = enable;
  digitalWrite(FLASH_GPIO_NUM, enable ? HIGH : LOW);
  
  Serial.printf("Core 1: 闪光灯已%s\n", enable ? "开启" : "关闭");
  
  // 发送响应
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.printf("{\"success\": true, \"flash_enabled\": %s, \"message\": \"闪光灯已%s\"}", 
                enable ? "true" : "false", enable ? "开启" : "关闭");
}

// Core 1 任务：处理拍照指令
void vTaskCore1(void *pvParameters) {
  Serial.println("Core 1 任务启动：处理拍照指令");
  
  // 创建HTTP服务器
  WiFiServer server(80);
  server.begin();
  Serial.println("Core 1: HTTP服务器启动在端口 80");
  
  while (1) {
    WiFiClient client = server.available();
    if (client) {
      Serial.println("Core 1: 新客户端连接");
      
      String currentLine = "";
      String requestMethod = "";
      
      String requestBody = "";
      bool isPostRequest = false;
      int contentLength = 0;
      
      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          if (c == '\n') {
            if (currentLine.length() == 0) {
              // 空行表示请求头结束
              // 读取请求体（如果是POST请求）
              if (isPostRequest && contentLength > 0) {
                while (requestBody.length() < contentLength && client.available()) {
                  requestBody += (char)client.read();
                }
                Serial.printf("Core 1: 请求体: %s\n", requestBody.c_str());
              }
              
              // 检查是否是拍照请求
              if (requestMethod.indexOf("POST /capture") != -1) {
                handleCapture(client);
              } else if (requestMethod.indexOf("POST /flash") != -1) {
                handleFlashControl(client, requestBody);
              } else if (requestMethod.indexOf("GET /flash") != -1) {
                // 返回闪光灯状态
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: application/json");
                client.println("Connection: close");
                client.println();
                client.printf("{\"success\": true, \"flash_enabled\": %s}", 
                              g_bFlashEnabled ? "true" : "false");
              } else {
                // 其他请求
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: text/html");
                client.println("Connection: close");
                client.println();
                client.println("<html><body><h1>ESP32-CAM 服务器</h1><p>拍照API: POST /capture</p><p>闪光灯API: POST /flash</p></body></html>");
              }
              break;
            } else {
              // 保存第一行请求信息
              if (requestMethod.isEmpty()) {
                requestMethod = currentLine;
                // 检查是否是POST请求
                if (currentLine.indexOf("POST") != -1) {
                  isPostRequest = true;
                }
              }
              // 检查Content-Length
              if (currentLine.indexOf("Content-Length:") != -1) {
                contentLength = currentLine.substring(currentLine.indexOf(":") + 1).toInt();
                Serial.printf("Core 1: Content-Length: %d\n", contentLength);
              }
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
      }
      
      client.stop();
      Serial.println("Core 1: 客户端连接关闭");
    }
    
    // 短暂延迟，避免CPU占用过高
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  // 禁用棕褐色检测
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // 初始化闪光灯引脚
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW); // 默认关闭闪光灯
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("ESP32-CAM FreeRTOS任务分配系统初始化");
  Serial.println("闪光灯引脚初始化完成");
  
  // 创建信号量
  xCaptureSemaphore = xSemaphoreCreateBinary();
  xPreviewSemaphore = xSemaphoreCreateBinary();
  
  if (xCaptureSemaphore == NULL || xPreviewSemaphore == NULL) {
    Serial.println("创建信号量失败");
    return;
  }
  
  // 连接WiFi
  Serial.printf("连接到 WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  
  Serial.println();
  Serial.printf("WiFi连接成功，IP地址: %s\n", WiFi.localIP().toString().c_str());
  
  // 创建Core 0任务
  xTaskCreatePinnedToCore(
    vTaskCore0,
    "TaskCore0",
    10000,
    NULL,
    1,
    &xTaskCore0,
    0
  );
  
  // 创建Core 1任务
  xTaskCreatePinnedToCore(
    vTaskCore1,
    "TaskCore1",
    10000,
    NULL,
    1,
    &xTaskCore1,
    1
  );
  
  Serial.println("系统初始化完成，双核心任务已启动");
}

void loop() {
  // 定时打印IP地址，每10秒打印一次
  static unsigned long lastPrintTime = 0;
  if (millis() - lastPrintTime > 10000) {
    Serial.printf("当前IP地址: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("HTTP服务器地址: http://%s:80\n", WiFi.localIP().toString().c_str());
    lastPrintTime = millis();
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}
