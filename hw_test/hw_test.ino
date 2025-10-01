/*
* HARDWARE DIAGNOSTIC VERSION
* This version focuses on diagnosing camera and SD card hardware issues
* Use this to identify what's causing the initialization failures
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

const int SD_PIN_CS = 21;
const int LED_PIN = LED_BUILTIN; // Built-in LED on XIAO ESP32S3

// WiFi Configuration
const char* WIFI_SSID = "wwddOhYeah!";        
const char* WIFI_PASSWORD = "wawadudu"; 

void printCameraPins() {
  Serial.println("\n=== CAMERA PIN CONFIGURATION ===");
  Serial.printf("PWDN_GPIO_NUM: %d\n", PWDN_GPIO_NUM);
  Serial.printf("RESET_GPIO_NUM: %d\n", RESET_GPIO_NUM);
  Serial.printf("XCLK_GPIO_NUM: %d\n", XCLK_GPIO_NUM);
  Serial.printf("SIOD_GPIO_NUM: %d\n", SIOD_GPIO_NUM);
  Serial.printf("SIOC_GPIO_NUM: %d\n", SIOC_GPIO_NUM);
  Serial.printf("Y9_GPIO_NUM: %d\n", Y9_GPIO_NUM);
  Serial.printf("Y8_GPIO_NUM: %d\n", Y8_GPIO_NUM);
  Serial.printf("Y7_GPIO_NUM: %d\n", Y7_GPIO_NUM);
  Serial.printf("Y6_GPIO_NUM: %d\n", Y6_GPIO_NUM);
  Serial.printf("Y5_GPIO_NUM: %d\n", Y5_GPIO_NUM);
  Serial.printf("Y4_GPIO_NUM: %d\n", Y4_GPIO_NUM);
  Serial.printf("Y3_GPIO_NUM: %d\n", Y3_GPIO_NUM);
  Serial.printf("Y2_GPIO_NUM: %d\n", Y2_GPIO_NUM);
  Serial.printf("VSYNC_GPIO_NUM: %d\n", VSYNC_GPIO_NUM);
  Serial.printf("HREF_GPIO_NUM: %d\n", HREF_GPIO_NUM);
  Serial.printf("PCLK_GPIO_NUM: %d\n", PCLK_GPIO_NUM);
  Serial.printf("LED_GPIO_NUM: %d\n", LED_GPIO_NUM);
  Serial.println("===============================\n");
}

void testCameraInitialization() {
  Serial.println("=== CAMERA DIAGNOSTIC TEST ===");
  
  // Print camera configuration
  printCameraPins();
  
  // Test camera initialization with different configurations
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

  Serial.println("Testing camera initialization...");
  esp_err_t err = esp_camera_init(&config);
  
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED with error: 0x%x\n", err);
    Serial.println("Error codes:");
    Serial.println("0x20001 = ESP_ERR_CAMERA_NOT_DETECTED");
    Serial.println("0x20002 = ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE");
    Serial.println("0x20003 = ESP_ERR_CAMERA_FAILED_TO_SET_OUT_FORMAT");
    Serial.println("0x20004 = ESP_ERR_CAMERA_NOT_INITIALIZED");
    Serial.println("0xffffffff = Generic failure (often hardware)");
    
    // Try alternative configurations
    Serial.println("\nTrying alternative camera configurations...");
    
    // Test 1: Lower frame size
    Serial.println("Test 1: Trying QVGA instead of VGA...");
    config.frame_size = FRAMESIZE_QVGA;
    err = esp_camera_init(&config);
    Serial.printf("QVGA result: 0x%x\n", err);
    
    // Test 2: Different pixel format
    Serial.println("Test 2: Trying RGB565 instead of JPEG...");
    config.frame_size = FRAMESIZE_VGA;
    config.pixel_format = PIXFORMAT_RGB565;
    err = esp_camera_init(&config);
    Serial.printf("RGB565 result: 0x%x\n", err);
    
    // Test 3: Lower quality
    Serial.println("Test 3: Trying lower JPEG quality...");
    config.pixel_format = PIXFORMAT_JPEG;
    config.jpeg_quality = 20;
    err = esp_camera_init(&config);
    Serial.printf("Lower quality result: 0x%x\n", err);
    
  } else {
    Serial.println("Camera initialization SUCCESSFUL!");
    
    // Test camera functionality
    Serial.println("Testing camera capture...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("Camera capture SUCCESS! Size: %d bytes, Width: %d, Height: %d\n", 
                    fb->len, fb->width, fb->height);
      esp_camera_fb_return(fb);
    } else {
      Serial.println("Camera capture FAILED!");
    }
  }
  Serial.println("===============================\n");
}

void testSDCardInitialization() {
  Serial.println("=== SD CARD DIAGNOSTIC TEST ===");
  
  Serial.printf("Testing SD card on pin: %d\n", SD_PIN_CS);
  
  // Test SD card initialization
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("SD card initialization FAILED!");
    
    // Try different SPI settings
    Serial.println("Trying alternative SPI settings...");
    
    // Test 1: Different SPI frequency
    Serial.println("Test 1: Trying lower SPI frequency...");
    SPI.begin();
    if (SD.begin(SD_PIN_CS, SPI, 1000000)) { // 1MHz
      Serial.println("SD card SUCCESS with 1MHz SPI!");
    } else {
      Serial.println("SD card FAILED with 1MHz SPI");
    }
    
    // Test 2: Different CS pin
    Serial.println("Test 2: Trying different CS pin (GPIO 5)...");
    if (SD.begin(5)) {
      Serial.println("SD card SUCCESS with GPIO 5!");
    } else {
      Serial.println("SD card FAILED with GPIO 5");
    }
    
  } else {
    Serial.println("SD card initialization SUCCESSFUL!");
    
    // Test SD card functionality
    uint8_t cardType = SD.cardType();
    Serial.printf("SD Card Type: ");
    if(cardType == CARD_NONE){
      Serial.println("NONE");
    } else if(cardType == CARD_MMC){
      Serial.println("MMC");
    } else if(cardType == CARD_SD){
      Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    Serial.printf("SD Card Usage: %lluMB used / %lluMB total\n", usedBytes, totalBytes);
    
    // Test file operations
    Serial.println("Testing file operations...");
    File testFile = SD.open("/test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("Test data");
      testFile.close();
      Serial.println("File write SUCCESS!");
      
      File readFile = SD.open("/test.txt", FILE_READ);
      if (readFile) {
        String content = readFile.readString();
        Serial.printf("File read SUCCESS! Content: %s\n", content.c_str());
        readFile.close();
        SD.remove("/test.txt");
        Serial.println("Test file cleaned up");
      } else {
        Serial.println("File read FAILED!");
      }
    } else {
      Serial.println("File write FAILED!");
    }
  }
  Serial.println("===============================\n");
}

void testWiFiConnection() {
  Serial.println("=== WIFI DIAGNOSTIC TEST ===");
  
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connection SUCCESSFUL!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
  } else {
    Serial.println();
    Serial.println("WiFi connection FAILED!");
    Serial.printf("WiFi Status Code: %d\n", WiFi.status());
  }
  Serial.println("===============================\n");
}

void testSystemInfo() {
  Serial.println("=== SYSTEM INFORMATION ===");
  Serial.printf("ESP32 Chip Model: %s\n", ESP.getChipModel());
  Serial.printf("ESP32 Chip Revision: %d\n", ESP.getChipRevision());
  Serial.printf("ESP32 CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("ESP32 Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("ESP32 PSRAM Size: %d MB\n", ESP.getPsramSize() / (1024 * 1024));
  Serial.printf("ESP32 Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("ESP32 Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.printf("ESP32 SDK Version: %s\n", ESP.getSdkVersion());
  Serial.println("===============================\n");
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  Serial.println("\n\n=== HARDWARE DIAGNOSTIC TOOL ===");
  Serial.printf("Compile Time: %s %s\n", __DATE__, __TIME__);
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED test completed");
  
  // Run diagnostic tests
  testSystemInfo();
  testCameraInitialization();
  testSDCardInitialization();
  testWiFiConnection();
  
  Serial.println("=== DIAGNOSTIC COMPLETE ===");
  Serial.println("Check the results above to identify hardware issues.");
  Serial.println("Common solutions:");
  Serial.println("1. Check camera module connections");
  Serial.println("2. Verify SD card is properly inserted");
  Serial.println("3. Check power supply (5V for camera, 3.3V for SD)");
  Serial.println("4. Try different SD card");
  Serial.println("5. Check for loose connections");
}

void loop() {
  // Blink LED to show system is running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 2000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
    Serial.printf("System running... Free heap: %d bytes\n", ESP.getFreeHeap());
  }
  delay(100);
}