/*
* Enhanced Edge Monitor with Web API and Image Streaming
* Features:
* - HTTP API for camera control and configuration
* - Real-time image streaming to web server
* - Remote control capabilities
* - Status reporting and monitoring
* */

#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_timer.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "time.h"
#include "esp_http_server.h"
#include "ArduinoJson.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"
#include "CircularBuffer.h"
#include "VideoUploader.h"

const int SD_PIN_CS = 21;
const int LED_PIN = LED_BUILTIN; // Built-in LED on XIAO ESP32S3

// WiFi Configuration
const char* WIFI_SSID = "wwddOhYeah!";        
const char* WIFI_PASSWORD = "wawadudu"; 

// Web Server Configuration
const char* WEB_SERVER_URL = "http://192.168.1.47:8000"; // Your web server URL
const int HTTP_PORT = 80;

// Upload Configuration (keeping original functionality)
const char* UPLOAD_URL = "http://192.168.1.47:8000/upload";
const char* UPLOAD_API_KEY = "";    
const long UPLOAD_CHUNK_SIZE = 8192;    
const long UPLOAD_TIMEOUT_MS = 30000;   
const int MAX_UPLOAD_RETRIES = 3;       
const bool ENABLE_HTTPS = false;        
const bool DELETE_AFTER_UPLOAD = true;  

// Storage Management Configuration
const long MAX_STORAGE_MB = 24;  
const long MIN_FREE_SPACE_MB = 1; 
const bool ENABLE_CIRCULAR_BUFFER = true; 

// NTP time configuration
const char* NTP_SERVER = "pool.ntp.org";       
const long GMT_OFFSET_SEC = 0;                  
const int DAYLIGHT_OFFSET_SEC = 0;              

// New streaming configuration
unsigned long imageStreamInterval = 5000; // Stream image every 5 seconds
unsigned long lastImageStream = 0;
bool streamingEnabled = true;

// Class instances
CircularBuffer* circularBuffer;
VideoUploader* videoUploader;
httpd_handle_t camera_httpd = NULL;

File videoFile;
bool camera_sign = false;
bool sd_sign = false;
bool wifi_connected = false;
bool recording_active = false;
bool system_paused = false;
unsigned long lastCaptureTime = 0;
unsigned long captureDuration = 10000; // 10 seconds
unsigned long captureInterval = 60000; // 60 seconds
int imageCount = 0;

// WiFi status LED variables
const long STATUS_CHECK_MS = 5000;         
unsigned long lastWiFiCheck = 0;
unsigned long lastLEDBlink = 0;
bool ledState = false;
int flashCount = 0;                         
unsigned long flashStartTime = 0;          

// Camera settings structure
struct CameraSettings {
  int framesize = FRAMESIZE_QVGA;
  int quality = 15;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
} cameraSettings;

// Function prototypes
void startCameraServer();
void streamImageToServer();
esp_err_t status_handler(httpd_req_t *req);
esp_err_t control_handler(httpd_req_t *req);
esp_err_t capture_handler(httpd_req_t *req);
esp_err_t command_handler(httpd_req_t *req);
esp_err_t recording_config_handler(httpd_req_t *req);
esp_err_t apply_settings_handler(httpd_req_t *req);

// WiFi and upload functions
void setupTime() {
  Serial.println("DEBUG: Setting up time synchronization...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 * 2 && attempts < 20) {
    delay(1000);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  
  if (now > 8 * 3600 * 2) {
    Serial.println();
    Serial.println("Time synchronized successfully!");
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
  } else {
    Serial.println();
    Serial.println("Failed to synchronize time with NTP server!");
    Serial.println("Videos will use fallback numbering system.");
  }
}

String getTimestampFilename() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    static int fallbackCount = 0;
    String fallbackName = "/video" + String(fallbackCount++) + ".avi";
    Serial.printf("DEBUG: Using fallback filename: %s\n", fallbackName.c_str());
    return fallbackName;
  }
  
  char timestamp[32];
  sprintf(timestamp, "/%04d%02d%02d%02d%02d%02d.avi",
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);
  
  String timestampName = String(timestamp);
  Serial.printf("DEBUG: Generated timestamp filename: %s\n", timestampName.c_str());
  return timestampName;
}

void connectToWiFi() {
  Serial.println("DEBUG: Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println();
    Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
    
    setupTime();
    startCameraServer();
  } else {
    wifi_connected = false;
    Serial.println();
    Serial.println("WiFi connection failed!");
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifi_connected) {
      Serial.println("DEBUG: WiFi connection lost. Attempting to reconnect...");
      wifi_connected = false;
      
      if (videoUploader->getIsUploading()) {
        videoUploader->pauseUpload();
        Serial.println("DEBUG: Paused uploads due to WiFi loss");
      }
    }
    connectToWiFi();
  } else if (!wifi_connected && WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("DEBUG: WiFi reconnected successfully!");
    
    if (videoUploader->getUploadPaused() && !isRecording()) {
      videoUploader->resumeUpload();
      Serial.println("DEBUG: Resumed uploads after WiFi reconnection");
    }
    
    videoUploader->populateUploadQueue();
    startCameraServer(); // Restart HTTP server
  }
}

void updateWiFiStatusLED() {
  unsigned long now = millis();
  
  if (isRecording()) {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;
    flashCount = 0;
    return;
  }
  
  if (!wifi_connected) {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    flashCount = 0;
    return;
  }
  
  if (now - lastLEDBlink >= STATUS_CHECK_MS) {
    lastLEDBlink = now;
    flashStartTime = now;
    flashCount = 0;
    digitalWrite(LED_PIN, LOW);
    
    Serial.printf("LED Flash - Uploading: %s, Queue: %d\n", 
                  videoUploader->getIsUploading() ? "YES" : "NO", videoUploader->getQueueSize());
  }
  
  unsigned long timeSinceFlashStart = now - flashStartTime;
  
  if (flashStartTime > 0 && timeSinceFlashStart < 1000) {
    
    if (videoUploader->getIsUploading()) {
      if (timeSinceFlashStart < 100) {
        digitalWrite(LED_PIN, HIGH);
      } else if (timeSinceFlashStart < 200) {
        digitalWrite(LED_PIN, LOW);
      } else if (timeSinceFlashStart < 300) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    } else {
      if (timeSinceFlashStart < 200) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

bool isRecording() {
  unsigned long now = millis();
  bool recording = recording_active && (now - lastCaptureTime) < captureDuration;
  return recording;
}

// HTTP Server Functions
void startCameraServer() {
  if (camera_httpd != NULL) {
    httpd_stop(camera_httpd);
    camera_httpd = NULL;
  }
  
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HTTP_PORT;
  
  httpd_uri_t status_uri = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t control_uri = {
    .uri       = "/control",
    .method    = HTTP_POST,
    .handler   = control_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t command_uri = {
    .uri       = "/command",
    .method    = HTTP_POST,
    .handler   = command_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t recording_config_uri = {
    .uri       = "/recording-config",
    .method    = HTTP_POST,
    .handler   = recording_config_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t apply_settings_uri = {
    .uri       = "/apply-settings",
    .method    = HTTP_POST,
    .handler   = apply_settings_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &control_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &command_uri);
    httpd_register_uri_handler(camera_httpd, &recording_config_uri);
    httpd_register_uri_handler(camera_httpd, &apply_settings_uri);
    
    Serial.printf("Camera HTTP server started on port %d\n", HTTP_PORT);
  } else {
    Serial.println("Failed to start camera HTTP server");
  }
}

esp_err_t status_handler(httpd_req_t *req) {
  DynamicJsonDocument doc(1024);
  
  doc["device_type"] = "edge_monitor";
  doc["wifi_connected"] = wifi_connected;
  doc["is_recording"] = isRecording();
  doc["system_paused"] = system_paused;
  doc["camera_ready"] = camera_sign;
  doc["sd_ready"] = sd_sign;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["file_count"] = circularBuffer->countVideoFiles();
  doc["storage_used"] = circularBuffer->getVideoStorageUsed() / (1024 * 1024);
  
  // Current settings
  JsonObject settings = doc.createNestedObject("settings");
  settings["framesize"] = cameraSettings.framesize;
  settings["quality"] = cameraSettings.quality;
  settings["brightness"] = cameraSettings.brightness;
  settings["contrast"] = cameraSettings.contrast;
  settings["saturation"] = cameraSettings.saturation;
  settings["capture_interval"] = captureInterval / 1000;
  settings["capture_duration"] = captureDuration / 1000;
  settings["stream_interval"] = imageStreamInterval / 1000;
  
  // Resolution info
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    framesize_t framesize = s->status.framesize;
    switch(framesize) {
      case FRAMESIZE_QQVGA: doc["resolution"] = "160x120"; break;
      case FRAMESIZE_QCIF: doc["resolution"] = "176x144"; break;
      case FRAMESIZE_HQVGA: doc["resolution"] = "240x176"; break;
      case FRAMESIZE_QVGA: doc["resolution"] = "320x240"; break;
      case FRAMESIZE_CIF: doc["resolution"] = "400x296"; break;
      case FRAMESIZE_HVGA: doc["resolution"] = "480x320"; break;
      case FRAMESIZE_VGA: doc["resolution"] = "640x480"; break;
      case FRAMESIZE_SVGA: doc["resolution"] = "800x600"; break;
      case FRAMESIZE_XGA: doc["resolution"] = "1024x768"; break;
      case FRAMESIZE_HD: doc["resolution"] = "1280x720"; break;
      case FRAMESIZE_SXGA: doc["resolution"] = "1280x1024"; break;
      case FRAMESIZE_UXGA: doc["resolution"] = "1600x1200"; break;
      default: doc["resolution"] = "Unknown"; break;
    }
  }
  
  String response;
  serializeJson(doc, response);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, response.c_str(), response.length());
  
  return ESP_OK;
}

esp_err_t control_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;
  
  if (remaining >= sizeof(buf)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
    return ESP_FAIL;
  }
  
  ret = httpd_req_recv(req, buf, remaining);
  if (ret <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    }
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, buf);
  
  String var = doc["var"];
  int val = doc["val"];
  
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  
  if (var == "framesize") {
    if (s->pixformat == PIXFORMAT_JPEG) {
      res = s->set_framesize(s, (framesize_t)val);
      cameraSettings.framesize = val;
    }
  } else if (var == "quality") {
    res = s->set_quality(s, val);
    cameraSettings.quality = val;
  } else if (var == "brightness") {
    res = s->set_brightness(s, val);
    cameraSettings.brightness = val;
  } else if (var == "contrast") {
    res = s->set_contrast(s, val);
    cameraSettings.contrast = val;
  } else if (var == "saturation") {
    res = s->set_saturation(s, val);
    cameraSettings.saturation = val;
  }
  
  DynamicJsonDocument response(256);
  response["success"] = (res == 0);
  response["message"] = (res == 0) ? "Setting updated" : "Setting failed";
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  
  return res;
}

esp_err_t command_handler(httpd_req_t *req) {
  char buf[100];
  int ret = httpd_req_recv(req, buf, sizeof(buf));
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, buf);
  
  String command = doc["command"];
  bool success = false;
  String message = "";
  
  if (command == "start") {
    recording_active = true;
    system_paused = false;
    success = true;
    message = "Recording started";
  } else if (command == "stop") {
    recording_active = false;
    success = true;
    message = "Recording stopped";
  } else if (command == "pause") {
    system_paused = !system_paused;
    success = true;
    message = system_paused ? "System paused" : "System resumed";
  } else if (command == "restart") {
    success = true;
    message = "Restarting system";
    // Send response first, then restart
    DynamicJsonDocument response(256);
    response["success"] = success;
    response["message"] = message;
    String responseStr;
    serializeJson(response, responseStr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, responseStr.c_str(), responseStr.length());
    
    delay(1000);
    ESP.restart();
    return ESP_OK;
  } else if (command == "photo") {
    // Capture a single photo
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      String filename = "/photo_" + String(millis()) + ".jpg";
      File file = SD.open(filename, FILE_WRITE);
      if (file) {
        file.write(fb->buf, fb->len);
        file.close();
        success = true;
        message = "Photo captured: " + filename;
      } else {
        message = "Failed to save photo";
      }
      esp_camera_fb_return(fb);
    } else {
      message = "Failed to capture photo";
    }
  }
  
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["message"] = message;
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

esp_err_t recording_config_handler(httpd_req_t *req) {
  char buf[200];
  int ret = httpd_req_recv(req, buf, sizeof(buf));
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, buf);
  
  String setting = doc["setting"];
  int value = doc["value"];
  bool success = false;
  
  if (setting == "interval") {
    captureInterval = value * 1000; // Convert to milliseconds
    success = true;
  } else if (setting == "duration") {
    captureDuration = value * 1000; // Convert to milliseconds
    success = true;
  } else if (setting == "stream_interval") {
    imageStreamInterval = value * 1000; // Convert to milliseconds
    success = true;
  }
  
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["message"] = success ? "Recording setting updated" : "Invalid setting";
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

esp_err_t apply_settings_handler(httpd_req_t *req) {
  char buf[500];
  int ret = httpd_req_recv(req, buf, sizeof(buf));
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, buf);
  
  sensor_t *s = esp_camera_sensor_get();
  bool success = true;
  String message = "Settings applied successfully";
  
  // Apply camera settings
  if (doc.containsKey("framesize")) {
    int val = doc["framesize"];
    if (s->set_framesize(s, (framesize_t)val) == 0) {
      cameraSettings.framesize = val;
    } else {
      success = false;
    }
  }
  
  if (doc.containsKey("quality")) {
    int val = doc["quality"];
    if (s->set_quality(s, val) == 0) {
      cameraSettings.quality = val;
    } else {
      success = false;
    }
  }
  
  if (doc.containsKey("brightness")) {
    int val = doc["brightness"];
    if (s->set_brightness(s, val) == 0) {
      cameraSettings.brightness = val;
    } else {
      success = false;
    }
  }
  
  if (doc.containsKey("contrast")) {
    int val = doc["contrast"];
    if (s->set_contrast(s, val) == 0) {
      cameraSettings.contrast = val;
    } else {
      success = false;
    }
  }
  
  if (doc.containsKey("saturation")) {
    int val = doc["saturation"];
    if (s->set_saturation(s, val) == 0) {
      cameraSettings.saturation = val;
    } else {
      success = false;
    }
  }
  
  // Apply recording settings
  if (doc.containsKey("capture_interval")) {
    captureInterval = doc["capture_interval"].as<int>() * 1000;
  }
  
  if (doc.containsKey("capture_duration")) {
    captureDuration = doc["capture_duration"].as<int>() * 1000;
  }
  
  if (doc.containsKey("stream_interval")) {
    imageStreamInterval = doc["stream_interval"].as<int>() * 1000;
  }
  
  if (!success) {
    message = "Some settings failed to apply";
  }
  
  DynamicJsonDocument response(256);
  response["success"] = success;
  response["message"] = message;
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

void streamImageToServer() {
  if (!wifi_connected || !streamingEnabled) return;
  
  unsigned long now = millis();
  if (now - lastImageStream < imageStreamInterval) return;
  
  // Don't stream while recording
  if (isRecording()) return;
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to capture image for streaming");
    return;
  }
  
  HTTPClient http;
  http.begin(String(WEB_SERVER_URL) + "/api/upload-image");
  http.addHeader("Content-Type", "image/jpeg");
  
  int httpResponseCode = http.POST(fb->buf, fb->len);
  
  if (httpResponseCode > 0) {
    Serial.printf("Image streamed successfully: %d\n", httpResponseCode);
  } else {
    Serial.printf("Image streaming failed: %d\n", httpResponseCode);
  }
  
  http.end();
  esp_camera_fb_return(fb);
  lastImageStream = now;
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  Serial.println("\n\n=== ENHANCED EDGE MONITOR STARTING ===");
  Serial.printf("Compile Time: %s %s\n", __DATE__, __TIME__);
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  
  // Initialize class instances
  Serial.println("DEBUG: Initializing class instances...");
  circularBuffer = new CircularBuffer(MAX_STORAGE_MB, MIN_FREE_SPACE_MB, ENABLE_CIRCULAR_BUFFER);
  videoUploader = new VideoUploader(UPLOAD_URL, UPLOAD_API_KEY, UPLOAD_CHUNK_SIZE, 
                                   UPLOAD_TIMEOUT_MS, MAX_UPLOAD_RETRIES, 
                                   ENABLE_HTTPS, DELETE_AFTER_UPLOAD);
  Serial.println("DEBUG: Class instances initialized successfully");
  
  // Initialize the camera with optimized settings
  Serial.println("DEBUG: Initializing camera...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = (framesize_t)cameraSettings.framesize;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = cameraSettings.quality;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Camera init failed with error 0x%x\n", err);
    
    // Try fallback configuration
    Serial.println("DEBUG: Trying fallback camera configuration...");
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 20;
    config.fb_location = CAMERA_FB_IN_DRAM;
    
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("ERROR: Fallback camera init also failed: 0x%x\n", err);
      camera_sign = false;
    } else {
      Serial.println("DEBUG: Fallback camera configuration successful!");
      camera_sign = true;
      cameraSettings.framesize = FRAMESIZE_QQVGA;
      cameraSettings.quality = 20;
    }
  } else {
    Serial.println("DEBUG: Camera initialized successfully!");
    camera_sign = true;
  }
  
  // Initialize the SD card
  Serial.println("DEBUG: Initializing SD card...");
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("ERROR: SD card initialization failed!");
    sd_sign = false;
  } else {
    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE){
      Serial.println("ERROR: No SD card attached");
      sd_sign = false;
    } else {
      Serial.print("SD Card Type: ");
      if(cardType == CARD_MMC){
        Serial.println("MMC");
      } else if(cardType == CARD_SD){
        Serial.println("SDSC");
      } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
      } else {
        Serial.println("UNKNOWN");
      }
      sd_sign = true;
      Serial.println("DEBUG: SD card initialized successfully");
    }
  }

  // Initialize LED
  Serial.println("DEBUG: Initializing LED...");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED initialized");

  // Connect to WiFi and start HTTP server
  connectToWiFi();

  // Print system status
  Serial.println("\n=== ENHANCED SYSTEM STATUS ===");
  Serial.printf("Camera Status: %s\n", camera_sign ? "OK" : "FAILED");
  Serial.printf("SD Card Status: %s\n", sd_sign ? "OK" : "FAILED");
  Serial.printf("WiFi Status: %s\n", wifi_connected ? "CONNECTED" : "DISCONNECTED");
  Serial.printf("HTTP Server: %s\n", camera_httpd ? "RUNNING" : "FAILED");
  Serial.printf("System Ready: %s\n", (camera_sign && sd_sign && wifi_connected) ? "YES" : "NO");
  Serial.printf("Device IP: %s\n", wifi_connected ? WiFi.localIP().toString().c_str() : "N/A");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.println("===============================\n");

  recording_active = true; // Start recording by default
  Serial.printf("Video recording will begin in %d seconds\n", captureInterval/1000);
  Serial.println("=== ENHANCED SETUP COMPLETE ===\n");
}

void loop() {
  static unsigned long loopCount = 0;
  loopCount++;
  
  // Check WiFi connection periodically
  if (millis() - lastWiFiCheck >= STATUS_CHECK_MS) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }
  
  // Update WiFi status LED
  updateWiFiStatusLED();
  
  // Stream image to server periodically
  streamImageToServer();
  
  // Skip recording operations if system is paused
  if (system_paused) {
    delay(100);
    return;
  }
  
  // Force resume uploads if recording is complete and uploads are paused
  videoUploader->forceResumeUploads(lastCaptureTime, captureDuration, captureInterval);
  
  // Reset stuck upload states periodically
  videoUploader->resetStuckUploadState();
  
  // Debug output every 30 seconds
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 30000) {
    unsigned long now = millis();
    unsigned long timeSinceLastCapture = now - lastCaptureTime;
    unsigned long timeUntilNextCapture = (timeSinceLastCapture >= captureInterval) ? 0 : captureInterval - timeSinceLastCapture;
    
    Serial.printf("DEBUG - Loop: %lu, WiFi: %s, Recording: %s, Active: %s, Queue: %d\n", 
                  loopCount,
                  wifi_connected ? "ON" : "OFF",
                  isRecording() ? "YES" : "NO",
                  recording_active ? "YES" : "NO",
                  videoUploader->getQueueSize());
    Serial.printf("TIMING - NextRecording: %lu sec, StreamNext: %lu sec\n",
                  timeUntilNextCapture/1000, 
                  (imageStreamInterval - (millis() - lastImageStream))/1000);
    Serial.printf("MEMORY - Free Heap: %d, Free PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
    lastDebugPrint = millis();
  }
  
  // Recording logic - only if recording is active and system is ready
  if (recording_active && camera_sign && sd_sign) {
    unsigned long now = millis();
    unsigned long timeSinceLastCapture = now - lastCaptureTime;

    if (timeSinceLastCapture >= captureInterval) {
      Serial.printf("*** RECORDING TRIGGER *** Now: %lu, LastCapture: %lu, TimeSince: %lu\n",
                    now, lastCaptureTime, timeSinceLastCapture);
      
      // Pause any ongoing upload
      if (videoUploader->getIsUploading()) {
        videoUploader->pauseUpload();
        Serial.println("DEBUG: Pausing upload for recording");
      }
      
      // Check storage and perform cleanup if necessary
      Serial.println("DEBUG: Checking storage space...");
      if (!circularBuffer->checkAndManageStorage(videoUploader->getUploadQueue())) {
        Serial.println("ERROR: Insufficient storage space available! Skipping recording.");
        lastCaptureTime = now;
        return;
      }
      Serial.println("DEBUG: Storage check passed");
      
      String filename = getTimestampFilename();
      Serial.printf("DEBUG: Opening file for writing: %s\n", filename.c_str());
      videoFile = SD.open(filename, FILE_WRITE);
      if (!videoFile) {
        Serial.printf("ERROR: Failed to open video file: %s\n", filename.c_str());
        return;
      }
      Serial.printf("*** RECORDING STARTED *** File: %s\n", filename.c_str());
      lastCaptureTime = now;
      
      // Start capturing video frames
      int frame_count = 0;
      unsigned long recordingStartTime = millis();
      while ((millis() - lastCaptureTime) < captureDuration) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("ERROR: Failed to get framebuffer!");
          break;
        }
        videoFile.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
        frame_count += 1;
        
        // Progress indicator every 50 frames
        if (frame_count % 50 == 0) {
          Serial.printf("Recording progress: %d frames, %lu ms elapsed\n", 
                        frame_count, millis() - recordingStartTime);
        }
      }
      
      // Close the video file
      videoFile.close();
      unsigned long recordingDuration = millis() - recordingStartTime;
      Serial.printf("*** RECORDING COMPLETED *** Frames: %d, Duration: %lu ms, File: %s\n", 
                    frame_count, recordingDuration, filename.c_str());
      
      // Print updated storage info
      uint64_t fileSize = 0;
      File file = SD.open(filename);
      if (file) {
        fileSize = file.size();
        file.close();
      }
      Serial.printf("File size: %.2fMB\n", fileSize / (1024.0 * 1024.0));
      
      // Add new video to upload queue
      if (wifi_connected) {
        Serial.printf("DEBUG: Adding to upload queue: %s\n", filename.c_str());
        videoUploader->addToUploadQueue(String(filename));
        Serial.printf("DEBUG: Upload queue size now: %d\n", videoUploader->getQueueSize());
      } else {
        Serial.println("DEBUG: WiFi not connected, video not added to upload queue");
      }
      
      imageCount++;

      // Resume uploads now that recording is complete
      if (videoUploader->getUploadPaused()) {
        Serial.println("DEBUG: Resuming uploads after recording completion");
        videoUploader->resumeUpload();
      }

      Serial.printf("Next video will begin in %d seconds\n", captureInterval/1000);
    }
    
    // Process upload queue when not recording and WiFi is connected
    if (wifi_connected && !isRecording()) {
      videoUploader->processUploadQueue();
    }
  }
  
  // Small delay to prevent excessive CPU usage
  delay(100);
}