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
#include "ESPmDNS.h"
#include <vector>
#include <Preferences.h>  // For persistent settings storage

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"
#include "CircularBuffer.h"
#include "VideoUploader.h"
#include "Motor.h"

const int SD_PIN_CS = 21;
const int LED_PIN = LED_BUILTIN; // Built-in LED on XIAO ESP32S3

// WiFi Configuration
const char* WIFI_SSID = "Firefly";        
const char* WIFI_PASSWORD = "wawadudu"; 

// Server Configuration - ONLY PLACE TO CHANGE IP
// const char* IP = "10.190.61.153";
const char* IP = "10.195.114.153";

const int SERVER_PORT = 8000;
const int HTTP_PORT = 80;

// Web Server Configuration
String WEB_SERVER_URL = "http://" + String(IP) + ":" + String(SERVER_PORT);

// Upload Configuration (keeping original functionality)
String UPLOAD_URL = WEB_SERVER_URL + "/upload";
const char* UPLOAD_API_KEY = "";    
const long UPLOAD_CHUNK_SIZE = 8192;    
const long UPLOAD_TIMEOUT_MS = 60000;   
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

// Motor instance (Single motor on D2=GPIO3, D3=GPIO4)
Motor motor(3, 4, 10, 100);  // deadZone=10, maxSpeed=100

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

// Preferences object for persistent storage
Preferences preferences;

// Camera settings structure
struct CameraSettings {
  int framesize = FRAMESIZE_HD;  // 1280x720 - Changed to HD as default
  int quality = 20;              // Quality for HD
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
} cameraSettings;

// High resolution support configuration
const int HIGH_RES_THRESHOLD = FRAMESIZE_SVGA;  // 800x600
const int VERY_HIGH_RES_THRESHOLD = FRAMESIZE_SXGA; // 1280x1024
const int HIGH_RES_QUALITY = 25;  // Lower quality for high res (higher number = lower quality)
const int VERY_HIGH_RES_QUALITY = 35; // Even lower quality for very high res
const int HIGH_RES_FB_COUNT = 2;  // Use 2 frame buffers for high res
const int NORMAL_FB_COUNT = 1;    // Normal frame buffer count

// FPS control (milliseconds delay between frames)
unsigned long frameDelayMs = 0;  // 0 = max FPS, 33 = ~30fps, 66 = ~15fps, 100 = ~10fps
unsigned long targetFPS = 0;     // 0 = unlimited, set via API

// Error tracking for capture failures
struct CaptureStats {
  unsigned long totalCaptures = 0;
  unsigned long failedCaptures = 0;
  unsigned long lastFailureTime = 0;
  int consecutiveFailures = 0;
  bool degradedMode = false;  // Auto-downgrade resolution on failures
} captureStats;

// Function prototypes
void startCameraServer();
void streamImageToServer();
void saveSettings();
void loadSettings();
esp_err_t root_handler(httpd_req_t *req);
esp_err_t status_handler(httpd_req_t *req);
esp_err_t control_handler(httpd_req_t *req);
esp_err_t capture_handler(httpd_req_t *req);
esp_err_t command_handler(httpd_req_t *req);
esp_err_t recording_config_handler(httpd_req_t *req);
esp_err_t apply_settings_handler(httpd_req_t *req);
esp_err_t files_handler(httpd_req_t *req);
esp_err_t motor_control_handler(httpd_req_t *req);

// Settings persistence functions
void saveSettings() {
  Serial.println("\n=== SAVING SETTINGS TO FLASH ===");
  preferences.begin("camera", false); // false = read/write mode
  
  preferences.putInt("framesize", cameraSettings.framesize);
  preferences.putInt("quality", cameraSettings.quality);
  preferences.putInt("brightness", cameraSettings.brightness);
  preferences.putInt("contrast", cameraSettings.contrast);
  preferences.putInt("saturation", cameraSettings.saturation);
  preferences.putULong("capInterval", captureInterval);
  preferences.putULong("capDuration", captureDuration);
  preferences.putULong("streamInt", imageStreamInterval);
  preferences.putULong("targetFPS", targetFPS);
  preferences.putULong("frameDelay", frameDelayMs);
  
  preferences.end();
  Serial.printf("Settings saved: framesize=%d, quality=%d, fps=%lu\n", 
                cameraSettings.framesize, cameraSettings.quality, targetFPS);
  Serial.println("================================\n");
}

void loadSettings() {
  Serial.println("\n=== LOADING SETTINGS FROM FLASH ===");
  preferences.begin("camera", true); // true = read-only mode
  
  // Load with defaults if not found
  cameraSettings.framesize = preferences.getInt("framesize", FRAMESIZE_HD);
  cameraSettings.quality = preferences.getInt("quality", 20);
  cameraSettings.brightness = preferences.getInt("brightness", 0);
  cameraSettings.contrast = preferences.getInt("contrast", 0);
  cameraSettings.saturation = preferences.getInt("saturation", 0);
  captureInterval = preferences.getULong("capInterval", 60000);
  captureDuration = preferences.getULong("capDuration", 10000);
  imageStreamInterval = preferences.getULong("streamInt", 5000);
  targetFPS = preferences.getULong("targetFPS", 0);
  frameDelayMs = preferences.getULong("frameDelay", 0);
  
  preferences.end();
  Serial.printf("Settings loaded: framesize=%d, quality=%d, fps=%lu\n", 
                cameraSettings.framesize, cameraSettings.quality, targetFPS);
  Serial.println("===================================\n");
}

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
    
    // Setup mDNS for device discovery
    if (MDNS.begin("edge-monitor")) {
      Serial.println("mDNS responder started");
      MDNS.addService("http", "tcp", HTTP_PORT);
      MDNS.addServiceTxt("http", "tcp", "device_type", "edge_monitor");
      MDNS.addServiceTxt("http", "tcp", "version", "1.0");
    } else {
      Serial.println("Error setting up mDNS responder!");
    }
    
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
  config.max_uri_handlers = 10;
  config.stack_size = 8192;
  
  // Performance optimizations - run HTTP server on separate core
  config.core_id = 0;              // Run on Core 0 (separate from main loop)
  config.task_priority = 5;        // Higher priority than default (4)
  config.max_open_sockets = 7;     // Allow more concurrent connections
  
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
  
  httpd_uri_t files_uri = {
    .uri       = "/files",
    .method    = HTTP_GET,
    .handler   = files_handler,
    .user_ctx  = NULL
  };
  
  // Motor control endpoint
  httpd_uri_t motor_uri = {
    .uri       = "/motor",
    .method    = HTTP_POST,
    .handler   = motor_control_handler,
    .user_ctx  = NULL
  };
  
  // Add root endpoint for basic device detection
  httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &control_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &command_uri);
    httpd_register_uri_handler(camera_httpd, &recording_config_uri);
    httpd_register_uri_handler(camera_httpd, &apply_settings_uri);
    httpd_register_uri_handler(camera_httpd, &files_uri);
    httpd_register_uri_handler(camera_httpd, &motor_uri);
    httpd_register_uri_handler(camera_httpd, &root_uri);
    
    Serial.printf("Camera HTTP server started on port %d\n", HTTP_PORT);
    Serial.printf("Device accessible at: http://%s:%d\n", WiFi.localIP().toString().c_str(), HTTP_PORT);
  } else {
    Serial.println("Failed to start camera HTTP server");
  }
}

esp_err_t root_handler(httpd_req_t *req) {
  const char* html = "<!DOCTYPE html><html><head><title>ESP32 Edge Monitor</title></head>"
                     "<body><h1>ESP32 Edge Monitor Device</h1>"
                     "<p>Device Type: edge_monitor</p>"
                     "<p>Status: Online</p>"
                     "<p>Endpoints:</p><ul>"
                     "<li><a href='/status'>/status</a> - Device status (JSON)</li>"
                     "<li><a href='/capture'>/capture</a> - Camera capture</li>"
                     "</ul></body></html>";
  
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, html, strlen(html));
  
  return ESP_OK;
}

esp_err_t status_handler(httpd_req_t *req) {
  JsonDocument doc;
  
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
  
  // Capture statistics
  JsonObject stats = doc["capture_stats"].to<JsonObject>();
  stats["total_captures"] = captureStats.totalCaptures;
  stats["failed_captures"] = captureStats.failedCaptures;
  stats["failure_rate"] = (captureStats.totalCaptures > 0) ? 
                          (captureStats.failedCaptures * 100.0 / captureStats.totalCaptures) : 0;
  stats["consecutive_failures"] = captureStats.consecutiveFailures;
  stats["last_failure"] = captureStats.lastFailureTime;
  stats["degraded_mode"] = captureStats.degradedMode;
  
  // Current settings
  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["framesize"] = cameraSettings.framesize;
  settings["quality"] = cameraSettings.quality;
  settings["brightness"] = cameraSettings.brightness;
  settings["contrast"] = cameraSettings.contrast;
  settings["saturation"] = cameraSettings.saturation;
  settings["capture_interval"] = captureInterval / 1000;
  settings["capture_duration"] = captureDuration / 1000;
  settings["stream_interval"] = imageStreamInterval / 1000;
  settings["target_fps"] = targetFPS;
  settings["frame_delay_ms"] = frameDelayMs;
  
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
  
  JsonDocument doc;
  deserializeJson(doc, buf);
  
  String var = doc["var"];
  int val = doc["val"];
  
  Serial.printf("\n=== CAMERA SETTING CHANGE ===\n");
  Serial.printf("Variable: %s, Value: %d\n", var.c_str(), val);
  Serial.printf("Memory before - Heap: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
  
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  
  if (var == "framesize") {
    if (s->pixformat == PIXFORMAT_JPEG) {
      Serial.printf("Changing framesize from %d to %d\n", cameraSettings.framesize, val);
      
      // Test if we can get a frame before changing
      camera_fb_t *test_fb = esp_camera_fb_get();
      if (test_fb) {
        Serial.printf("Pre-change test: Successfully captured frame (%d bytes)\n", test_fb->len);
        esp_camera_fb_return(test_fb);
      } else {
        Serial.println("WARNING: Cannot capture frame BEFORE resolution change!");
      }
      
            res = s->set_framesize(s, (framesize_t)val);
       if (res == 0) {
          cameraSettings.framesize = val;
          Serial.println("Framesize change successful");
          
          // Test capture after change
          delay(100);  // Give camera time to adjust
          test_fb = esp_camera_fb_get();
          if (test_fb) {
            Serial.printf("Post-change test: Successfully captured frame at new resolution (%d bytes)\n", test_fb->len);
            esp_camera_fb_return(test_fb);
            
            // Save settings to flash
            saveSettings();
          } else {
            Serial.println("ERROR: Cannot capture frame AFTER resolution change!");
            Serial.println("This resolution may not work with current buffer configuration!");
            Serial.println("Consider restarting device to reinitialize camera with new settings.");
            Serial.println("Settings NOT saved due to test failure.");
          }
        } else {
          Serial.printf("ERROR: Framesize change failed with error: %d\n", res);
        }
    }
  } else if (var == "quality") {
    Serial.printf("Changing quality from %d to %d\n", cameraSettings.quality, val);
    res = s->set_quality(s, val);
    cameraSettings.quality = val;
    if (res == 0) {
      Serial.println("Quality change successful");
    } else {
      Serial.printf("ERROR: Quality change failed with error: %d\n", res);
    }
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
  
  Serial.printf("Memory after - Heap: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
  Serial.println("===========================\n");
  
  JsonDocument response;
  response["success"] = (res == 0);
  
  // Add detailed feedback for framesize changes
  if (var == "framesize") {
    if (res == 0) {
      // Test if camera can capture at new resolution
      camera_fb_t *test_fb = esp_camera_fb_get();
      if (test_fb) {
        response["message"] = "Framesize updated and tested OK";
        response["test_passed"] = true;
        response["frame_size_bytes"] = test_fb->len;
        esp_camera_fb_return(test_fb);
      } else {
        response["message"] = "Framesize changed but capture test FAILED! Device restart recommended.";
        response["test_passed"] = false;
        response["warning"] = "Camera may not work at this resolution without restart";
      }
    } else {
      response["message"] = "Setting failed";
    }
  } else {
    response["message"] = (res == 0) ? "Setting updated" : "Setting failed";
  }
  
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
  
  // Don't capture while recording (camera is busy)
  if (isRecording()) {
    const char* msg = "Camera busy: recording in progress";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, msg, strlen(msg));
    return ESP_OK;
  }
  
  fb = esp_camera_fb_get();
  if (!fb) {
    // Suppress spam - camera busy
    // Serial.println("Camera capture failed");
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
  
  JsonDocument doc;
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
    JsonDocument response;
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
    if (isRecording()) {
      message = "Cannot capture photo: recording in progress";
    } else {
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
  } else if (command == "list_files") {
    // List all files on SD card
    Serial.println("=== MANUAL FILE LIST ===");
    File root = SD.open("/");
    if (root) {
      File entry = root.openNextFile();
      int count = 0;
      while (entry && count < 50) {
        if (!entry.isDirectory()) {
          Serial.printf("  File: '%s' - %lu bytes\n", entry.name(), (unsigned long)entry.size());
          count++;
        }
        entry.close();
        entry = root.openNextFile();
      }
      root.close();
      success = true;
      message = String("Listed ") + count + " files (check Serial Monitor)";
    } else {
      message = "Failed to open root directory";
    }
  } else if (command == "test_sd") {
    // Test SD card write/read
    String testFile = "/sd_test.txt";
    Serial.println("=== SD CARD TEST ===");
    
    // Write test
    File f = SD.open(testFile, FILE_WRITE);
    if (f) {
      String testData = "SD card test at " + String(millis());
      f.print(testData);
      f.close();
      Serial.println("Write: OK");
      delay(100);
      
      // Read test
      f = SD.open(testFile, FILE_READ);
      if (f) {
        String readData = f.readString();
        f.close();
        Serial.printf("Read: OK (%d bytes)\n", readData.length());
        
        // Delete test
        if (SD.remove(testFile)) {
          Serial.println("Delete: OK");
          success = true;
          message = "SD card test passed!";
        } else {
          message = "SD delete failed";
        }
      } else {
        message = "SD read failed";
      }
    } else {
      message = "SD write failed";
    }
  } else if (command == "test_camera") {
    // Test camera capture capability
    Serial.println("\n=== CAMERA CAPTURE TEST ===");
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      Serial.printf("Current settings: framesize=%d, quality=%d\n", 
                    s->status.framesize, s->status.quality);
      Serial.printf("Memory: Heap=%d, PSRAM=%d\n", ESP.getFreeHeap(), ESP.getFreePsram());
      
      // Try to capture 3 frames
      int successes = 0;
      int failures = 0;
      unsigned long totalTime = 0;
      
      for (int i = 0; i < 3; i++) {
        unsigned long start = millis();
        camera_fb_t *fb = esp_camera_fb_get();
        unsigned long elapsed = millis() - start;
        
        if (fb) {
          successes++;
          totalTime += elapsed;
          Serial.printf("  Test %d: ✓ SUCCESS - %d bytes, %dx%d, took %lu ms\n", 
                        i+1, fb->len, fb->width, fb->height, elapsed);
          esp_camera_fb_return(fb);
        } else {
          failures++;
          Serial.printf("  Test %d: ✗ FAILED after %lu ms\n", i+1, elapsed);
        }
        delay(100);
      }
      
      Serial.printf("Results: %d/%d successful", successes, successes + failures);
      if (successes > 0) {
        Serial.printf(" (avg %lu ms per frame, ~%.1f FPS possible)\n", 
                      totalTime/successes, 1000.0/(totalTime/successes));
      } else {
        Serial.println();
      }
      Serial.println("===========================\n");
      
      if (successes == 3) {
        success = true;
        message = String("Camera test PASSED (") + (1000/(totalTime/successes)) + " FPS capable)";
      } else if (successes > 0) {
        success = true;
        message = String("Camera test PARTIAL (") + successes + "/3 frames captured)";
      } else {
        success = false;
        message = "Camera test FAILED - cannot capture frames! Try restarting device.";
      }
    } else {
      message = "Failed to get camera sensor";
    }
  } else if (command == "clear_sd") {
    // Delete all video files from SD card
    Serial.println("=== CLEARING SD CARD ===");
    File root = SD.open("/");
    if (root) {
      int deletedCount = 0;
      int failedCount = 0;
      
      // First pass: collect filenames (can't delete while iterating)
      std::vector<String> filesToDelete;
      File entry = root.openNextFile();
      while (entry) {
        if (!entry.isDirectory()) {
          String fileName = entry.name();
          // Delete .avi and .jpg files, but keep system files
          if (fileName.endsWith(".avi") || fileName.endsWith(".jpg") || 
              (fileName.startsWith("photo_") && fileName.endsWith(".jpg"))) {
            filesToDelete.push_back("/" + fileName);
          }
        }
        entry.close();
        entry = root.openNextFile();
      }
      root.close();
      
      // Second pass: delete files
      for (const String& filePath : filesToDelete) {
        Serial.printf("Deleting: %s\n", filePath.c_str());
        if (SD.remove(filePath.c_str())) {
          deletedCount++;
        } else {
          Serial.printf("Failed to delete: %s\n", filePath.c_str());
          failedCount++;
        }
      }
      
      // Clear upload queue
      videoUploader->clearUploadQueue();
      
      Serial.printf("Deleted: %d files, Failed: %d files\n", deletedCount, failedCount);
      success = true;
      message = String("Cleared SD card: deleted ") + deletedCount + " files";
      if (failedCount > 0) {
        message += String(", failed: ") + failedCount;
      }
    } else {
      message = "Failed to open root directory";
    }
  }
  
  JsonDocument response;
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
  
  JsonDocument doc;
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
  } else if (setting == "fps") {
    // Set target FPS (0 = unlimited)
    targetFPS = value;
    if (value == 0) {
      frameDelayMs = 0;  // No delay = max FPS
    } else if (value > 0 && value <= 60) {
      frameDelayMs = 1000 / value;  // Calculate delay for target FPS
    } else {
      success = false;
    }
    if (value >= 0 && value <= 60) {
      success = true;
      Serial.printf("FPS set to %d (frame delay: %lu ms)\n", value, frameDelayMs);
    }
  } else if (setting == "frame_delay") {
    // Direct frame delay control in milliseconds
    frameDelayMs = value;
    targetFPS = (value > 0) ? (1000 / value) : 0;
    success = true;
    Serial.printf("Frame delay set to %lu ms (approx %lu FPS)\n", frameDelayMs, targetFPS);
  }
  
  // Save settings to flash if successful
  if (success) {
    saveSettings();
  }
  
  JsonDocument response;
  response["success"] = success;
  response["message"] = success ? "Recording setting updated (saved to flash)" : "Invalid setting";
  
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
  
  JsonDocument doc;
  deserializeJson(doc, buf);
  
  sensor_t *s = esp_camera_sensor_get();
  bool success = true;
  String message = "Settings applied successfully";
  
  // Apply camera settings
  if (doc["framesize"].is<int>()) {
    int val = doc["framesize"];
    if (s->set_framesize(s, (framesize_t)val) == 0) {
      cameraSettings.framesize = val;
    } else {
      success = false;
    }
  }
  
  if (doc["quality"].is<int>()) {
    int val = doc["quality"];
    if (s->set_quality(s, val) == 0) {
      cameraSettings.quality = val;
    } else {
      success = false;
    }
  }
  
  if (doc["brightness"].is<int>()) {
    int val = doc["brightness"];
    if (s->set_brightness(s, val) == 0) {
      cameraSettings.brightness = val;
    } else {
      success = false;
    }
  }
  
  if (doc["contrast"].is<int>()) {
    int val = doc["contrast"];
    if (s->set_contrast(s, val) == 0) {
      cameraSettings.contrast = val;
    } else {
      success = false;
    }
  }
  
  if (doc["saturation"].is<int>()) {
    int val = doc["saturation"];
    if (s->set_saturation(s, val) == 0) {
      cameraSettings.saturation = val;
    } else {
      success = false;
    }
  }
  
  // Apply recording settings
  if (doc["capture_interval"].is<int>()) {
    captureInterval = doc["capture_interval"].as<int>() * 1000;
  }
  
  if (doc["capture_duration"].is<int>()) {
    captureDuration = doc["capture_duration"].as<int>() * 1000;
  }
  
  if (doc["stream_interval"].is<int>()) {
    imageStreamInterval = doc["stream_interval"].as<int>() * 1000;
  }
  
  // FPS control
  if (doc["fps"].is<int>()) {
    int fps = doc["fps"];
    targetFPS = fps;
    if (fps == 0) {
      frameDelayMs = 0;
    } else if (fps > 0 && fps <= 60) {
      frameDelayMs = 1000 / fps;
    }
    Serial.printf("FPS set to %d (frame delay: %lu ms)\n", fps, frameDelayMs);
  }
  
  if (doc["frame_delay"].is<int>()) {
    frameDelayMs = doc["frame_delay"];
    targetFPS = (frameDelayMs > 0) ? (1000 / frameDelayMs) : 0;
    Serial.printf("Frame delay set to %lu ms (approx %lu FPS)\n", frameDelayMs, targetFPS);
  }
  
  if (!success) {
    message = "Some settings failed to apply";
  } else {
    // Save all settings to flash if successful
    Serial.println("All settings applied successfully - saving to flash");
    saveSettings();
    message += " (saved to flash)";
  }
  
  JsonDocument response;
  response["success"] = success;
  response["message"] = message;
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

esp_err_t files_handler(httpd_req_t *req) {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();
  
  File root = SD.open("/");
  if (root) {
    File file = root.openNextFile();
    int count = 0;
    while (file && count < 50) {
      if (!file.isDirectory()) {
        JsonObject fileObj = files.add<JsonObject>();
        fileObj["name"] = String(file.name());
        fileObj["size"] = file.size();
        fileObj["path"] = "/" + String(file.name());
      }
      file.close();
      file = root.openNextFile();
      count++;
    }
    root.close();
  }
  
  doc["upload_queue_size"] = videoUploader->getQueueSize();
  doc["total_files"] = files.size();
  
  String response;
  serializeJson(doc, response);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, response.c_str(), response.length());
  
  return ESP_OK;
}

esp_err_t motor_control_handler(httpd_req_t *req) {
  char buf[100];
  int ret = httpd_req_recv(req, buf, sizeof(buf));
  if (ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
  }
  buf[ret] = '\0';
  
  JsonDocument doc;
  deserializeJson(doc, buf);
  
  // Check if speed key exists and is valid
  if (!doc["speed"].is<int>()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'speed' parameter");
    return ESP_FAIL;
  }
  
  int speed = doc["speed"];
  
  // Clamp speed to -100 to 100 range
  speed = constrain(speed, -100, 100);
  
  // Apply to motor (dead zone is handled in Motor class)
  motor.setSpeed(speed);
  
  Serial.printf("Motor speed set to: %d\n", speed);
  
  JsonDocument response;
  response["success"] = true;
  response["speed"] = speed;
  response["message"] = "Motor speed updated";
  
  String responseStr;
  serializeJson(response, responseStr);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, responseStr.c_str(), responseStr.length());
  
  return ESP_OK;
}

void streamImageToServer() {
  if (!wifi_connected || !streamingEnabled) {
    return;
  }
  
  unsigned long now = millis();
  if (now - lastImageStream < imageStreamInterval) return;
  
  // Don't stream while recording or uploading (camera/SD card busy)
  if (isRecording() || videoUploader->getIsUploading()) {
    return;
  }
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    // Don't spam error messages - camera might be busy
    lastImageStream = now;
    return;
  }
  
  String uploadImageURL = String(WEB_SERVER_URL) + "/api/upload-image";
  Serial.printf("DEBUG: Streaming image to: %s (size: %d bytes)\n", uploadImageURL.c_str(), fb->len);
  
  HTTPClient http;
  http.setTimeout(10000); // 10 second timeout
  
  if (!http.begin(uploadImageURL)) {
    Serial.printf("ERROR: Failed to begin HTTP connection to %s\n", uploadImageURL.c_str());
    esp_camera_fb_return(fb);
    return;
  }
  
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Content-Length", String(fb->len));
  
  int httpResponseCode = http.POST(fb->buf, fb->len);
  
  if (httpResponseCode > 0) {
    Serial.printf("SUCCESS: Image streamed to server - Response: %d\n", httpResponseCode);
    if (httpResponseCode == 200) {
      String response = http.getString();
      if (response.length() > 0 && response.length() < 200) {
        Serial.printf("Server response: %s\n", response.c_str());
      }
    }
  } else {
    Serial.printf("ERROR: Image streaming failed - HTTP error code: %d\n", httpResponseCode);
    Serial.printf("ERROR: HTTP error: %s\n", http.errorToString(httpResponseCode).c_str());
    Serial.printf("DEBUG: Server URL: %s\n", WEB_SERVER_URL.c_str());
    Serial.printf("DEBUG: WiFi connected: %s, RSSI: %d dBm\n", 
                  wifi_connected ? "YES" : "NO", WiFi.RSSI());
  }
  
  http.end();
  esp_camera_fb_return(fb);
  lastImageStream = now;
}

void setup() {
  Serial.begin(115200);
  
  Serial.println("\n\n=== ENHANCED EDGE MONITOR STARTING ===");
  Serial.printf("Compile Time: %s %s\n", __DATE__, __TIME__);
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  
  // === STAGE 0: LOAD SAVED SETTINGS ===
  Serial.println("STAGE 0: Loading saved settings from flash...");
  loadSettings();
  
  // Auto-configure FPS for UXGA if not set
  if (cameraSettings.framesize >= FRAMESIZE_UXGA && targetFPS == 0) {
    targetFPS = 8;  // Auto-limit to 8 FPS for UXGA
    frameDelayMs = 1000 / targetFPS;
    Serial.println("⚠️  UXGA detected with unlimited FPS - auto-limiting to 8 FPS for stability");
  }
  
  Serial.printf("Will initialize camera with: framesize=%d, quality=%d, fps=%lu\n",
                cameraSettings.framesize, cameraSettings.quality, targetFPS);
  
  // === STAGE 1: BASIC INITIALIZATION (Low Power) ===
  Serial.println("STAGE 1: Basic system initialization...");
  
  // Initialize LED first (minimal power consumption)
  Serial.println("DEBUG: Initializing LED...");
  pinMode(LED_PIN, OUTPUT);
  
  // STAGE 1 LED PATTERN: Single long blink (2 seconds ON)
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED STAGE 1: Single long blink - Basic init starting");
  delay(2000);
  digitalWrite(LED_PIN, LOW);
  delay(500);
  
  // Initialize motor
  Serial.println("DEBUG: Initializing motor...");
  motor.init();
  Serial.println("DEBUG: Motor initialized successfully");
  
  // Initialize basic class instances (no hardware access yet)
  Serial.println("DEBUG: Initializing class instances...");
  circularBuffer = new CircularBuffer(MAX_STORAGE_MB, MIN_FREE_SPACE_MB, ENABLE_CIRCULAR_BUFFER);
  videoUploader = new VideoUploader(UPLOAD_URL, UPLOAD_API_KEY, UPLOAD_CHUNK_SIZE, 
                                   UPLOAD_TIMEOUT_MS, MAX_UPLOAD_RETRIES, 
                                   ENABLE_HTTPS, DELETE_AFTER_UPLOAD);
  Serial.println("DEBUG: Class instances initialized successfully");
  
  // STAGE 1 SUCCESS: Two quick blinks
  Serial.println("LED STAGE 1: Two quick blinks - Basic init SUCCESS");
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  // Power stabilization delay with heartbeat pattern
  Serial.println("DEBUG: Waiting for power stabilization (3 seconds)...");
  for (int i = 3; i > 0; i--) {
    Serial.printf("Power stabilization: %d seconds remaining\n", i);
    // Heartbeat pattern during stabilization
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(700); // Rest of the second
  }
  
  // === STAGE 2: SD CARD INITIALIZATION ===
  Serial.println("\nSTAGE 2: SD card initialization...");
  
  // STAGE 2 LED PATTERN: Three quick blinks (SD init starting)
  Serial.println("LED STAGE 2: Three quick blinks - SD init starting");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
  delay(500);
  
  // Initialize the SD card (moderate power consumption)
  Serial.println("DEBUG: Initializing SD card...");
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("ERROR: SD card initialization failed!");
    sd_sign = false;
    // STAGE 2 FAILURE: Rapid blinking (10 times)
    Serial.println("LED STAGE 2: Rapid blinking - SD FAILED");
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  } else {
    uint8_t cardType = SD.cardType();
    if(cardType == CARD_NONE){
      Serial.println("ERROR: No SD card attached");
      sd_sign = false;
      // STAGE 2 FAILURE: Rapid blinking (10 times)
      Serial.println("LED STAGE 2: Rapid blinking - No SD card");
      for (int i = 0; i < 10; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
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
      
      // STAGE 2 SUCCESS: Four quick blinks
      Serial.println("LED STAGE 2: Four quick blinks - SD SUCCESS");
      for (int i = 0; i < 4; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
      }
    }
  }
  
  // Short delay after SD initialization
  Serial.println("DEBUG: SD initialization complete, brief pause...");
  delay(500);
  
  // === STAGE 3: WIFI INITIALIZATION ===
  Serial.println("\nSTAGE 3: WiFi initialization...");
  
  // STAGE 3 LED PATTERN: Five quick blinks (WiFi init starting)
  Serial.println("LED STAGE 3: Five quick blinks - WiFi init starting");
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(120);
    digitalWrite(LED_PIN, LOW);
    delay(120);
  }
  delay(500);
  
  // Connect to WiFi (high power consumption during connection)
  Serial.println("DEBUG: Starting WiFi connection (HIGH POWER STAGE)...");
  connectToWiFi();
  
  // WiFi result LED feedback
  if (wifi_connected) {
    // STAGE 3 SUCCESS: Six quick blinks
    Serial.println("LED STAGE 3: Six quick blinks - WiFi SUCCESS");
    for (int i = 0; i < 6; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  } else {
    // STAGE 3 FAILURE: Very rapid blinking (15 times)
    Serial.println("LED STAGE 3: Very rapid blinking - WiFi FAILED");
    for (int i = 0; i < 15; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(80);
      digitalWrite(LED_PIN, LOW);
      delay(80);
    }
  }
  
  // Short delay after WiFi
  Serial.println("DEBUG: WiFi initialization complete, brief pause...");
  delay(1000);
  
  // === STAGE 4: CAMERA INITIALIZATION (Highest Power) ===
  Serial.println("\nSTAGE 4: Camera initialization (final stage)...");
  
  // STAGE 4 LED PATTERN: Seven quick blinks (Camera init starting - HIGHEST POWER!)
  Serial.println("LED STAGE 4: Seven quick blinks - Camera init starting (HIGHEST POWER!)");
  for (int i = 0; i < 7; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  delay(1000); // Longer pause before highest power operation
  
  // Initialize the camera with optimized settings (highest power consumption)
  Serial.println("DEBUG: Initializing camera (CRITICAL POWER MOMENT)...");
  size_t psramSize = ESP.getFreePsram();
  Serial.printf("Pre-camera init - Free Heap: %d, Free PSRAM: %d\n", ESP.getFreeHeap(), psramSize);
  
  // Check if PSRAM is available
  bool hasPSRAM = (psramSize > 0);
  if (!hasPSRAM) {
    Serial.println("WARNING: No PSRAM detected! Camera will use lower resolution.");
    Serial.println("To enable PSRAM in Arduino IDE:");
    Serial.println("  Tools -> PSRAM -> OPI PSRAM (or Enabled)");
  }
  
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
  
  // Auto-configure based on PSRAM availability and resolution
  if (hasPSRAM) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.frame_size = (framesize_t)cameraSettings.framesize;
    
    // Adaptive settings for high resolution - Multi-tier system
    if (cameraSettings.framesize >= FRAMESIZE_UXGA) {
      // UXGA (1600x1200) - MAXIMUM resolution - Special handling
      Serial.println("========================================");
      Serial.println("UXGA (1600x1200) MAXIMUM RESOLUTION MODE");
      Serial.println("========================================");
      config.fb_count = HIGH_RES_FB_COUNT;
      
      // Force aggressive quality settings for UXGA
      if (cameraSettings.quality < 35) {
        Serial.printf("WARNING: Quality %d too high for UXGA, forcing to 35\n", cameraSettings.quality);
        config.jpeg_quality = 35;
      } else {
        config.jpeg_quality = cameraSettings.quality;
      }
      
      Serial.printf("Configuration: %d frame buffers, quality %d\n", 
                    config.fb_count, config.jpeg_quality);
      Serial.printf("Expected frame size: ~%d KB (depends on scene complexity)\n", 
                    config.jpeg_quality <= 35 ? 150 : 
                    config.jpeg_quality <= 40 ? 120 : 100);
      Serial.println("Recommendations for UXGA:");
      Serial.println("  - Use FPS 5-10 maximum");
      Serial.println("  - Quality 35-40 for stability");
      Serial.println("  - Adequate power supply (5V 2A minimum)");
      Serial.println("  - Monitor capture failure rate");
      Serial.println("========================================");
      
    } else if (cameraSettings.framesize >= VERY_HIGH_RES_THRESHOLD) {
      // SXGA (1280x1024) - Very high resolution
      Serial.printf("VERY HIGH resolution mode: SXGA (using %d frame buffers, quality %d)\n", 
                    HIGH_RES_FB_COUNT, VERY_HIGH_RES_QUALITY);
      config.fb_count = HIGH_RES_FB_COUNT;
      config.jpeg_quality = max(cameraSettings.quality, VERY_HIGH_RES_QUALITY);
      
      Serial.printf("Expected frame size: ~%d KB\n", 
                    config.jpeg_quality <= 30 ? 200 : 150);
      Serial.println("Recommended FPS: 8-12 for stability");
      
    } else if (cameraSettings.framesize >= HIGH_RES_THRESHOLD) {
      // High resolution (800x600 to 1024x768)
      Serial.printf("High resolution mode: %d (using %d frame buffers, quality %d)\n", 
                    cameraSettings.framesize, HIGH_RES_FB_COUNT, HIGH_RES_QUALITY);
      config.fb_count = HIGH_RES_FB_COUNT;
      config.jpeg_quality = max(cameraSettings.quality, HIGH_RES_QUALITY);
      
      // For XGA (1024x768), use quality 30
      if (cameraSettings.framesize >= FRAMESIZE_XGA) {
        config.jpeg_quality = max(config.jpeg_quality, 30);
        Serial.printf("XGA resolution - quality adjusted to %d\n", config.jpeg_quality);
      }
    } else {
      // Normal resolution (VGA and below)
      Serial.printf("Normal resolution mode: %d (using %d frame buffer, quality %d)\n",
                    cameraSettings.framesize, NORMAL_FB_COUNT, cameraSettings.quality);
      config.fb_count = NORMAL_FB_COUNT;
      config.jpeg_quality = cameraSettings.quality;
    }
  } else {
    Serial.println("No PSRAM - using QQVGA with DRAM");
    config.frame_size = FRAMESIZE_QQVGA;  // 160x120 (small but works)
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.jpeg_quality = 20;  // Lower quality for smaller size
    config.fb_count = 1;
    cameraSettings.framesize = FRAMESIZE_QQVGA;
    cameraSettings.quality = 20;
  }
  
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  // Check if we have enough PSRAM for the requested configuration
  if (hasPSRAM && cameraSettings.framesize >= HIGH_RES_THRESHOLD) {
    size_t estimatedFrameSize = 0;
    
    // Enhanced estimation accounting for quality settings
    switch(cameraSettings.framesize) {
      case FRAMESIZE_SVGA: 
        estimatedFrameSize = 80000; 
        break;   // ~80 KB
      case FRAMESIZE_XGA:  
        estimatedFrameSize = 120000; 
        break;  // ~120 KB
      case FRAMESIZE_HD:   
        estimatedFrameSize = 100000; 
        break;  // ~100 KB (widescreen, less pixels than XGA)
      case FRAMESIZE_SXGA: 
        estimatedFrameSize = config.jpeg_quality <= 30 ? 200000 : 150000;
        break;  // ~150-200 KB depending on quality
      case FRAMESIZE_UXGA: 
        // UXGA estimation based on quality
        if (config.jpeg_quality <= 35) {
          estimatedFrameSize = 150000;  // ~150 KB with quality 35
        } else if (config.jpeg_quality <= 40) {
          estimatedFrameSize = 120000;  // ~120 KB with quality 40
        } else {
          estimatedFrameSize = 100000;  // ~100 KB with quality 45+
        }
        Serial.printf("UXGA frame size estimate: %d KB (quality %d)\n", 
                      estimatedFrameSize/1024, config.jpeg_quality);
        break;
      default: 
        estimatedFrameSize = 50000; 
        break;
    }
    
    // Different safety margins for different resolutions
    float safetyMultiplier = (cameraSettings.framesize >= FRAMESIZE_UXGA) ? 2.5 : 2.0;
    size_t requiredPSRAM = (size_t)(estimatedFrameSize * config.fb_count * safetyMultiplier);
    
    Serial.printf("Estimated PSRAM required: %d KB (have: %d KB)\n", 
                  requiredPSRAM/1024, psramSize/1024);
    Serial.printf("  Per-frame estimate: %d KB × %d buffers × %.1fx safety = %d KB\n",
                  estimatedFrameSize/1024, config.fb_count, safetyMultiplier, requiredPSRAM/1024);
    
    if (psramSize < requiredPSRAM) {
      Serial.println("****************************************");
      Serial.println("⚠️  WARNING: Insufficient PSRAM for resolution!");
      Serial.printf("Need: %d KB, Have: %d KB (Deficit: %d KB)\n", 
                    requiredPSRAM/1024, psramSize/1024, (requiredPSRAM-psramSize)/1024);
      Serial.println("Consider:");
      Serial.println("  1. Using lower resolution");
      if (cameraSettings.framesize >= FRAMESIZE_UXGA) {
        Serial.println("  2. For UXGA: Quality MUST be 35-40");
        Serial.println("  3. For UXGA: FPS should be 5-10 maximum");
        Serial.println("  4. Try SXGA or HD instead");
      } else {
        Serial.println("  2. Increasing JPEG quality number (35-40)");
        Serial.println("  3. Limiting FPS to 5-10");
      }
      Serial.println("****************************************");
    } else {
      Serial.printf("✓ PSRAM sufficient (%.1f KB available for other uses)\n", 
                    (psramSize - requiredPSRAM) / 1024.0);
    }
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("\n*** CAMERA INITIALIZATION FAILED ***\n");
    Serial.printf("Error code: 0x%x\n", err);
    Serial.println("Possible causes:");
    Serial.println("  1. PSRAM not enabled (check Arduino IDE Tools -> PSRAM)");
    Serial.println("  2. Insufficient power supply");
    Serial.println("  3. Camera module not connected properly");
    Serial.println("  4. Wrong board selected in Arduino IDE");
    Serial.println("************************************\n");
    
    camera_sign = false;
    
    // CAMERA FAILURE LED: Alternating pattern (5 cycles)
    Serial.println("LED STAGE 4: Alternating pattern - Camera FAILURE");
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(500);
      digitalWrite(LED_PIN, LOW);
      delay(500);
    }
  } else {
    Serial.println("✓ Camera initialized successfully!");
    Serial.printf("  Resolution: %dx%d (%s)\n", 
                  hasPSRAM ? 320 : 160, 
                  hasPSRAM ? 240 : 120,
                  hasPSRAM ? "QVGA/PSRAM" : "QQVGA/DRAM");
    Serial.printf("  JPEG Quality: %d\n", config.jpeg_quality);
    Serial.printf("  Frame Buffer: %s\n", hasPSRAM ? "PSRAM" : "DRAM");
    camera_sign = true;
    
    // CAMERA SUCCESS: Ten quick blinks
    Serial.println("LED STAGE 4: Ten quick blinks - Camera SUCCESS");
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(120);
      digitalWrite(LED_PIN, LOW);
      delay(120);
    }
  }
  
  Serial.printf("Post-camera init - Free Heap: %d, Free PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
  
  // === FINAL STAGE: SYSTEM READY ===
  Serial.println("\nFINAL STAGE: System ready...");
  
  // LED off to show normal operation
  digitalWrite(LED_PIN, LOW);

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
  Serial.println("=== STAGGERED SETUP COMPLETE ===\n");
  
  // === FINAL STATUS LED PATTERNS ===
  if (camera_sign && sd_sign && wifi_connected) {
    Serial.println("SUCCESS: All systems operational!");
    // FINAL SUCCESS: Celebration pattern (fast alternating 12 times)
    Serial.println("LED FINAL: Celebration pattern - ALL SYSTEMS SUCCESS!");
    for (int i = 0; i < 12; i++) {
      digitalWrite(LED_PIN, i % 2);
      delay(100);
    }
    // End with LED OFF for normal operation
    digitalWrite(LED_PIN, LOW);
  } else {
    Serial.println("WARNING: Some systems failed to initialize");
    Serial.printf("Status - Camera: %s, SD: %s, WiFi: %s\n", 
                  camera_sign ? "OK" : "FAIL", 
                  sd_sign ? "OK" : "FAIL", 
                  wifi_connected ? "OK" : "FAIL");
    
    // PARTIAL FAILURE: SOS pattern (3 short, 3 long, 3 short)
    Serial.println("LED FINAL: SOS pattern - PARTIAL SYSTEM FAILURE");
    // 3 short
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    delay(300);
    // 3 long
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(600);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    delay(300);
    // 3 short
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    
    // End with LED OFF
    digitalWrite(LED_PIN, LOW);
  }
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
      
      // Get current camera settings before recording
      sensor_t *s = esp_camera_sensor_get();
      if (s) {
        Serial.printf("\n=== CAMERA STATUS BEFORE RECORDING ===\n");
        Serial.printf("Current framesize: %d\n", s->status.framesize);
        Serial.printf("Current quality: %d\n", s->status.quality);
        Serial.printf("Configured framesize: %d\n", cameraSettings.framesize);
        Serial.printf("Configured quality: %d\n", cameraSettings.quality);
        Serial.printf("Memory: Heap=%d, PSRAM=%d\n", ESP.getFreeHeap(), ESP.getFreePsram());
        
        // Do a test capture before starting recording
        Serial.println("Performing pre-recording test capture...");
        camera_fb_t *test_fb = esp_camera_fb_get();
        if (test_fb) {
          Serial.printf("✓ Test capture successful: %d bytes, %dx%d\n", 
                        test_fb->len, test_fb->width, test_fb->height);
          esp_camera_fb_return(test_fb);
        } else {
          Serial.println("✗ Test capture FAILED!");
          Serial.println("Recording will likely fail. Check:");
          Serial.println("  1. Resolution too high for buffer configuration");
          Serial.println("  2. Camera needs restart after settings change");
          Serial.println("  3. Power supply insufficient");
          Serial.println("Aborting recording to prevent wasted SD writes.\n");
          return;
        }
        Serial.println("=====================================\n");
      }
      
      videoFile = SD.open(filename, FILE_WRITE);
      if (!videoFile) {
        Serial.printf("ERROR: Failed to open video file: %s\n", filename.c_str());
        return;
      }
      Serial.printf("*** RECORDING STARTED *** File: %s\n", filename.c_str());
      lastCaptureTime = now;
      
      // Start capturing video frames
      int frame_count = 0;
      int failed_frames = 0;
      size_t totalBytesWritten = 0;
      unsigned long recordingStartTime = millis();
      unsigned long lastFrameTime = millis();
      
      while ((millis() - lastCaptureTime) < captureDuration) {
        // FPS control: delay between frames if configured
        if (frameDelayMs > 0) {
          unsigned long timeSinceLastFrame = millis() - lastFrameTime;
          if (timeSinceLastFrame < frameDelayMs) {
            delay(frameDelayMs - timeSinceLastFrame);
          }
        }
        lastFrameTime = millis();
        
        camera_fb_t *fb = esp_camera_fb_get();
        captureStats.totalCaptures++;
        
        if (!fb) {
          // Track capture failure
          captureStats.failedCaptures++;
          captureStats.consecutiveFailures++;
          captureStats.lastFailureTime = millis();
          failed_frames++;
          
          Serial.printf("ERROR: Failed to get framebuffer! (consecutive: %d, total: %lu/%lu)\n", 
                        captureStats.consecutiveFailures, 
                        captureStats.failedCaptures, 
                        captureStats.totalCaptures);
          Serial.printf("DEBUG: Free Heap: %d, Free PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
          
          // If too many consecutive failures, abort recording
          if (captureStats.consecutiveFailures >= 10) {
            Serial.println("CRITICAL: Too many consecutive capture failures - aborting recording!");
            Serial.println("This likely means:");
            Serial.println("  1. Resolution too high for available memory");
            Serial.println("  2. PSRAM fragmentation");
            Serial.println("  3. Insufficient power supply");
            Serial.println("Consider lowering resolution or JPEG quality.");
            break;
          }
          
          delay(10);  // Brief delay before retry
          continue;
        }
        
        // Successful capture - reset consecutive failure counter
        captureStats.consecutiveFailures = 0;
        
        size_t bytesWritten = videoFile.write(fb->buf, fb->len);
        if (bytesWritten != fb->len) {
          Serial.printf("ERROR: Write failed! Expected %d bytes, wrote %d bytes\n", fb->len, bytesWritten);
        }
        totalBytesWritten += bytesWritten;
        
        esp_camera_fb_return(fb);
        frame_count += 1;

        // Flush to SD card every 10 frames to ensure data is written
        if (frame_count % 10 == 0) {
          videoFile.flush();
          delay(1);  // Yields CPU to other tasks including HTTP server
        }
        
        // Progress indicator every 50 frames with detailed stats
        if (frame_count % 50 == 0) {
          float failRate = (failed_frames > 0) ? (failed_frames * 100.0 / (frame_count + failed_frames)) : 0;
          Serial.printf("Recording progress: %d frames (%d failed, %.1f%% fail rate), %lu ms elapsed, %d bytes written\n", 
                        frame_count, failed_frames, failRate, millis() - recordingStartTime, totalBytesWritten);
          Serial.printf("  Memory: Heap=%d, PSRAM=%d\n", ESP.getFreeHeap(), ESP.getFreePsram());
        }
      }
      
      // Report capture statistics
      float totalFailRate = (captureStats.totalCaptures > 0) ? 
                            (captureStats.failedCaptures * 100.0 / captureStats.totalCaptures) : 0;
      Serial.printf("\n=== CAPTURE STATISTICS ===\n");
      Serial.printf("This recording: %d successful, %d failed\n", frame_count, failed_frames);
      Serial.printf("Session total: %lu successful, %lu failed (%.2f%% failure rate)\n",
                    captureStats.totalCaptures - captureStats.failedCaptures,
                    captureStats.failedCaptures, totalFailRate);
      Serial.println("=========================\n");
      
      // Flush and close the video file
      Serial.println("DEBUG: Flushing file buffer...");
      videoFile.flush();
      Serial.println("DEBUG: Closing file...");
      videoFile.close();
      Serial.printf("DEBUG: Total bytes written to file: %d\n", totalBytesWritten);
      
      // Critical: Give filesystem time to update before checking
      Serial.println("DEBUG: Waiting for filesystem to sync...");
      delay(500);
      
      unsigned long recordingDuration = millis() - recordingStartTime;
      Serial.printf("*** RECORDING COMPLETED *** Frames: %d, Duration: %lu ms, File: %s\n", 
                    frame_count, recordingDuration, filename.c_str());
      
      // Print updated storage info
      Serial.println("DEBUG: Checking file on SD card...");
      
      uint64_t fileSize = 0;
      File file = SD.open(filename, FILE_READ);
      if (file) {
        fileSize = file.size();
        file.close();
        Serial.printf("✓ Video saved successfully!\n");
        Serial.printf("  File: %s\n", filename.c_str());
        Serial.printf("  Size: %.2f MB (%llu bytes)\n", fileSize / (1024.0 * 1024.0), fileSize);
        Serial.printf("  Frames: %d\n", frame_count);
        Serial.printf("  Duration: %lu ms\n", recordingDuration);
      } else {
        Serial.printf("\n*** CRITICAL ERROR ***\n");
        Serial.printf("File was written (%d bytes) but cannot be read back!\n", totalBytesWritten);
        Serial.printf("Filename: %s\n", filename.c_str());
        Serial.println("This indicates SD card corruption or hardware issue!");
        Serial.println("Try:");
        Serial.println("  1. Remove and reinsert SD card");
        Serial.println("  2. Reformat SD card (FAT32)");
        Serial.println("  3. Try a different SD card");
        Serial.println("******************\n");
        
        // Try to verify SD card is still accessible
        uint64_t cardTotal = SD.totalBytes();
        if (cardTotal == 0) {
          Serial.println("FATAL: SD card no longer accessible!");
        }
      }
      
      // Increment and print video count
      imageCount++;
      Serial.printf("=== RECORDING SUMMARY ===\n");
      Serial.printf("Total videos recorded this session: %d\n", imageCount);
      Serial.printf("========================\n\n");
      
      // Add new video to upload queue
      if (wifi_connected) {
        Serial.printf("DEBUG: Adding to upload queue: %s\n", filename.c_str());
        videoUploader->addToUploadQueue(String(filename));
        Serial.printf("DEBUG: Upload queue size now: %d\n", videoUploader->getQueueSize());
      } else {
        Serial.println("DEBUG: WiFi not connected, video not added to upload queue");
      }

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
  delay(10);   // Reduced from 100ms for better HTTP responsiveness
} 
