#include "CircularBuffer.h"

CircularBuffer::CircularBuffer(long maxStorageMB, long minFreeSpaceMB, bool enableCircularBuffer) {
    this->maxStorageMB = maxStorageMB;
    this->minFreeSpaceMB = minFreeSpaceMB;
    this->enableCircularBuffer = enableCircularBuffer;
}

void CircularBuffer::printStorageInfo() {
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    uint64_t freeBytes = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
    
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    Serial.printf("Total Space: %lluMB\n", totalBytes);
    Serial.printf("Used Space: %lluMB\n", usedBytes);
    Serial.printf("Free Space: %lluMB\n", freeBytes);
    Serial.printf("Reserved for Videos: %ldMB\n", maxStorageMB);
    Serial.printf("Min Free Space: %ldMB\n", minFreeSpaceMB);
}

String CircularBuffer::getOldestVideoFile() {
    File root = SD.open("/");
    File file = root.openNextFile();
    String oldestFile = "";
    time_t oldestTime = 0;
    bool foundFirst = false;
    
    while (file) {
        String fileName = file.name();
        // Check for both old format (video*.avi) and new timestamp format (*.avi)
        if (fileName.endsWith(".avi")) {
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

int CircularBuffer::countVideoFiles() {
    File root = SD.open("/");
    File file = root.openNextFile();
    int count = 0;
    
    while (file) {
        String fileName = file.name();
        // Count all .avi files regardless of naming format
        if (fileName.endsWith(".avi")) {
            count++;
        }
        file = root.openNextFile();
    }
    root.close();
    return count;
}

uint64_t CircularBuffer::getVideoStorageUsed() {
    File root = SD.open("/");
    File file = root.openNextFile();
    uint64_t totalSize = 0;
    
    while (file) {
        String fileName = file.name();
        // Count all .avi files regardless of naming format
        if (fileName.endsWith(".avi")) {
            totalSize += file.size();
        }
        file = root.openNextFile();
    }
    root.close();
    return totalSize;
}

bool CircularBuffer::checkAndManageStorage() {
    std::vector<String> emptyQueue;
    return checkAndManageStorage(emptyQueue);
}

bool CircularBuffer::checkAndManageStorage(std::vector<String>& uploadQueue) {
    if (!enableCircularBuffer) {
        return true; // Skip storage management if disabled
    }
    
    uint64_t freeSpaceMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
    uint64_t videoStorageMB = getVideoStorageUsed() / (1024 * 1024);
    
    Serial.printf("Current free space: %lluMB\n", freeSpaceMB);
    Serial.printf("Video storage used: %lluMB\n", videoStorageMB);
    
    // Check if we need to free up space
    bool needCleanup = false;
    
    // Check 1: Free space is below minimum
    if (freeSpaceMB < minFreeSpaceMB) {
        Serial.println("Free space below minimum threshold!");
        needCleanup = true;
    }
    
    // Check 2: Video storage exceeds maximum allocation
    if (videoStorageMB > maxStorageMB) {
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
        needCleanup = (freeSpaceMB < minFreeSpaceMB) || (videoStorageMB > maxStorageMB);
    }
    
    Serial.printf("After cleanup - Free space: %lluMB, Video storage: %lluMB\n", 
                  freeSpaceMB, videoStorageMB);
    
    // Return false if we still don't have enough space
    return (freeSpaceMB >= minFreeSpaceMB);
} 