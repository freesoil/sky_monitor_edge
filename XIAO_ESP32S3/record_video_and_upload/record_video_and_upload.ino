/*
* Video recording with SD card space management, circular buffer, and background upload
* Automatically removes oldest videos when storage space is low
* Uploads videos to a web server when not recording, with intelligent pause/resume
* Refactored with modular classes for CircularBuffer and VideoUploader
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

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"
#include "CircularBuffer.h"
#include "VideoUploader.h"

const int SD_PIN_CS = 21;
const int LED_PIN = LED_BUILTIN; // Built-in LED on XIAO ESP32S3

// WiFi Configuration
const char* WIFI_SSID = "WIFI SSID";        // Replace with your WiFi SSID
const char* WIFI_PASSWORD = "WIFI Password"; // Replace with your WiFi password

// Upload Configuration
const char* UPLOAD_URL = "http://192.168.1.57:8000/upload"; // Replace with your upload endpoint
const char* UPLOAD_API_KEY = "";    // Optional API key for authentication
const long UPLOAD_CHUNK_SIZE = 8192;    // Upload chunk size in bytes (8KB)
const long UPLOAD_TIMEOUT_MS = 30000;   // Upload timeout in milliseconds
const int MAX_UPLOAD_RETRIES = 3;       // Maximum retry attempts for failed uploads
const bool ENABLE_HTTPS = false;        // Set to true if using HTTPS
const bool DELETE_AFTER_UPLOAD = true;  // Delete videos after successful upload

// Storage Management Configuration
const long MAX_STORAGE_MB = 24;  // Maximum storage to reserve for videos (MB)
const long MIN_FREE_SPACE_MB = 1; // Minimum free space to maintain (MB)
const bool ENABLE_CIRCULAR_BUFFER = true; // Enable automatic deletion of oldest videos

// NTP time configuration
const char* NTP_SERVER = "pool.ntp.org";       // NTP server for time sync
const long GMT_OFFSET_SEC = 0;                  // GMT offset in seconds
const int DAYLIGHT_OFFSET_SEC = 0;              // Daylight saving offset in seconds

// Class instances
CircularBuffer* circularBuffer;
VideoUploader* videoUploader;

File videoFile;
bool camera_sign = false;
bool sd_sign = false;
bool wifi_connected = false;
unsigned long lastCaptureTime = 0;
unsigned long captureDuration = 10000; // 10 seconds
unsigned long captureInterval = 60000; // 60 seconds
int imageCount = 0;

// WiFi status LED variables
const long STATUS_CHECK_MS = 5000;         // Status check and LED blink interval (5 seconds)
unsigned long lastWiFiCheck = 0;
unsigned long lastLEDBlink = 0;
bool ledState = false;
int flashCount = 0;                         // Track flash count for multi-flash patterns
unsigned long flashStartTime = 0;          // Track when flash sequence started

// WiFi and upload functions
void setupTime() {
  // Configure time with NTP
  Serial.println("Setting up time synchronization...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  // Wait for time to be set
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 * 2 && attempts < 20) { // Wait until we have a valid timestamp
    delay(1000);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  
  if (now > 8 * 3600 * 2) {
    Serial.println();
    Serial.println("Time synchronized successfully!");
    
    // Print current time for verification
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
    // Fallback to numbered filename if time is not available
    static int fallbackCount = 0;
    return "/video" + String(fallbackCount++) + ".avi";
  }
  
  // Format: YYYYmmddHHmmss.avi
  char timestamp[32];
  sprintf(timestamp, "/%04d%02d%02d%02d%02d%02d.avi",
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);
  
  return String(timestamp);
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
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
    
    // Setup time synchronization after WiFi connection
    setupTime();
  } else {
    wifi_connected = false;
    Serial.println();
    Serial.println("WiFi connection failed!");
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifi_connected) {
      Serial.println("WiFi connection lost. Attempting to reconnect...");
      wifi_connected = false;
      
      // Pause any ongoing uploads
      if (videoUploader->getIsUploading()) {
        videoUploader->pauseUpload();
        Serial.println("Paused uploads due to WiFi loss");
      }
    }
    connectToWiFi();
  } else if (!wifi_connected && WiFi.status() == WL_CONNECTED) {
    // WiFi reconnected
    wifi_connected = true;
    Serial.println("WiFi reconnected successfully!");
    
    // Resume uploads if they were paused due to WiFi loss
    if (videoUploader->getUploadPaused() && !isRecording()) {
      videoUploader->resumeUpload();
      Serial.println("Resumed uploads after WiFi reconnection");
    }
    
    // Repopulate upload queue in case we missed any files
    videoUploader->populateUploadQueue();
  }
}

void updateWiFiStatusLED() {
  unsigned long now = millis();
  
  // During recording, keep LED solid ON
  if (isRecording()) {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;
    flashCount = 0;
    return;
  }
  
  // No WiFi connection - LED stays OFF
  if (!wifi_connected) {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    flashCount = 0;
    return;
  }
  
  // WiFi connected but not recording - implement flash patterns
  // Start new flash sequence every STATUS_CHECK_MS
  if (now - lastLEDBlink >= STATUS_CHECK_MS) {
    lastLEDBlink = now;
    flashStartTime = now;
    flashCount = 0;
    digitalWrite(LED_PIN, LOW); // Ensure LED starts OFF
    
    // Debug output every flash cycle
    Serial.printf("LED Flash - Uploading: %s, Queue: %d\n", 
                  videoUploader->getIsUploading() ? "YES" : "NO", videoUploader->getQueueSize());
  }
  
  // Handle flash patterns within the STATUS_CHECK_MS window
  unsigned long timeSinceFlashStart = now - flashStartTime;
  
  if (flashStartTime > 0 && timeSinceFlashStart < 1000) { // Flash sequence window (1 second)
    
    if (videoUploader->getIsUploading()) {
      // Uploading: 2 quick flashes (100ms ON, 100ms OFF, 100ms ON, 100ms OFF)
      if (timeSinceFlashStart < 100) {
        // First flash ON
        digitalWrite(LED_PIN, HIGH);
      } else if (timeSinceFlashStart < 200) {
        // First flash OFF
        digitalWrite(LED_PIN, LOW);
      } else if (timeSinceFlashStart < 300) {
        // Second flash ON
        digitalWrite(LED_PIN, HIGH);
      } else {
        // Second flash OFF and stay OFF
        digitalWrite(LED_PIN, LOW);
      }
    } else {
      // WiFi connected but not uploading: 1 flash (200ms ON)
      if (timeSinceFlashStart < 200) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
  } else {
    // Outside flash window - ensure LED is OFF
    digitalWrite(LED_PIN, LOW);
  }
}

bool isRecording() {
  // Check if we're currently recording (within the capture duration)
  unsigned long now = millis();
  return (now - lastCaptureTime) < captureDuration && (now - lastCaptureTime) < captureInterval;
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  // Initialize class instances
  circularBuffer = new CircularBuffer(MAX_STORAGE_MB, MIN_FREE_SPACE_MB, ENABLE_CIRCULAR_BUFFER);
  videoUploader = new VideoUploader(UPLOAD_URL, UPLOAD_API_KEY, UPLOAD_CHUNK_SIZE, 
                                   UPLOAD_TIMEOUT_MS, MAX_UPLOAD_RETRIES, 
                                   ENABLE_HTTPS, DELETE_AFTER_UPLOAD);
  
  // Initialize the camera
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  camera_sign = true;
  
  // Initialize the SD card
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("SD card initialization failed!");
    return;
  }

  uint8_t cardType = SD.cardType();

  // Determine if the type of SD card is available
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

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

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off
  Serial.println("LED initialized");

  // Connect to WiFi
  connectToWiFi();

  // Print storage configuration and current status
  Serial.println("\n=== Storage Management Configuration ===");
  Serial.printf("Circular Buffer: %s\n", circularBuffer->isCircularBufferEnabled() ? "ENABLED" : "DISABLED");
  circularBuffer->printStorageInfo();
  Serial.printf("Current video files: %d\n", circularBuffer->countVideoFiles());
  Serial.println("==========================================");
  
  Serial.println("\n=== Upload Configuration ===");
  Serial.printf("WiFi Status: %s\n", wifi_connected ? "CONNECTED" : "DISCONNECTED");
  Serial.printf("Upload URL: %s\n", UPLOAD_URL);
  Serial.printf("Delete after upload: %s\n", DELETE_AFTER_UPLOAD ? "YES" : "NO");
  Serial.printf("Upload chunk size: %ldKB\n", UPLOAD_CHUNK_SIZE / 1024);
  Serial.printf("Max retries: %d\n", MAX_UPLOAD_RETRIES);
  Serial.println("=====================================\n");

  // Populate initial upload queue
  if (wifi_connected) {
    videoUploader->populateUploadQueue();
  }

  Serial.printf("Video will begin in %d seconds, please be ready.\n", captureInterval/1000);
}

void loop() {
  // Check WiFi connection periodically using the configured interval
  if (millis() - lastWiFiCheck >= STATUS_CHECK_MS) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }
  
  // Update WiFi status LED (blinks when connected and not recording)
  updateWiFiStatusLED();
  
  // Force resume uploads if recording is complete and uploads are paused
  videoUploader->forceResumeUploads(lastCaptureTime, captureDuration, captureInterval);
  
  // Reset stuck upload states periodically
  videoUploader->resetStuckUploadState();
  
  // Debug output every 10 seconds
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 10000) {
    Serial.printf("DEBUG - WiFi: %s, Recording: %s, Uploading: %s, Queue: %d\n", 
                  wifi_connected ? "ON" : "OFF",
                  isRecording() ? "YES" : "NO",
                  videoUploader->getIsUploading() ? "YES" : "NO",
                  videoUploader->getQueueSize());
    lastDebugPrint = millis();
  }
  
  // Camera & SD available, start taking video
  if (camera_sign && sd_sign) {
    // Get the current time
    unsigned long now = millis();

    //If it has been more than captureInterval since the last video capture, start capturing a new video
    if ((now - lastCaptureTime) >= captureInterval) {
      
      // Pause any ongoing upload
      if (videoUploader->getIsUploading()) {
        videoUploader->pauseUpload();
        Serial.println("Pausing upload for recording");
      }
      
      // Check storage and perform cleanup if necessary
      if (!circularBuffer->checkAndManageStorage(videoUploader->getUploadQueue())) {
        Serial.println("Insufficient storage space available! Skipping recording.");
        lastCaptureTime = now; // Update time to prevent immediate retry
        return;
      }
      
      String filename = getTimestampFilename();
      videoFile = SD.open(filename, FILE_WRITE);
      if (!videoFile) {
        Serial.println("Error opening video file!");
        return;
      }
      Serial.printf("Recording video: %s\n", filename);
      lastCaptureTime = now;
      
      // Start capturing video frames
      int frame_count = 0;
      while ((millis() - lastCaptureTime) < captureDuration) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("Error getting framebuffer!");
          break;
        }
        videoFile.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
        frame_count += 1;
      }
      
      // Close the video file
      videoFile.close();
      Serial.printf("Video with %d frames saved: %s\n", frame_count, filename);
      
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
        videoUploader->addToUploadQueue(String(filename));
      }
      
      imageCount++;

      // Resume uploads now that recording is complete
      if (videoUploader->getUploadPaused()) {
        videoUploader->resumeUpload();
      }

      Serial.printf("Video will begin in %d seconds, please be ready.\n", captureInterval/1000);

      // Note: No blocking delay here - let the main loop continue for uploads and LED updates
    }
    
    // Process upload queue when not recording and WiFi is connected
    if (wifi_connected && !isRecording()) {
      videoUploader->processUploadQueue();
    }
    
    // Print status every 30 seconds
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 30000) {
      videoUploader->printUploadStatus();
      lastStatusPrint = millis();
    }
  }
  
  // Small delay to prevent excessive CPU usage
  delay(100);
} 