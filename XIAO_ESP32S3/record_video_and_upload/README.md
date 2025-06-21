# Video Recording with Background Upload and Storage Management

This implementation combines video recording, SD card storage management, and intelligent background video uploading. The system automatically uploads recorded videos to a web server while prioritizing recording operations.

## Features

### 🔄 Circular Buffer Management
- Automatically deletes oldest videos when storage space runs low
- Configurable maximum storage allocation for videos
- Maintains minimum free space on SD card
- Preserves at least one video file at all times

### 📡 Background Upload System
- **Intelligent Pause/Resume**: Automatically pauses uploads when recording is about to start
- **Priority Management**: Recording always takes priority over uploading
- **Chunked Uploads**: Handles large video files efficiently
- **Retry Logic**: Automatic retry with exponential backoff for failed uploads
- **Queue Management**: Maintains upload queue and processes files sequentially

### 🛡️ Robust Error Handling
- WiFi connection monitoring and auto-reconnection
- Upload timeout and retry mechanisms
- Graceful handling of network interruptions
- Comprehensive logging and status reporting

## 🔋 New Features

### WiFi Status LED Indicator
- **Smart LED Patterns**: Built-in LED shows different patterns every 5 seconds based on system status
- **Visual Status** (only when NOT recording): 
  - **2 Quick Flashes**: WiFi connected and actively uploading
  - **1 Flash**: WiFi connected but idle (no uploads)
  - **OFF**: WiFi disconnected or no activity
  - **Solid ON**: Currently recording video (overrides all other patterns)
- **Smart Control**: Automatically adjusts based on recording, WiFi, and upload status

### Enhanced Upload Management
- **Recording Priority**: Uploads are automatically paused during video recording
- **Smart Resume**: Uploads resume immediately after recording completes
- **Background Processing**: Upload queue processes continuously when not recording
- **No Interference**: Video recording quality is never affected by uploads

### Timestamp-based File Naming
- **NTP Synchronization**: Automatically syncs time with internet time servers
- **Timestamp Filenames**: Videos named as `YYYYmmddHHmmss.avi` (e.g., `20241220143052.avi`)
- **Timezone Support**: Configurable GMT offset and daylight saving time
- **Fallback System**: Uses numbered naming if NTP sync fails
- **Chronological Organization**: Files automatically sorted by recording time

## 🐛 Bug Fix: Multipart Upload Format

**Fixed Issue**: The original upload implementation was sending raw binary data, causing HTTP 422 errors with servers expecting multipart/form-data uploads.

**Error**: `{"detail":[{"type":"missing","loc":["body","file"],"msg":"Field required","input":null}]}`

**Solution**: The upload function now properly formats requests as multipart/form-data:
- ✅ Generates unique boundary for each upload
- ✅ Includes proper `Content-Disposition: form-data; name="file"` headers  
- ✅ Maintains filename in the upload request
- ✅ Compatible with standard web upload endpoints (FastAPI, Flask, etc.)

## Configuration

### WiFi and Upload Settings

```cpp
// WiFi Configuration
const char* WIFI_SSID = "YourWiFiSSID";        
const char* WIFI_PASSWORD = "YourWiFiPassword"; 

// Upload Configuration
const char* UPLOAD_URL = "https://your-server.com/upload";
const char* UPLOAD_API_KEY = "your-api-key";    // Optional

// NTP Time Configuration
const char* NTP_SERVER = "pool.ntp.org";       // NTP server for time sync
const long GMT_OFFSET_SEC = 0;                  // GMT offset in seconds (e.g., -8*3600 for PST)
const int DAYLIGHT_OFFSET_SEC = 0;              // Daylight saving offset in seconds (e.g., 3600 for DST)

// Upload Settings
const long UPLOAD_CHUNK_SIZE = 8192;    // Upload chunk size (8KB)
const long UPLOAD_TIMEOUT_MS = 30000;   // Upload timeout (30 seconds)
const int MAX_UPLOAD_RETRIES = 3;       // Maximum retry attempts
const bool ENABLE_HTTPS = false;        // Set to true for HTTPS
const bool DELETE_AFTER_UPLOAD = true;  // Delete after successful upload
```

### Timezone Examples
```cpp
// Pacific Standard Time (PST/PDT)
const long GMT_OFFSET_SEC = -8 * 3600;   
const int DAYLIGHT_OFFSET_SEC = 3600;    // Add 1 hour for PDT in summer

// Eastern Standard Time (EST/EDT)
const long GMT_OFFSET_SEC = -5 * 3600;   
const int DAYLIGHT_OFFSET_SEC = 3600;    

// Central European Time (CET/CEST)
const long GMT_OFFSET_SEC = 1 * 3600;    
const int DAYLIGHT_OFFSET_SEC = 3600;    

// Japan Standard Time (JST) - No DST
const long GMT_OFFSET_SEC = 9 * 3600;    
const int DAYLIGHT_OFFSET_SEC = 0;       
```

### Storage Management

```cpp
const long MAX_STORAGE_MB = 512;         // Maximum storage for videos
const long MIN_FREE_SPACE_MB = 50;       // Minimum free space to maintain
const bool ENABLE_CIRCULAR_BUFFER = true; // Enable automatic cleanup
```

## How It Works

### Upload Priority System

1. **Recording Priority**: When recording time approaches (within 5 seconds), uploads are paused
2. **Automatic Pause**: Any ongoing upload is paused when recording starts
3. **Smart Resume**: Upload resumes after recording completes and safe period begins
4. **Queue Processing**: Videos are uploaded in chronological order (oldest first)

### Upload Flow

```
Video Created → Added to Queue → Upload When Safe → 
Retry on Failure → Delete on Success → Next in Queue
```

### Pause/Resume Logic

```cpp
bool shouldPauseUpload() {
  unsigned long timeUntilNextRecording = captureInterval - (now - lastCaptureTime);
  return (timeUntilNextRecording <= 5000); // Pause if recording within 5 seconds
}
```

## Server Requirements

### Upload Endpoint

Your server should accept POST requests with the following headers:

```
Content-Type: application/octet-stream
Content-Length: [file_size]
X-Filename: [filename]
Authorization: Bearer [api_key] (optional)
```

### Example Server Implementation (Node.js)

```javascript
const express = require('express');
const multer = require('multer');
const fs = require('fs');
const app = express();

const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    cb(null, 'uploads/');
  },
  filename: (req, file, cb) => {
    const filename = req.headers['x-filename'] || 'video.avi';
    cb(null, `${Date.now()}_${filename}`);
  }
});

const upload = multer({ storage: storage });

app.post('/upload', upload.single('file'), (req, res) => {
  console.log('Video uploaded:', req.file.filename);
  res.status(200).json({ 
    message: 'Upload successful',
    filename: req.file.filename,
    size: req.file.size 
  });
});

app.listen(3000, () => {
  console.log('Upload server running on port 3000');
});
```

### Python Flask Example

```python
from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)
UPLOAD_FOLDER = 'uploads'

@app.route('/upload', methods=['POST'])
def upload_video():
    try:
        filename = request.headers.get('X-Filename', 'video.avi')
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        safe_filename = f"{timestamp}_{filename}"
        
        file_path = os.path.join(UPLOAD_FOLDER, safe_filename)
        
        with open(file_path, 'wb') as f:
            f.write(request.data)
        
        file_size = os.path.getsize(file_path)
        
        return jsonify({
            'message': 'Upload successful',
            'filename': safe_filename,
            'size': file_size
        }), 200
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    os.makedirs(UPLOAD_FOLDER, exist_ok=True)
    app.run(host='0.0.0.0', port=3000)
```

## Serial Monitor Output

The system provides comprehensive logging:

```
=== Upload Configuration ===
WiFi Status: CONNECTED
Upload URL: https://your-server.com/upload
Delete after upload: YES
Upload chunk size: 8KB
Max retries: 3
=====================================

WiFi connected! IP: 192.168.1.100
Signal strength: -45 dBm
Found 3 videos to upload

Recording video: /video0.avi
Video with 150 frames saved: /video0.avi
File size: 2.34MB
Added to upload queue: /video0.avi (Queue size: 4)

Starting upload: /video0.avi (2.34MB)
Upload successful: /video0.avi (Response: 200)
Deleted uploaded file: /video0.avi
Upload completed successfully: /video0.avi

Upload queue: 3 files pending
```

## Advanced Features

### Intelligent Timing

- **Safe Upload Windows**: Only uploads when sufficient time remains before next recording
- **Automatic Pause**: Pauses uploads 5 seconds before recording
- **Graceful Resume**: Resumes uploads after recording with safety margin

### Network Resilience

- **Connection Monitoring**: Checks WiFi status every 30 seconds
- **Auto-Reconnection**: Automatically reconnects to WiFi if connection is lost
- **Retry Logic**: Exponential backoff for failed uploads (2s, 4s, 6s delays)
- **Timeout Handling**: Configurable upload timeouts to prevent hanging

### Storage Integration

- **Queue Cleanup**: Removes files from upload queue when deleted by storage management
- **Priority Ordering**: Uploads oldest videos first to optimize storage efficiency
- **Status Tracking**: Real-time monitoring of upload progress and queue status

## Performance Optimization

### Upload Settings by Network Speed

| Network Type | Chunk Size | Timeout | Retries |
|--------------|------------|---------|---------|
| WiFi (Fast)  | 16384 (16KB) | 15000ms | 2 |
| WiFi (Normal)| 8192 (8KB)   | 30000ms | 3 |
| WiFi (Slow)  | 4096 (4KB)   | 60000ms | 5 |

### Memory Usage

- **Minimal RAM**: Uses streaming uploads to minimize memory footprint
- **Efficient Queuing**: Vector-based queue for optimal performance
- **No Buffering**: Direct SD-to-network transfer without intermediate buffers

## Troubleshooting

### Common Issues

1. **Upload Fails with Timeout**
   ```
   Solution: Increase UPLOAD_TIMEOUT_MS or reduce UPLOAD_CHUNK_SIZE
   ```

2. **WiFi Connection Lost**
   ```
   Automatic reconnection will be attempted
   Check WiFi signal strength and credentials
   ```

3. **Server Rejects Upload**
   ```
   Verify server endpoint URL and API key
   Check server logs for detailed error messages
   ```

4. **Upload Queue Growing**
   ```
   Check network speed and server capacity
   Consider reducing video quality or duration
   ```

### Debug Mode

Enable detailed logging by monitoring serial output:

```cpp
// Add this for more verbose logging
#define DEBUG_UPLOAD 1
```

### Network Diagnostics

```cpp
void printNetworkDiagnostics() {
  Serial.printf("WiFi Status: %d\n", WiFi.status());
  Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
}
```

## Security Considerations

### HTTPS Configuration

```cpp
const bool ENABLE_HTTPS = true;
// For production, implement proper certificate validation:
secureClient.setCACert(your_ca_certificate);
```

### API Authentication

```cpp
const char* UPLOAD_API_KEY = "your-secure-api-key";
// Server should validate this key for authentication
```

### Network Security

- Use WPA2/WPA3 secured WiFi networks
- Consider VPN for additional security
- Implement server-side rate limiting
- Use HTTPS for encrypted transmission

## Future Enhancements

- [ ] Resume interrupted uploads from last chunk
- [ ] Compression before upload to reduce bandwidth
- [ ] Multiple server endpoints for redundancy
- [ ] Upload scheduling during specific time windows
- [ ] Real-time upload progress via web interface
- [ ] Cloud storage integration (AWS S3, Google Cloud)
- [ ] Email notifications for upload status
