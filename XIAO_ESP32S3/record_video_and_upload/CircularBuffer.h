#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include "FS.h"
#include "SD.h"
#include <vector>

class CircularBuffer {
private:
    long maxStorageMB;
    long minFreeSpaceMB;
    bool enableCircularBuffer;
    
public:
    // Constructor
    CircularBuffer(long maxStorageMB = 24, long minFreeSpaceMB = 1, bool enableCircularBuffer = true);
    
    // Storage information methods
    void printStorageInfo();
    uint64_t getVideoStorageUsed();
    int countVideoFiles();
    String getOldestVideoFile();
    
    // Storage management methods
    bool checkAndManageStorage();
    bool checkAndManageStorage(std::vector<String>& uploadQueue); // Version that can modify upload queue
    
    // Configuration methods
    void setMaxStorageMB(long maxMB) { maxStorageMB = maxMB; }
    void setMinFreeSpaceMB(long minMB) { minFreeSpaceMB = minMB; }
    void setCircularBufferEnabled(bool enabled) { enableCircularBuffer = enabled; }
    
    // Getters
    long getMaxStorageMB() const { return maxStorageMB; }
    long getMinFreeSpaceMB() const { return minFreeSpaceMB; }
    bool isCircularBufferEnabled() const { return enableCircularBuffer; }
};

#endif // CIRCULARBUFFER_H 