#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <Arduino.h>
#include <WebServer.h>

// WiFi配置
const char* ssid = "test";
const char* password = "12345678";

// 服务器配置
char serverIP[20] = "172.172.23.8"; // 默认服务器IP
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

// 全局变量
WiFiClient client;
WebServer server(80);
bool captureRequest = false;
framesize_t currentFrameSize = FRAMESIZE_QVGA; // 默认预览分辨率
framesize_t captureFrameSize = FRAMESIZE_XGA; // 拍照分辨率 (1024x768 = 0.8MP)

// 优化参数
#define PREVIEW_DELAY 50 // 预览延迟，单位毫秒 (约20fps)
#define JPEG_QUALITY 12 // JPEG质量，0-63，值越小质量越高，12是高质量设置


// 拍照处理函数
void handleCapture() {
  captureRequest = true;
  server.send(200, "application/json", "{\"success\": true, \"message\": \"拍照指令已接收\"}");
}

// 状态处理函数
void handleStatus() {
  String response = "{";
  response += "\"psram_size\": " + String(ESP.getPsramSize() / (1024 * 1024)) + ",";
  response += "\"free_psram\": " + String(ESP.getFreePsram() / (1024)) + ",";
  response += "\"free_heap\": " + String(ESP.getFreeHeap() / 1024) + ",";
  response += "\"current_resolution\": \"" + String(currentFrameSize) + "\",";
  response += "\"capture_resolution\": \"" + String(captureFrameSize) + "\",";
  response += "\"server_ip\": \"" + String(serverIP) + "\",";
  response += "\"server_port\": " + String(serverPort);
  response += "}";
  server.send(200, "application/json", response);
}

// 配置页面处理函数
void handleConfig() {
  String html = "<!DOCTYPE html>";
  html += "<html lang=\"zh-CN\">";
  html += "<head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>ESP32-CAM 配置</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "h1 { color: #333; }";
  html += ".config-form { margin-top: 20px; }";
  html += "label { display: block; margin: 10px 0 5px; }";
  html += "input[type=text] { width: 200px; padding: 8px; }";
  html += "input[type=submit] { margin-top: 20px; padding: 10px 20px; background: #4CAF50; color: white; border: none; cursor: pointer; }";
  html += "input[type=submit]:hover { background: #45a049; }";
  html += ".status { margin-top: 20px; padding: 10px; background: #f0f0f0; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<h1>ESP32-CAM 配置</h1>";
  html += "<div class=\"status\">";
  html += "<p>当前服务器IP: " + String(serverIP) + "</p>";
  html += "<p>当前服务器端口: " + String(serverPort) + "</p>";
  html += "<p>ESP32-CAM IP: " + WiFi.localIP().toString() + "</p>";
  html += "</div>";
  html += "<form class=\"config-form\" method=\"post\" action=\"/saveconfig\">";
  html += "<label for=\"serverIP\">服务器IP地址:</label>";
  html += "<input type=\"text\" id=\"serverIP\" name=\"serverIP\" value=\"" + String(serverIP) + "\">";
  html += "<input type=\"submit\" value=\"保存配置\">";
  html += "</form>";
  html += "</body>";
  html += "</html>";
  server.send(200, "text/html", html);
}

// 保存配置处理函数
void handleSaveConfig() {
  if (server.hasArg("serverIP")) {
    String newServerIP = server.arg("serverIP");
    newServerIP.toCharArray(serverIP, sizeof(serverIP));
    server.send(200, "text/html", "<h1>配置保存成功！</h1><p>新的服务器IP: " + newServerIP + "</p><a href=\"/config\">返回配置页面</a>");
  } else {
    server.send(400, "text/html", "<h1>保存失败</h1><p>请提供服务器IP地址</p>");
  }
}

// 设置摄像头分辨率
void setCameraResolution(framesize_t frameSize) {
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    esp_err_t err = s->set_framesize(s, frameSize);
    if (err == ESP_OK) {
      Serial.printf("分辨率已设置为: %s\n", frameSizeToString(frameSize));
      currentFrameSize = frameSize;
    } else {
      Serial.printf("设置分辨率失败: %d\n", err);
    }
  }
}

// 帧率统计
unsigned long lastFrameTime = 0;
int frameCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32-CAM 启动中...");
  
  // 打印内存信息
  Serial.printf("PSRAM大小: %d MB\n", ESP.getPsramSize() / (1024 * 1024));
  Serial.printf("可用PSRAM: %d MB\n", ESP.getFreePsram() / (1024 * 1024));
  Serial.printf("可用堆内存: %d KB\n", ESP.getFreeHeap() / 1024);
  
  // 连接WiFi
  WiFi.begin(ssid, password);
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi连接失败，重启...");
    ESP.restart();
  }
  
  Serial.println("\nWiFi连接成功！");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // 初始化摄像头 - 预览模式（低分辨率）
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
  config.xclk_freq_hz = 20000000; // 20MHz平衡性能和稳定性
  // 尝试使用JPEG格式
  config.pixel_format = PIXFORMAT_JPEG;
  
  // 预览模式：低分辨率，高帧率
  config.frame_size = FRAMESIZE_QVGA; // 320x240
  config.fb_count = 3; // 三缓冲区，提高帧率
  config.jpeg_quality = JPEG_QUALITY; // 设置JPEG质量
  
  Serial.println("尝试初始化摄像头...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("QVGA初始化失败: %d\n", err);
    return;
  }
  
  Serial.println("摄像头初始化成功！");
  
  // 配置摄像头参数
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_brightness(s, 1);     // 亮度: -2 to 2，稍微降低亮度减少噪点
    s->set_contrast(s, 2);       // 对比度: -2 to 2，提高对比度增强清晰度
    s->set_saturation(s, 1);     // 饱和度: -2 to 2
    s->set_sharpness(s, 2);      // 锐度: -2 to 2，提高锐度减少模糊
    s->set_denoise(s, 1);        // 降噪: 0=关闭, 1=开启，减少噪点
    s->set_special_effect(s, 0); // 特殊效果: 0=无, 1=负片, 2=灰度等
    s->set_whitebal(s, 1);       // 自动白平衡
    s->set_awb_gain(s, 1);       // 自动白平衡增益
    s->set_wb_mode(s, 0);        // 白平衡模式: 0=自动, 1=阳光, 2=阴天, 3=荧光灯, 4=白炽灯
    s->set_exposure_ctrl(s, 1);   // 自动曝光控制
    s->set_aec2(s, 1);            // 高级曝光控制
    s->set_gain_ctrl(s, 1);       // 自动增益控制
    s->set_agc_gain(s, 5);        // 自动增益值: 0 to 30，降低增益减少噪点
    s->set_gainceiling(s, (gainceiling_t)2); // 增益上限: 0=2x, 1=4x, 2=8x, 3=16x, 4=32x, 5=64x
    s->set_bpc(s, 1);             // 坏像素校正: 开启
    s->set_wpc(s, 1);             // 白平衡校正
    s->set_raw_gma(s, 1);         // 原始图像 gamma 校正
    s->set_lenc(s, 1);            // 镜头校正
    s->set_dcw(s, 1);             // 动态对比度增强
    s->set_colorbar(s, 0);        // 彩条测试模式
    
    // 获取一帧来显示分辨率
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("当前分辨率: %dx%d\n", fb->width, fb->height);
      esp_camera_fb_return(fb);
    }
  }
  
  // 初始化WebServer
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/saveconfig", HTTP_POST, handleSaveConfig);
  server.begin();
  
  Serial.println("系统就绪");
  Serial.println("预览模式：QVGA (320x240) 高帧率");
  Serial.println("拍照模式：QXGA (2048x1536) (3MP) 高质量");
}

// 辅助函数：将分辨率枚举转换为字符串
const char* frameSizeToString(framesize_t size) {
  switch(size) {
    case FRAMESIZE_QVGA: return "QVGA (320x240)";
    case FRAMESIZE_VGA: return "VGA (640x480)";
    case FRAMESIZE_SVGA: return "SVGA (800x600)";
    case FRAMESIZE_XGA: return "XGA (1024x768)";
    case FRAMESIZE_SXGA: return "SXGA (1280x1024)";
    case FRAMESIZE_UXGA: return "UXGA (1600x1200)";
    case FRAMESIZE_QXGA: return "QXGA (2048x1536) (3MP)";
    default: return "未知";
  }
}

void loop() {
  // 处理WebServer请求
  server.handleClient();
  
  // 帧率统计
  unsigned long currentTime = millis();
  if (currentTime - lastFrameTime >= 1000) { // 每秒统计一次
    Serial.printf("帧率: %d fps, 分辨率: %s\n", frameCount, frameSizeToString(currentFrameSize));
    frameCount = 0;
    lastFrameTime = currentTime;
  }
  
  // 检查是否有拍照请求
  if (captureRequest) {
    Serial.println("\n=== 拍照模式 ===");
    captureRequest = false;
    
    // 停止预览流，释放资源
    delay(100);
    
    // 切换到高分辨率
    Serial.println("切换到高分辨率...");
    setCameraResolution(captureFrameSize);
    
    // 等待摄像头稳定
    delay(800);
    
    // 清除缓冲区中的旧帧
    camera_fb_t * fb_old = esp_camera_fb_get();
    if (fb_old) {
      esp_camera_fb_return(fb_old);
    }
    delay(100);
    
    // 获取高分辨率画面
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("高分辨率画面: %zu 字节, 分辨率: %dx%d\n", fb->len, fb->width, fb->height);
      
      // 验证分辨率是否正确
      if (fb->width <= 320 && fb->height <= 240) {
        Serial.println("警告: 分辨率仍然较低，尝试重新初始化摄像头...");
        esp_camera_fb_return(fb);
        
        // 重新初始化摄像头
        esp_camera_deinit();
        delay(500);
        
        // 重新配置摄像头为高分辨率
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
        config.frame_size = captureFrameSize;
        config.jpeg_quality = 10; // 高质量
        config.fb_count = 1; // 单缓冲区节省内存
        
        esp_err_t err = esp_camera_init(&config);
        if (err != ESP_OK) {
          Serial.printf("摄像头重新初始化失败: %d\n", err);
        } else {
          Serial.println("摄像头重新初始化成功");
          delay(500);
          fb = esp_camera_fb_get();
        }
      }
      
      if (fb && fb->width > 320) {
        // 上传高分辨率画面
        if (client.connect(serverIP, serverPort)) {
          String header = "POST /api/raw HTTP/1.1\r\n";
          header += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
          header += "Content-Type: image/jpeg\r\n";
          header += "Content-Length: " + String(fb->len) + "\r\n";
          header += "X-Resolution: high\r\n";
          header += "X-Pixel-Format: jpeg\r\n";
          header += "X-Frame-Width: " + String(fb->width) + "\r\n";
          header += "X-Frame-Height: " + String(fb->height) + "\r\n";
          header += "X-ESP32-IP: " + WiFi.localIP().toString() + "\r\n";
          header += "Connection: close\r\n\r\n";
          
          client.print(header);
          
          // 分块发送数据
          size_t chunkSize = 8192; // 8KB块
          size_t sent = 0;
          while (sent < fb->len) {
            size_t toSend = min(chunkSize, fb->len - sent);
            size_t written = client.write(fb->buf + sent, toSend);
            if (written == 0) {
              Serial.println("发送失败");
              break;
            }
            sent += written;
            delay(1); // 短暂延迟避免网络拥塞
          }
          
          client.stop();
          Serial.printf("高分辨率画面上传完成，已发送: %zu / %zu 字节\n", sent, fb->len);
        } else {
          Serial.println("无法连接服务器");
        }
      } else {
        Serial.println("获取高分辨率画面失败");
      }
      
      // 释放帧缓冲区
      if (fb) {
        esp_camera_fb_return(fb);
      }
    } else {
      Serial.println("无法获取高分辨率画面");
    }
    
    // 重新初始化摄像头为预览模式
    Serial.println("切换回预览模式...");
    esp_camera_deinit();
    delay(300);
    
    // 重新配置为预览模式
    camera_config_t preview_config;
    preview_config.ledc_channel = LEDC_CHANNEL_0;
    preview_config.ledc_timer = LEDC_TIMER_0;
    preview_config.pin_pwdn = PWDN_GPIO_NUM;
    preview_config.pin_reset = RESET_GPIO_NUM;
    preview_config.pin_xclk = XCLK_GPIO_NUM;
    preview_config.pin_sscb_sda = SIOD_GPIO_NUM;
    preview_config.pin_sscb_scl = SIOC_GPIO_NUM;
    preview_config.pin_d7 = Y9_GPIO_NUM;
    preview_config.pin_d6 = Y8_GPIO_NUM;
    preview_config.pin_d5 = Y7_GPIO_NUM;
    preview_config.pin_d4 = Y6_GPIO_NUM;
    preview_config.pin_d3 = Y5_GPIO_NUM;
    preview_config.pin_d2 = Y4_GPIO_NUM;
    preview_config.pin_d1 = Y3_GPIO_NUM;
    preview_config.pin_d0 = Y2_GPIO_NUM;
    preview_config.pin_vsync = VSYNC_GPIO_NUM;
    preview_config.pin_href = HREF_GPIO_NUM;
    preview_config.pin_pclk = PCLK_GPIO_NUM;
    preview_config.xclk_freq_hz = 20000000;
    preview_config.pixel_format = PIXFORMAT_JPEG;
    preview_config.frame_size = FRAMESIZE_QVGA;
    preview_config.jpeg_quality = JPEG_QUALITY;
    preview_config.fb_count = 3;
    
    esp_err_t err = esp_camera_init(&preview_config);
    if (err != ESP_OK) {
      Serial.printf("预览模式初始化失败: %d\n", err);
    } else {
      // 重新配置摄像头参数
      sensor_t * s = esp_camera_sensor_get();
      if (s != NULL) {
        s->set_brightness(s, 1);
        s->set_contrast(s, 2);
        s->set_saturation(s, 1);
        s->set_sharpness(s, 2);
        s->set_denoise(s, 1);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 5);
        s->set_gainceiling(s, (gainceiling_t)2);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
      }
      currentFrameSize = FRAMESIZE_QVGA;
      Serial.println("预览模式恢复完成");
    }
    Serial.println("=== 回到预览模式 ===");
  } else {
    // 预览模式：低分辨率高帧率
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      frameCount++;
      
      // 上传预览画面
      if (client.connect(serverIP, serverPort)) {
        String header = "POST /api/raw HTTP/1.1\r\n";
        header += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
        header += "Content-Type: image/jpeg\r\n";
        header += "Content-Length: " + String(fb->len) + "\r\n";
        header += "X-Resolution: preview\r\n";
        header += "X-Pixel-Format: jpeg\r\n";
        header += "X-Frame-Width: " + String(fb->width) + "\r\n";
        header += "X-Frame-Height: " + String(fb->height) + "\r\n";
        header += "X-ESP32-IP: " + WiFi.localIP().toString() + "\r\n";
        header += "Connection: close\r\n\r\n";
        
        client.print(header);
        
        // 分块发送数据，确保数据完整性
        size_t chunkSize = 4096; // 4KB 块，更小的块大小，减少内存使用
        size_t sent = 0;
        while (sent < fb->len) {
          size_t toSend = min(chunkSize, fb->len - sent);
          size_t written = client.write(fb->buf + sent, toSend);
          if (written == 0) {
            Serial.println("发送失败");
            break;
          }
          sent += written;
          // 短暂延迟，避免网络拥塞
          delay(1);
        }
        
        client.stop();
      }
      
      // 释放帧缓冲区
      esp_camera_fb_return(fb);
    } else {
      // 帧缓冲区获取失败，可能是溢出
      Serial.println("警告：无法获取帧缓冲区，可能是帧缓冲区溢出");
      // 短暂延迟，给系统时间恢复
      delay(100);
    }
    
    // 控制帧率：每80ms发送一帧 (约12fps)
    delay(PREVIEW_DELAY);
  }
  
  // 每5秒打印一次内存状态
  static unsigned long lastMemoryCheck = 0;
  if (currentTime - lastMemoryCheck >= 5000) {
    Serial.printf("内存状态 - 堆: %d KB, PSRAM: %d KB\n", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
    lastMemoryCheck = currentTime;
  }
}
