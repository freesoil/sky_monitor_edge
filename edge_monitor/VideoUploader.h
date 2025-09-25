#ifndef VIDEOUPLOADER_H
#define VIDEOUPLOADER_H

#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "FS.h"
#include "SD.h"
#include <vector>

class VideoUploader {
private:
    // Configuration
    String uploadURL;
    String apiKey;
    long chunkSize;
    long timeoutMs;
    int maxRetries;
    bool enableHTTPS;
    bool deleteAfterUpload;
    
    // State variables
    std::vector<String> uploadQueue;
    bool isUploading;
    bool uploadPaused;
    bool uploadInProgress;
    String currentUploadFile;
    size_t uploadProgress;
    size_t uploadFileSize;
    unsigned long lastUploadAttempt;
    
    // Internal methods
    bool uploadFileInChunks(String filename);
    
public:
    // Constructor
    VideoUploader(const String& uploadURL, const String& apiKey = "", 
                  long chunkSize = 8192, long timeoutMs = 30000, 
                  int maxRetries = 3, bool enableHTTPS = false, 
                  bool deleteAfterUpload = true);
    
    // Queue management
    void addToUploadQueue(const String& filename);
    void populateUploadQueue();
    void clearUploadQueue();
    
    // Upload control
    void processUploadQueue();
    bool shouldPauseUpload(unsigned long lastCaptureTime, unsigned long captureInterval);
    void pauseUpload();
    void resumeUpload();
    void forceResumeUploads(unsigned long lastCaptureTime, unsigned long captureDuration, unsigned long captureInterval);
    void resetStuckUploadState();
    
    // Status and information
    void printUploadStatus();
    bool getIsUploading() const { return isUploading; }
    bool getUploadPaused() const { return uploadPaused; }
    int getQueueSize() const { return uploadQueue.size(); }
    String getCurrentUploadFile() const { return currentUploadFile; }
    
    // Configuration setters
    void setUploadURL(const String& url) { uploadURL = url; }
    void setApiKey(const String& key) { apiKey = key; }
    void setChunkSize(long size) { chunkSize = size; }
    void setTimeoutMs(long timeout) { timeoutMs = timeout; }
    void setMaxRetries(int retries) { maxRetries = retries; }
    void setEnableHTTPS(bool enable) { enableHTTPS = enable; }
    void setDeleteAfterUpload(bool enable) { deleteAfterUpload = enable; }
    
    // Allow external access to upload queue for storage management
    std::vector<String>& getUploadQueue() { return uploadQueue; }
};

#endif // VIDEOUPLOADER_H 