/*
* Video recording with SD card space management and circular buffer
* Automatically removes oldest videos when storage space is low
* */

#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_timer.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"

const int SD_PIN_CS = 21;

// Storage management configuration
const long MAX_STORAGE_MB = 512;  // Maximum storage to reserve for videos (MB) - adjust based on your SD card size
const long MIN_FREE_SPACE_MB = 50; // Minimum free space to maintain (MB)
const bool ENABLE_CIRCULAR_BUFFER = true; // Enable automatic deletion of oldest videos when space is low

File videoFile;
bool camera_sign = false;
bool sd_sign = false;
unsigned long lastCaptureTime = 0;
unsigned long captureDuration = 10000; // 10 seconds
unsigned long captureInterval = 20000; // 20 seconds
int imageCount = 0;

// Storage management functions
void printStorageInfo() {
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t cardUsed = (SD.cardSize() - SD.cardSize()) / (1024 * 1024);  // Note: SD.cardSize() doesn't have usedBytes()
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  uint64_t freeBytes = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total Space: %lluMB\n", totalBytes);
  Serial.printf("Used Space: %lluMB\n", usedBytes);
  Serial.printf("Free Space: %lluMB\n", freeBytes);
  Serial.printf("Reserved for Videos: %ldMB\n", MAX_STORAGE_MB);
  Serial.printf("Min Free Space: %ldMB\n", MIN_FREE_SPACE_MB);
}

String getOldestVideoFile() {
  File root = SD.open("/");
  File file = root.openNextFile();
  String oldestFile = "";
  time_t oldestTime = 0;
  bool foundFirst = false;
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("video") && fileName.endsWith(".avi")) {
      time_t fileTime = file.getLastWrite();
      if (!foundFirst || fileTime < oldestTime) {
        oldestTime = fileTime;
        oldestFile = "/" + fileName;
        foundFirst = true;
      }
    }
    file = root.openNextFile();
  }
  root.close();
  return oldestFile;
}

int countVideoFiles() {
  File root = SD.open("/");
  File file = root.openNextFile();
  int count = 0;
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("video") && fileName.endsWith(".avi")) {
      count++;
    }
    file = root.openNextFile();
  }
  root.close();
  return count;
}

uint64_t getVideoStorageUsed() {
  File root = SD.open("/");
  File file = root.openNextFile();
  uint64_t totalSize = 0;
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("video") && fileName.endsWith(".avi")) {
      totalSize += file.size();
    }
    file = root.openNextFile();
  }
  root.close();
  return totalSize;
}

bool checkAndManageStorage() {
  if (!ENABLE_CIRCULAR_BUFFER) {
    return true; // Skip storage management if disabled
  }
  
  uint64_t freeSpaceMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  uint64_t videoStorageMB = getVideoStorageUsed() / (1024 * 1024);
  
  Serial.printf("Current free space: %lluMB\n", freeSpaceMB);
  Serial.printf("Video storage used: %lluMB\n", videoStorageMB);
  
  // Check if we need to free up space
  bool needCleanup = false;
  
  // Check 1: Free space is below minimum
  if (freeSpaceMB < MIN_FREE_SPACE_MB) {
    Serial.println("Free space below minimum threshold!");
    needCleanup = true;
  }
  
  // Check 2: Video storage exceeds maximum allocation
  if (videoStorageMB > MAX_STORAGE_MB) {
    Serial.println("Video storage exceeds maximum allocation!");
    needCleanup = true;
  }
  
  // Perform cleanup if needed
  while (needCleanup && countVideoFiles() > 1) { // Keep at least 1 video file
    String oldestFile = getOldestVideoFile();
    if (oldestFile.length() == 0) {
      Serial.println("No video files found to delete!");
      break;
    }
    
    // Get file size before deletion for reporting
    File file = SD.open(oldestFile.c_str());
    size_t fileSize = 0;
    if (file) {
      fileSize = file.size();
      file.close();
    }
    
    // Delete the oldest video file
    if (SD.remove(oldestFile.c_str())) {
      Serial.printf("Deleted oldest video: %s (%.2fMB)\n", oldestFile.c_str(), fileSize / (1024.0 * 1024.0));
    } else {
      Serial.printf("Failed to delete: %s\n", oldestFile.c_str());
      break; // Exit if we can't delete files
    }
    
    // Recalculate storage usage
    freeSpaceMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
    videoStorageMB = getVideoStorageUsed() / (1024 * 1024);
    
    // Check if cleanup is still needed
    needCleanup = (freeSpaceMB < MIN_FREE_SPACE_MB) || (videoStorageMB > MAX_STORAGE_MB);
  }
  
  Serial.printf("After cleanup - Free space: %lluMB, Video storage: %lluMB\n", 
                freeSpaceMB, videoStorageMB);
  
  // Return false if we still don't have enough space
  return (freeSpaceMB >= MIN_FREE_SPACE_MB);
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
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

  // Print storage configuration and current status
  Serial.println("\n=== Storage Management Configuration ===");
  Serial.printf("Circular Buffer: %s\n", ENABLE_CIRCULAR_BUFFER ? "ENABLED" : "DISABLED");
  printStorageInfo();
  Serial.printf("Current video files: %d\n", countVideoFiles());
  Serial.println("==========================================\n");

  Serial.println("Video will begin in one minute, please be ready.");
}

void loop() {
  // Camera & SD available, start taking video
  if (camera_sign && sd_sign) {
    // Get the current time
    unsigned long now = millis();

    //If it has been more than 1 minute since the last video capture, start capturing a new video
    if ((now - lastCaptureTime) >= captureInterval) {
      
      // Check storage and perform cleanup if necessary
      if (!checkAndManageStorage()) {
        Serial.println("Insufficient storage space available! Skipping recording.");
        lastCaptureTime = now; // Update time to prevent immediate retry
        return;
      }
      
      char filename[32];
      sprintf(filename, "/video%d.avi", imageCount);
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
      
      imageCount++;

      Serial.println("Video will begin in one minute, please be ready.");

      // Wait for the remaining time of the minute
      delay(captureInterval - (millis() - lastCaptureTime));
    }
  }
}
