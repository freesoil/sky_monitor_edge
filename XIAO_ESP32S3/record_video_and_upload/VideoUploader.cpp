#include "VideoUploader.h"

VideoUploader::VideoUploader(const String& uploadURL, const String& apiKey, 
                           long chunkSize, long timeoutMs, int maxRetries, 
                           bool enableHTTPS, bool deleteAfterUpload) {
    this->uploadURL = uploadURL;
    this->apiKey = apiKey;
    this->chunkSize = chunkSize;
    this->timeoutMs = timeoutMs;
    this->maxRetries = maxRetries;
    this->enableHTTPS = enableHTTPS;
    this->deleteAfterUpload = deleteAfterUpload;
    
    // Initialize state variables
    this->isUploading = false;
    this->uploadPaused = false;
    this->uploadInProgress = false;
    this->currentUploadFile = "";
    this->uploadProgress = 0;
    this->uploadFileSize = 0;
    this->lastUploadAttempt = 0;
}

void VideoUploader::addToUploadQueue(const String& filename) {
    // Add to upload queue if not already present
    for (const String& queued : uploadQueue) {
        if (queued == filename) {
            return; // Already in queue
        }
    }
    uploadQueue.push_back(filename);
    Serial.printf("Added to upload queue: %s (Queue size: %d)\n", filename.c_str(), uploadQueue.size());
}

void VideoUploader::populateUploadQueue() {
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

void VideoUploader::clearUploadQueue() {
    uploadQueue.clear();
    Serial.println("Upload queue cleared");
}

bool VideoUploader::shouldPauseUpload(unsigned long lastCaptureTime, unsigned long captureInterval) {
    // Check if we should pause upload for recording
    unsigned long now = millis();
    unsigned long timeUntilNextRecording = captureInterval - (now - lastCaptureTime);
    
    bool shouldPause = (timeUntilNextRecording <= 5000);
    
    Serial.printf("shouldPauseUpload() - Time until next recording: %lu ms, Should pause: %s\n",
                  timeUntilNextRecording, shouldPause ? "YES" : "NO");
    
    // Pause if next recording is within 5 seconds
    return shouldPause;
}

void VideoUploader::pauseUpload() {
    if (isUploading && !uploadPaused) {
        uploadPaused = true;
        Serial.println("Upload paused for recording priority");
    }
}

void VideoUploader::resumeUpload() {
    if (uploadPaused) {
        uploadPaused = false;
        Serial.println("Upload resumed");
    }
}

void VideoUploader::forceResumeUploads(unsigned long lastCaptureTime, unsigned long captureDuration, unsigned long captureInterval) {
    // Check if we're currently recording (within the capture duration)
    unsigned long now = millis();
    bool isRecording = (now - lastCaptureTime) < captureDuration && (now - lastCaptureTime) < captureInterval;
    
    // Force resume uploads after recording is complete
    if (uploadPaused && !isRecording) {
        resumeUpload();
        Serial.println("Automatically resumed uploads after recording completed");
    }
}

void VideoUploader::resetStuckUploadState() {
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
        
        // If upload is paused but WiFi is connected, check if we should resume
        if (uploadPaused && WiFi.status() == WL_CONNECTED) {
            Serial.println("RESET: Upload paused but WiFi connected, checking conditions");
        }
        
        Serial.printf("RESET CHECK: uploadInProgress=%s, uploadPaused=%s, isUploading=%s\n",
                      uploadInProgress ? "YES" : "NO",
                      uploadPaused ? "YES" : "NO", 
                      isUploading ? "YES" : "NO");
    }
}

bool VideoUploader::uploadFileInChunks(String filename) {
    if (WiFi.status() != WL_CONNECTED || uploadPaused) {
        return false;
    }
    
    File file = SD.open(filename.c_str(), FILE_READ);
    if (!file) {
        Serial.printf("Failed to open file for upload: %s\n", filename.c_str());
        return false;
    }
    
    uploadFileSize = file.size();
    Serial.printf("Starting upload: %s (%.2fMB)\n", filename.c_str(), uploadFileSize / (1024.0 * 1024.0));
    
    // Parse URL to extract host, port, and path
    String url = uploadURL;
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
    
    // Setup client
    WiFiClient client;
    WiFiClientSecure secureClient;
    WiFiClient* stream;
    
    // Connect to server
    bool connected = false;
    if (enableHTTPS) {
        secureClient.setInsecure(); // For testing - use proper certificates in production
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
    
    // Send HTTP headers manually
    stream->printf("POST %s HTTP/1.1\r\n", path.c_str());
    stream->printf("Host: %s:%d\r\n", host.c_str(), port);
    stream->printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    stream->printf("Content-Length: %d\r\n", total_length);
    if (apiKey.length() > 0) {
        stream->printf("Authorization: Bearer %s\r\n", apiKey.c_str());
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
    
    while (stream->connected() && millis() - timeout < timeoutMs) {
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
    if (enableHTTPS) {
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
        
        if (deleteAfterUpload) {
            if (SD.remove(filename.c_str())) {
                Serial.printf("Deleted uploaded file: %s\n", filename.c_str());
            } else {
                Serial.printf("Failed to delete uploaded file: %s\n", filename.c_str());
            }
        }
    } else {
        Serial.printf("Upload failed: %s (Response: %d)\n", filename.c_str(), httpResponseCode);
    }
    
    return success;
}

void VideoUploader::processUploadQueue() {
    if (WiFi.status() != WL_CONNECTED || uploadQueue.empty() || uploadInProgress) {
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
    
    while (!success && retries < maxRetries && !uploadPaused) {
        if (retries > 0) {
            Serial.printf("Retry attempt %d for %s\n", retries, filename.c_str());
            delay(2000 * retries); // Exponential backoff
        }
        
        success = uploadFileInChunks(filename);
        
        if (!success) {
            retries++;
            // Check WiFi connection
            if (WiFi.status() != WL_CONNECTED) {
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

void VideoUploader::printUploadStatus() {
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