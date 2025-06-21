# Video Recording with SD Card Storage Management

This enhanced video recording implementation includes automatic storage management with circular buffer functionality to prevent SD card overflow.

## Features

### 🔄 Circular Buffer Management
- Automatically deletes oldest videos when storage space runs low
- Configurable maximum storage allocation for videos
- Maintains minimum free space on SD card
- Preserves at least one video file at all times

### 📊 Storage Monitoring
- Real-time storage usage reporting
- Automatic cleanup based on configurable thresholds
- Detailed logging of storage operations
- File size reporting for each recorded video

### ⚙️ Configuration Options
- `MAX_STORAGE_MB`: Maximum storage to reserve for videos (default: 512MB)
- `MIN_FREE_SPACE_MB`: Minimum free space to maintain (default: 50MB)
- `ENABLE_CIRCULAR_BUFFER`: Enable/disable automatic cleanup (default: true)

## How It Works

### Storage Management Algorithm

1. **Before Recording**: Check available storage space
2. **Cleanup Triggers**: 
   - Free space falls below `MIN_FREE_SPACE_MB`
   - Video storage exceeds `MAX_STORAGE_MB`
3. **Cleanup Process**:
   - Identify oldest video file by timestamp
   - Delete oldest video and report file size
   - Repeat until storage constraints are met
   - Always preserve at least one video file

### Circular Buffer Implementation

```cpp
bool checkAndManageStorage() {
  // Check current storage usage
  uint64_t freeSpaceMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  uint64_t videoStorageMB = getVideoStorageUsed() / (1024 * 1024);
  
  // Determine if cleanup is needed
  bool needCleanup = (freeSpaceMB < MIN_FREE_SPACE_MB) || 
                     (videoStorageMB > MAX_STORAGE_MB);
  
  // Delete oldest videos until constraints are met
  while (needCleanup && countVideoFiles() > 1) {
    deleteOldestVideo();
    // Recalculate and check if more cleanup needed
  }
}
```

## Configuration Guide

### Recommended Settings by SD Card Size

| SD Card Size | MAX_STORAGE_MB | MIN_FREE_SPACE_MB | Notes |
|--------------|----------------|-------------------|-------|
| 1GB          | 400            | 100               | Basic usage |
| 2GB          | 1500           | 200               | Moderate usage |
| 4GB          | 3000           | 500               | Extended usage |
| 8GB+         | 4000+          | 1000              | Long-term storage |

### Custom Configuration

```cpp
// Storage management configuration
const long MAX_STORAGE_MB = 512;  // Adjust based on your SD card size
const long MIN_FREE_SPACE_MB = 50; // Minimum free space to maintain
const bool ENABLE_CIRCULAR_BUFFER = true; // Enable automatic cleanup
```

## Usage Examples

### Basic Setup
```cpp
// Default configuration works for most 1GB+ SD cards
// MAX_STORAGE_MB = 512MB
// MIN_FREE_SPACE_MB = 50MB
// ENABLE_CIRCULAR_BUFFER = true
```

### High-Capacity Setup
```cpp
// For 4GB+ SD cards with extended recording
const long MAX_STORAGE_MB = 3000;  // 3GB for videos
const long MIN_FREE_SPACE_MB = 500; // 500MB free space buffer
```

### Disable Circular Buffer
```cpp
// Disable automatic cleanup (will stop recording when full)
const bool ENABLE_CIRCULAR_BUFFER = false;
```

## Serial Monitor Output

The system provides detailed logging of storage operations:

```
=== Storage Management Configuration ===
Circular Buffer: ENABLED
SD Card Size: 1024MB
Total Space: 1024MB
Used Space: 234MB
Free Space: 790MB
Reserved for Videos: 512MB
Min Free Space: 50MB
Current video files: 3
==========================================

Current free space: 45MB
Video storage used: 467MB
Free space below minimum threshold!
Deleted oldest video: /video0.avi (15.32MB)
After cleanup - Free space: 60MB, Video storage: 452MB
```

## Error Handling

### Insufficient Storage
- If cleanup cannot free enough space, recording is skipped
- Error message logged to serial monitor
- System continues monitoring for next recording cycle

### File System Errors
- Failed deletions are logged and cleanup stops
- System maintains at least one video file
- Graceful handling of SD card errors

## Performance Considerations

### Storage Operations
- File enumeration: ~50ms for 100 files
- File deletion: ~10-100ms per file
- Storage calculation: ~20ms

### Memory Usage
- Minimal additional RAM usage
- File operations use stack memory
- No persistent buffers for storage management

## Troubleshooting

### Common Issues

1. **"Insufficient storage space available!"**
   - Check SD card capacity
   - Verify `MAX_STORAGE_MB` setting
   - Ensure `MIN_FREE_SPACE_MB` is reasonable

2. **"Failed to delete oldest video"**
   - SD card may be write-protected
   - File system corruption
   - Check SD card health

3. **Frequent deletions**
   - Reduce `MAX_STORAGE_MB`
   - Increase `MIN_FREE_SPACE_MB`
   - Check video file sizes

### Debug Steps

1. Monitor serial output for storage statistics
2. Verify SD card mounting and type detection
3. Check file permissions and SD card health
4. Adjust configuration parameters as needed

## Technical Details

### File Management
- Files identified by "video*.avi" pattern
- Timestamp-based aging using `file.getLastWrite()`
- Atomic operations for file deletion

### Storage Calculations
- Uses `SD.totalBytes()` and `SD.usedBytes()`
- Converts to MB for user-friendly reporting
- Accounts for file system overhead

### Safety Features
- Always preserves at least one video file
- Prevents infinite deletion loops
- Graceful error handling and recovery

## Future Enhancements

- [ ] Web interface for configuration
- [ ] Storage usage graphs
- [ ] Configurable file retention policies
- [ ] Compression options for older videos
- [ ] Cloud storage integration
