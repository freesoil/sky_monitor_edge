/*
* Video recording with SD card space management, circular buffer, and background upload
* Automatically removes oldest videos when storage space is low
* Uploads videos to a web server when not recording, with intelligent pause/resume
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

// WiFi and Upload Configuration
const char* WIFI_SSID = "WIFI SSID";        // Replace with your WiFi SSID
const char* WIFI_PASSWORD = "WIFI Password"; // Replace with your WiFi password
const char* UPLOAD_URL = "http://192.168.1.57:8000/upload"; // Replace with your upload endpoint
const char* UPLOAD_API_KEY = "";    // Optional API key for authentication

// NTP time configuration
const char* NTP_SERVER = "pool.ntp.org";       // NTP server for time sync
const long GMT_OFFSET_SEC = 0;                  // GMT offset in seconds (e.g., -8*3600 for PST)
const int DAYLIGHT_OFFSET_SEC = 0;              // Daylight saving offset in seconds (e.g., 3600 for DST)

// Upload settings
const long UPLOAD_CHUNK_SIZE = 8192;    // Upload chunk size in bytes (8KB)
const long UPLOAD_TIMEOUT_MS = 30000;   // Upload timeout in milliseconds
const int MAX_UPLOAD_RETRIES = 3;       // Maximum retry attempts for failed uploads
const bool ENABLE_HTTPS = false;        // Set to true if using HTTPS
const bool DELETE_AFTER_UPLOAD = true;  // Delete videos after successful upload

// Storage management configuration
const long MAX_STORAGE_MB = 24;  // Maximum storage to reserve for videos (MB) - adjust based on your SD card size
const long MIN_FREE_SPACE_MB = 1; // Minimum free space to maintain (MB)
const bool ENABLE_CIRCULAR_BUFFER = true; // Enable automatic deletion of oldest videos when space is low

File videoFile;
bool camera_sign = false;
bool sd_sign = false;
bool wifi_connected = false;
unsigned long lastCaptureTime = 0;
unsigned long captureDuration = 10000; // 10 seconds
unsigned long captureInterval = 60000; // 20 seconds
int imageCount = 0;

// Upload management variables
bool isUploading = false;
bool uploadPaused = false;
String currentUploadFile = "";
size_t uploadProgress = 0;
size_t uploadFileSize = 0;
unsigned long lastUploadAttempt = 0;
std::vector<String> uploadQueue;
bool uploadInProgress = false;

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
      if (isUploading) {
        pauseUpload();
        Serial.println("Paused uploads due to WiFi loss");
      }
    }
    connectToWiFi();
  } else if (!wifi_connected && WiFi.status() == WL_CONNECTED) {
    // WiFi reconnected
    wifi_connected = true;
    Serial.println("WiFi reconnected successfully!");
    
    // Resume uploads if they were paused due to WiFi loss
    if (uploadPaused && !isRecording()) {
      resumeUpload();
      Serial.println("Resumed uploads after WiFi reconnection");
    }
    
    // Repopulate upload queue in case we missed any files
    populateUploadQueue();
  }
}

void addToUploadQueue(String filename) {
  // Add to upload queue if not already present
  for (const String& queued : uploadQueue) {
    if (queued == filename) {
      return; // Already in queue
    }
  }
  uploadQueue.push_back(filename);
  Serial.printf("Added to upload queue: %s (Queue size: %d)\n", filename.c_str(), uploadQueue.size());
}

void populateUploadQueue() {
  // Scan SD card for video files and add them to upload queue
  File root = SD.open("/");
  File file = root.openNextFile();
  
  while (file) {
    String fileName = file.name();
    // Upload all .avi files regardless of naming format
    if (fileName.endsWith(".avi")) {
      String fullPath = "/" + fileName;
      addToUploadQueue(fullPath);
    }
    file = root.openNextFile();
  }
  root.close();
  
  if (uploadQueue.size() > 0) {
    Serial.printf("Found %d videos to upload\n", uploadQueue.size());
  }
}

bool shouldPauseUpload() {
  // Check if we should pause upload for recording
  unsigned long now = millis();
  unsigned long timeUntilNextRecording = captureInterval - (now - lastCaptureTime);
  
  bool shouldPause = (timeUntilNextRecording <= 5000);
  
  Serial.printf("shouldPauseUpload() - Time until next recording: %lu ms, Should pause: %s\n",
                timeUntilNextRecording, shouldPause ? "YES" : "NO");
  
  // Pause if next recording is within 5 seconds
  return shouldPause;
}

void pauseUpload() {
  if (isUploading && !uploadPaused) {
    uploadPaused = true;
    Serial.println("Upload paused for recording priority");
  }
}

void resumeUpload() {
  if (uploadPaused) {
    uploadPaused = false;
    Serial.println("Upload resumed");
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
                  isUploading ? "YES" : "NO", uploadQueue.size());
  }
  
  // Handle flash patterns within the STATUS_CHECK_MS window
  unsigned long timeSinceFlashStart = now - flashStartTime;
  
  if (flashStartTime > 0 && timeSinceFlashStart < 1000) { // Flash sequence window (1 second)
    
    if (isUploading) {
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

void forceResumeUploads() {
  // Force resume uploads after recording is complete
  if (uploadPaused && !isRecording()) {
    resumeUpload();
    Serial.println("Automatically resumed uploads after recording completed");
  }
}

void resetStuckUploadState() {
  // Reset upload state if it's been stuck for too long
  static unsigned long lastUploadReset = 0;
  unsigned long now = millis();
  
  // Check every 30 seconds
  if (now - lastUploadReset > 30000) {
    lastUploadReset = now;
    
    // If uploadInProgress has been true for more than 5 minutes, reset it
    if (uploadInProgress && (now - lastUploadAttempt > 300000)) {
      Serial.println("RESET: Upload appears stuck, resetting uploadInProgress");
      uploadInProgress = false;
      isUploading = false;
      currentUploadFile = "";
    }
    
    // If upload is paused but not recording and WiFi is connected, try to resume
    if (uploadPaused && !isRecording() && wifi_connected && !shouldPauseUpload()) {
      Serial.println("RESET: Upload paused but should be able to continue, resuming");
      resumeUpload();
    }
    
    Serial.printf("RESET CHECK: uploadInProgress=%s, uploadPaused=%s, isUploading=%s\n",
                  uploadInProgress ? "YES" : "NO",
                  uploadPaused ? "YES" : "NO", 
                  isUploading ? "YES" : "NO");
  }
}

bool uploadFileInChunks(String filename) {
  if (!wifi_connected || uploadPaused) {
    return false;
  }
  
  File file = SD.open(filename.c_str(), FILE_READ);
  if (!file) {
    Serial.printf("Failed to open file for upload: %s\n", filename.c_str());
    return false;
  }
  
  uploadFileSize = file.size();
  Serial.printf("Starting upload: %s (%.2fMB)\n", filename.c_str(), uploadFileSize / (1024.0 * 1024.0));
  
  HTTPClient http;
  WiFiClient client;
  WiFiClientSecure secureClient;
  
  if (ENABLE_HTTPS) {
    secureClient.setInsecure(); // For testing - use proper certificates in production
    http.begin(secureClient, UPLOAD_URL);
  } else {
    http.begin(client, UPLOAD_URL);
  }
  
  // Generate boundary for multipart/form-data
  String boundary = "----ESP32FormBoundary" + String(random(10000, 99999));
  
  // Extract filename without path
  String filename_only = filename.substring(filename.lastIndexOf('/') + 1);
  
  // Build multipart form data
  String multipart_start = "--" + boundary + "\r\n";
  multipart_start += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename_only + "\"\r\n";
  multipart_start += "Content-Type: application/octet-stream\r\n\r\n";
  
  String multipart_end = "\r\n--" + boundary + "--\r\n";
  
  // Calculate total content length
  size_t total_length = multipart_start.length() + uploadFileSize + multipart_end.length();
  
  // Set headers
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(total_length));
  if (strlen(UPLOAD_API_KEY) > 0) {
    http.addHeader("Authorization", "Bearer " + String(UPLOAD_API_KEY));
  }
  
  http.setTimeout(UPLOAD_TIMEOUT_MS);
  
  // Create payload string that combines multipart headers + file content + multipart footer
  // We need to build this as a stream since the file might be large
  
  WiFiClient* stream;
  if (ENABLE_HTTPS) {
    stream = http.getStreamPtr();
  } else {
    stream = http.getStreamPtr();
  }
  
  // Try using HTTPClient's built-in multipart support first
  String payload = multipart_start;
  // Note: We can't easily build the full payload in memory for large files
  // So let's use a different approach - build the payload as a string for small parts
  
  // For HTTPClient, we need to use a different approach
  // Let's try the manual connection method which gives us more control
  
  Serial.println("Using manual multipart upload...");
  
  // Parse URL to extract host, port, and path
  String url = UPLOAD_URL;
  String protocol = "http://";
  String host = "";
  int port = 80;
  String path = "/upload";
  
  if (url.startsWith("https://")) {
    protocol = "https://";
    port = 443;
    url = url.substring(8); // Remove "https://"
  } else if (url.startsWith("http://")) {
    url = url.substring(7); // Remove "http://"
  }
  
  // Extract host and path
  int slash_pos = url.indexOf('/');
  if (slash_pos > 0) {
    host = url.substring(0, slash_pos);
    path = url.substring(slash_pos);
  } else {
    host = url;
    path = "/upload"; // Default path
  }
  
  // Check for custom port in host
  int colon_pos = host.indexOf(':');
  if (colon_pos > 0) {
    port = host.substring(colon_pos + 1).toInt();
    host = host.substring(0, colon_pos);
  }
  
  Serial.printf("Connecting to: %s:%d%s\n", host.c_str(), port, path.c_str());
  
  // Connect to server
  bool connected = false;
  if (ENABLE_HTTPS) {
    connected = secureClient.connect(host.c_str(), port);
    stream = &secureClient;
  } else {
    connected = client.connect(host.c_str(), port);
    stream = &client;
  }
  
  if (!connected) {
    Serial.printf("Connection failed to %s:%d\n", host.c_str(), port);
    file.close();
    return false;
  }
  
  Serial.println("Connected! Sending HTTP request...");
  
  // Send HTTP headers manually
  stream->printf("POST %s HTTP/1.1\r\n", path.c_str());
  stream->printf("Host: %s:%d\r\n", host.c_str(), port);
  stream->printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  stream->printf("Content-Length: %d\r\n", total_length);
  if (strlen(UPLOAD_API_KEY) > 0) {
    stream->printf("Authorization: Bearer %s\r\n", UPLOAD_API_KEY);
  }
  stream->print("Connection: close\r\n");
  stream->print("\r\n");
  
  Serial.println("Sending multipart data...");
  
  // Send multipart start
  stream->print(multipart_start);
  
  // Send file content in chunks
  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];
  size_t remaining = uploadFileSize;
  size_t totalSent = 0;
  
  while (remaining > 0 && !uploadPaused && stream->connected()) {
    size_t toRead = min(remaining, bufferSize);
    size_t bytesRead = file.read(buffer, toRead);
    
    if (bytesRead > 0) {
      size_t written = stream->write(buffer, bytesRead);
      if (written != bytesRead) {
        Serial.printf("Write error: expected %d, wrote %d\n", bytesRead, written);
        break;
      }
      remaining -= bytesRead;
      totalSent += bytesRead;
      
      // Progress indicator for large files
      if (totalSent % (100 * 1024) == 0) { // Every 100KB
        Serial.printf("Uploaded: %.1f%%\n", (float)totalSent / uploadFileSize * 100);
      }
    } else {
      Serial.println("Error reading file");
      break;
    }
    yield(); // Allow other tasks to run
  }
  
  // Send multipart end
  stream->print(multipart_end);
  stream->flush(); // Ensure all data is sent
  
  Serial.printf("Upload data sent: %d bytes\n", totalSent + multipart_start.length() + multipart_end.length());
  
  // Read response
  String response = "";
  unsigned long timeout = millis();
  bool headersParsed = false;
  
  while (stream->connected() && millis() - timeout < UPLOAD_TIMEOUT_MS) {
    if (stream->available()) {
      String line = stream->readStringUntil('\n');
      line.trim();
      response += line + "\n";
      
      if (!headersParsed) {
        if (line.length() == 0) { // Empty line indicates end of headers
          headersParsed = true;
          Serial.println("Headers received, reading body...");
        }
      } else {
        // We're in the body section
        while (stream->available()) {
          response += stream->readString();
        }
        break;
      }
    }
    delay(1);
  }
  
  // Close connection
  if (ENABLE_HTTPS) {
    secureClient.stop();
  } else {
    client.stop();
  }
  
  // Parse response code
  int httpResponseCode = 0;
  if (response.startsWith("HTTP/1.1 ") || response.startsWith("HTTP/1.0 ")) {
    httpResponseCode = response.substring(9, 12).toInt();
  }
  
  // Extract and print response body
  int body_start = response.indexOf("\r\n\r\n");
  if (body_start < 0) {
    body_start = response.indexOf("\n\n");
    if (body_start >= 0) body_start += 2;
  } else {
    body_start += 4;
  }
  
  if (body_start > 0) {
    String responseBody = response.substring(body_start);
    responseBody.trim();
    if (responseBody.length() > 0) {
      Serial.printf("Server response: %s\n", responseBody.c_str());
    }
  }
  
  Serial.printf("Full response:\n%s\n", response.c_str());
  
  file.close();
  
  bool success = (httpResponseCode == 200 || httpResponseCode == 201);
  
  if (success) {
    Serial.printf("Upload successful: %s (Response: %d)\n", filename.c_str(), httpResponseCode);
    
    if (DELETE_AFTER_UPLOAD) {
      if (SD.remove(filename.c_str())) {
        Serial.printf("Deleted uploaded file: %s\n", filename.c_str());
      } else {
        Serial.printf("Failed to delete uploaded file: %s\n", filename.c_str());
      }
    }
  } else {
    Serial.printf("Upload failed: %s (Response: %d)\n", filename.c_str(), httpResponseCode);
  }
  
  http.end();
  return success;
}

void processUploadQueue() {
  if (!wifi_connected || uploadQueue.empty() || uploadInProgress) {
    return;
  }
  
  // Check if we should pause for upcoming recording
  if (shouldPauseUpload()) {
    Serial.println("processUploadQueue() - Pausing for upcoming recording");
    pauseUpload();
    return;
  }
  
  // Resume if paused and safe to continue
  if (uploadPaused) {
    Serial.println("processUploadQueue() - Resuming from paused state");
    resumeUpload();
  }
  
  if (uploadPaused) {
    Serial.println("processUploadQueue() - Still paused, exiting");
    return; // Still paused
  }
  
  // Throttle upload attempts
  unsigned long now = millis();
  Serial.printf("processUploadQueue() - Time since last attempt: %lu ms (need 5000ms)\n", 
                now - lastUploadAttempt);
  if (now - lastUploadAttempt < 5000) { // Wait 5 seconds between attempts
    Serial.println("processUploadQueue() - Throttling uploads, too soon");
    return;
  }
  
  Serial.println("processUploadQueue() - Starting upload process");
  
  uploadInProgress = true;
  isUploading = true;
  lastUploadAttempt = now;
  
  String filename = uploadQueue[0];
  currentUploadFile = filename;
  uploadProgress = 0;
  
  Serial.printf("Processing upload: %s\n", filename.c_str());
  
  bool success = false;
  int retries = 0;
  
  while (!success && retries < MAX_UPLOAD_RETRIES && !uploadPaused) {
    if (retries > 0) {
      Serial.printf("Retry attempt %d for %s\n", retries, filename.c_str());
      delay(2000 * retries); // Exponential backoff
    }
    
    success = uploadFileInChunks(filename);
    
    if (!success) {
      retries++;
      // Check WiFi connection
      checkWiFiConnection();
      if (!wifi_connected) {
        break; // Exit retry loop if WiFi is down
      }
    }
  }
  
  // Remove from queue regardless of success/failure after max retries
  uploadQueue.erase(uploadQueue.begin());
  
  if (success) {
    Serial.printf("Upload completed successfully: %s\n", filename.c_str());
  } else {
    Serial.printf("Upload failed after %d attempts: %s\n", retries, filename.c_str());
  }
  
  currentUploadFile = "";
  isUploading = false;
  uploadInProgress = false;
}

void printUploadStatus() {
  if (uploadQueue.empty()) {
    Serial.println("Upload queue: Empty");
  } else {
    Serial.printf("Upload queue: %d files pending\n", uploadQueue.size());
    if (isUploading) {
      Serial.printf("Currently uploading: %s\n", currentUploadFile.c_str());
      if (uploadPaused) {
        Serial.println("Upload status: PAUSED for recording");
      }
    }
  }
}

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
    
    // Remove from upload queue if present
    for (auto it = uploadQueue.begin(); it != uploadQueue.end(); ++it) {
      if (*it == oldestFile) {
        uploadQueue.erase(it);
        Serial.printf("Removed from upload queue: %s\n", oldestFile.c_str());
        break;
      }
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

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off
  Serial.println("LED initialized");

  // Connect to WiFi
  connectToWiFi();

  // Print storage configuration and current status
  Serial.println("\n=== Storage Management Configuration ===");
  Serial.printf("Circular Buffer: %s\n", ENABLE_CIRCULAR_BUFFER ? "ENABLED" : "DISABLED");
  printStorageInfo();
  Serial.printf("Current video files: %d\n", countVideoFiles());
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
    populateUploadQueue();
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
  forceResumeUploads();
  
  // Reset stuck upload states periodically
  resetStuckUploadState();
  
  // Debug output every 10 seconds
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 10000) {
    Serial.printf("DEBUG - WiFi: %s, Recording: %s, Uploading: %s, Queue: %d\n", 
                  wifi_connected ? "ON" : "OFF",
                  isRecording() ? "YES" : "NO",
                  isUploading ? "YES" : "NO",
                  uploadQueue.size());
    lastDebugPrint = millis();
  }
  
  // Camera & SD available, start taking video
  if (camera_sign && sd_sign) {
    // Get the current time
    unsigned long now = millis();

    //If it has been more than 1 minute since the last video capture, start capturing a new video
    if ((now - lastCaptureTime) >= captureInterval) {
      
      // Pause any ongoing upload
      if (isUploading) {
        pauseUpload();
        Serial.println("Pausing upload for recording");
      }
      
      // Check storage and perform cleanup if necessary
      if (!checkAndManageStorage()) {
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
        addToUploadQueue(String(filename));
      }
      
      imageCount++;

      // Resume uploads now that recording is complete
      if (uploadPaused) {
        resumeUpload();
      }

      Serial.printf("Video will begin in %d seconds, please be ready.\n", captureInterval/1000);

      // Note: No blocking delay here - let the main loop continue for uploads and LED updates
    }
    
    // Process upload queue when not recording and WiFi is connected
    if (wifi_connected && !isRecording() && !uploadInProgress) {
      processUploadQueue();
    }
    
    // Print status every 30 seconds
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 30000) {
      printUploadStatus();
      lastStatusPrint = millis();
    }
  }
  
  // Small delay to prevent excessive CPU usage
  delay(100);
}
