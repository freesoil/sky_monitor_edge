# ESP32 Edge Monitor - Complete Documentation

A production-ready ESP32-based camera monitoring system with web API, real-time streaming, background uploads, and comprehensive device management.

## 🚀 Quick Start

### Hardware Requirements
- **ESP32-CAM** or **XIAO ESP32S3** (with PSRAM)
- **SD Card** (Class 10, 8GB+ recommended)
- **5V 2A Power Supply** (minimum for high resolution)
- **Camera Module** (OV2640 sensor)

### Software Setup
1. **Install Arduino IDE** with ESP32 board support
2. **Enable PSRAM**: Tools → PSRAM → "OPI PSRAM" or "Enabled"
3. **Upload Firmware**: Open `edge_monitor/edge_monitor.ino` and upload
4. **Configure WiFi**: Edit WiFi credentials in the code
5. **Start Web Server**: Run `python web/app/main.py`

### First Boot
The device will automatically:
- Load saved settings from flash memory
- Initialize camera with HD (1280x720) default resolution
- Connect to WiFi and start HTTP server
- Begin recording videos every 60 seconds

## 📋 Features Overview

### ✅ Core Features
- **HD Default Resolution** (1280x720) - Much better than QVGA
- **Settings Persistence** - Survives reboots and power loss
- **FPS Control** - Full control via API and web UI
- **Comprehensive Error Detection** - Real-time failure monitoring
- **Background Uploads** - Intelligent pause/resume system
- **Circular Buffer Management** - Automatic storage cleanup
- **Timestamp Filenames** - NTP-synchronized naming
- **WiFi Status LED** - Visual system status indicators

### ✅ Resolution Support
| Resolution | Framesize | Pixels | Quality | FPS | Status |
|------------|-----------|--------|---------|-----|--------|
| QVGA | 4 | 320×240 | 15 | 30+ | ✅ Works |
| VGA | 5 | 640×480 | 15 | 20-30 | ✅ Works |
| SVGA | 6 | 800×600 | 20 | 15-20 | ✅ Works |
| XGA | 7 | 1024×768 | 25 | 12-15 | ✅ Works |
| **HD** | **8** | **1280×720** | **20** | **15-20** | ✅ **Default** |
| SXGA | 9 | 1280×1024 | 30 | 10-12 | ✅ Works |
| **UXGA** | **10** | **1600×1200** | **35** | **8-10** | ✅ **Maximum** |

## 🔧 Configuration

### WiFi Settings
```cpp
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";
```

### Server Configuration
```cpp
const char* IP = "192.168.1.57";  // Web server IP
const int SERVER_PORT = 8000;      // Web server port
const int HTTP_PORT = 80;         // Device HTTP port
```

### Storage Management
```cpp
const long MAX_STORAGE_MB = 24;        // Max storage for videos
const long MIN_FREE_SPACE_MB = 1;      // Min free space to maintain
const bool ENABLE_CIRCULAR_BUFFER = true; // Auto cleanup
```

### Upload Settings
```cpp
const long UPLOAD_CHUNK_SIZE = 8192;    // Upload chunk size
const long UPLOAD_TIMEOUT_MS = 60000;   // Upload timeout
const int MAX_UPLOAD_RETRIES = 3;       // Max retry attempts
const bool DELETE_AFTER_UPLOAD = true;  // Delete after upload
```

## 🌐 Web Interface

### Access Points
- **Main Dashboard**: `http://SERVER_IP:8000/`
- **Camera Control**: `http://SERVER_IP:8000/camera-control`
- **System Status**: `http://SERVER_IP:8000/system-status`
- **API Documentation**: `http://SERVER_IP:8000/api-docs`

### Device Discovery
The web interface automatically discovers ESP32 devices on the network using:
- **mDNS Discovery** (most reliable)
- **Common IP Scanning** (faster than full scan)
- **Network Scanning** (last resort)

### Camera Control Features
- **Real-time Preview** - Live camera stream
- **Resolution Control** - All supported resolutions
- **FPS Control** - Target FPS setting with recommendations
- **Quality Adjustment** - JPEG quality control
- **Recording Controls** - Start/stop/pause recording
- **System Commands** - Restart, photo capture, testing
- **Status Monitoring** - Real-time system health

## 📡 API Reference

### Device Endpoints (ESP32)

#### Status Information
```bash
GET http://DEVICE_IP/status
```
Returns comprehensive device status including:
- Camera/SD/WiFi status
- Memory usage (Heap/PSRAM)
- Capture statistics
- Current settings
- Resolution information

#### Camera Control
```bash
POST http://DEVICE_IP/control
Content-Type: application/json
{"var": "framesize", "val": 8}
```
Supported variables:
- `framesize` - Resolution (4-10)
- `quality` - JPEG quality (4-63)
- `brightness` - Brightness (-2 to 2)
- `contrast` - Contrast (-2 to 2)
- `saturation` - Saturation (-2 to 2)

#### Recording Configuration
```bash
POST http://DEVICE_IP/recording-config
Content-Type: application/json
{"setting": "fps", "value": 15}
```
Supported settings:
- `interval` - Capture interval (seconds)
- `duration` - Capture duration (seconds)
- `stream_interval` - Image stream interval (seconds)
- `fps` - Target FPS (0-60, 0=unlimited)

#### System Commands
```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "start"}
```
Supported commands:
- `start` - Start recording
- `stop` - Stop recording
- `pause` - Pause/resume system
- `restart` - Restart device
- `photo` - Capture single photo
- `test_camera` - Test camera functionality
- `test_sd` - Test SD card
- `clear_sd` - Clear all files

#### Batch Settings
```bash
POST http://DEVICE_IP/apply-settings
Content-Type: application/json
{
  "framesize": 8,
  "quality": 20,
  "fps": 15,
  "capture_interval": 60,
  "capture_duration": 10
}
```

#### File Management

#### SD Card File Management
```bash
GET http://DEVICE_IP/files
```
Returns list of files on SD card with sizes and upload queue status.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "clear_sd"}
```
Deletes all video and photo files from SD card.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "list_files"}
```
Prints all files to Serial Monitor for debugging.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "test_sd"}
```
Tests SD card write/read/delete operations.
```bash
GET http://DEVICE_IP/files
```
Returns list of files on SD card with sizes and upload queue status.

### Web Server Endpoints

#### Device Management
```bash
POST http://SERVER_IP:8000/api/device/configure
Content-Type: application/json
{"ip": "192.168.1.52", "port": 80}
```

#### Camera Control (via Web Server)
```bash
GET http://SERVER_IP:8000/api/camera/status
POST http://SERVER_IP:8000/api/camera/setting
POST http://SERVER_IP:8000/api/camera/command
```

#### File Management

#### SD Card File Management
```bash
GET http://DEVICE_IP/files
```
Returns list of files on SD card with sizes and upload queue status.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "clear_sd"}
```
Deletes all video and photo files from SD card.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "list_files"}
```
Prints all files to Serial Monitor for debugging.

```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "test_sd"}
```
Tests SD card write/read/delete operations.
```bash
GET http://SERVER_IP:8000/api/files
POST http://SERVER_IP:8000/api/clear-files
```

## 🎯 Resolution Guide

### HD (1280x720) - Default
**Best for most applications**
- 16:9 widescreen aspect ratio
- Good balance of quality/performance
- ~100 KB per frame
- Stable at 15-20 FPS
- Works reliably with 2 frame buffers

### UXGA (1600x1200) - Maximum
**For detail-critical applications**
- Maximum resolution available
- Requires quality 35+ for stability
- Auto-limited to 8 FPS if unlimited
- ~150 KB per frame
- Needs restart after configuration

### Configuration Examples

#### High Quality Setup
```bash
curl -X POST http://DEVICE_IP/apply-settings \
  -H "Content-Type: application/json" \
  -d '{
    "framesize": 9,
    "quality": 25,
    "fps": 10,
    "capture_interval": 60,
    "capture_duration": 10
  }'
```

#### Maximum Resolution Setup
```bash
curl -X POST http://DEVICE_IP/apply-settings \
  -H "Content-Type: application/json" \
  -d '{
    "framesize": 10,
    "quality": 35,
    "fps": 8,
    "capture_interval": 60,
    "capture_duration": 10
  }'
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "restart"}'
```

## 🔋 Power Management

### LED Status Indicators
The built-in LED provides visual feedback:
- **Solid ON** - Currently recording
- **2 Quick Flashes** - WiFi connected, actively uploading
- **1 Flash** - WiFi connected, idle
- **OFF** - WiFi disconnected or no activity

### Power Requirements
- **Minimum**: 5V 1A (for low resolution)
- **Recommended**: 5V 2A (for HD and above)
- **High Resolution**: 5V 2A+ (for UXGA)

### Power Optimization
- FPS limiting reduces power consumption
- Lower resolution uses less power
- Longer capture intervals save power
- Automatic upload pausing during recording

## 📊 Performance Monitoring

### Capture Statistics
Monitor system health via `/status` endpoint:
```json
{
  "capture_stats": {
    "total_captures": 1500,
    "failed_captures": 25,
    "failure_rate": 1.67,
    "consecutive_failures": 0,
    "last_failure": 123456,
    "degraded_mode": false
  }
}
```

### Health Indicators
- **🟢 Green (0-5%)** - System healthy
- **🟡 Yellow (5-10%)** - Warning, consider adjustments
- **🔴 Red (>10%)** - Critical, immediate action needed

### Memory Monitoring
- **Heap Memory** - Available RAM
- **PSRAM** - External memory for camera buffers
- **Frame Buffer Usage** - Camera memory allocation

## ��️ Troubleshooting

### Common Issues

#### High Capture Failure Rate
**Symptoms**: Failure rate > 5% in status
**Solutions**:
1. Increase JPEG quality number (35-40)
2. Reduce FPS (5-10 for high resolution)
3. Lower resolution
4. Check power supply (5V 2A minimum)
5. Restart device

#### Camera Initialization Fails
**Error Codes**:
- `0x105` (ESP_ERR_NO_MEM) - Out of memory
- `0x20002` - Camera driver error

**Solutions**:
1. Enable PSRAM in Arduino IDE
2. Check power supply
3. Verify camera connections
4. Try lower resolution

#### Settings Not Persisting
**Check**:
1. Serial Monitor for save confirmation
2. Settings load on boot
3. Flash memory not corrupted

#### Upload Failures
**Common Causes**:
1. Network connectivity issues
2. Server endpoint incorrect
3. File size too large
4. Server timeout

**Solutions**:
1. Check WiFi connection
2. Verify server URL
3. Reduce video quality/duration
4. Increase upload timeout

### Diagnostic Commands

#### Test Camera
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "test_camera"}'
```

#### Test SD Card

#### Test SD Card Health
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "test_sd"}'
```

#### List Files on SD Card
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "list_files"}'
```

#### Clear All Files
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "clear_sd"}'
```
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "test_sd"}'
```

#### Check System Status
```bash
curl http://DEVICE_IP/status | jq .
```

## 🔧 Advanced Configuration

### Buffer Allocation
The system automatically configures frame buffers:
- **Normal Resolution** (≤640x480): 1 buffer
- **High Resolution** (≥800x600): 2 buffers
- **Quality Enforcement**: Automatic quality adjustment

### PSRAM Management
- **Total PSRAM**: 8MB (XIAO ESP32S3)
- **Available**: ~5-6MB after system overhead
- **Safety Margins**: 2.0× for normal, 2.5× for UXGA
- **Quality-aware Estimation**: Accurate memory calculations

### Upload System
- **Intelligent Pausing**: Uploads pause during recording
- **Chunked Uploads**: Handles large files efficiently
- **Retry Logic**: Exponential backoff for failures
- **Queue Management**: Processes files in order

## 📁 File Structure

```
sky_monitor_edge/
├── edge_monitor/           # ESP32 firmware
│   ├── edge_monitor.ino   # Main firmware file
│   ├── CircularBuffer.h   # Storage management
│   ├── VideoUploader.h    # Upload system
│   └── camera_pins.h     # Hardware configuration
├── web/                   # Web server
│   └── app/
│       ├── main.py       # FastAPI web server
│       ├── templates/    # HTML templates
│       └── static/       # Static assets
├── test_*.py             # Diagnostic scripts
└── README.md             # This file
```

## 🚀 Getting Started

### 1. Hardware Setup
1. Connect ESP32-CAM to power supply
2. Insert SD card
3. Connect camera module
4. Power on device

### 2. Software Configuration
1. Edit WiFi credentials in `edge_monitor.ino`
2. Configure server IP address
3. Upload firmware to ESP32
4. Start web server: `python web/app/main.py`

### 3. First Use
1. Wait for device to boot (30 seconds)
2. Check Serial Monitor for status
3. Open web interface: `http://SERVER_IP:8000`
4. Discover and configure device
5. Start recording

### 4. Customization
1. Adjust resolution via web interface
2. Set preferred FPS and quality
3. Configure recording intervals
4. Settings automatically persist

## 📚 Additional Resources

### Testing Scripts
- `test_edge_monitor_api.py` - Comprehensive API testing
- `test_sd_card.py` - SD card diagnostics
- `test_server_file_apis.py` - Server file management
- `test_upload_endpoints.py` - Upload system testing

### Configuration Files
- `web/app/device_config.json` - Device configuration
- `web/app/requirements.txt` - Python dependencies

### Documentation
- All features are production-ready
- Comprehensive error handling
- Extensive logging and diagnostics
- Mobile-friendly web interface

## 🎉 Summary

This ESP32 Edge Monitor system provides:
- ✅ **Production-ready** camera monitoring
- ✅ **HD default resolution** (much better than QVGA)
- ✅ **Settings persistence** (survives reboots)
- ✅ **FPS control** (API + web UI)
- ✅ **Comprehensive error detection**
- ✅ **Background uploads** (intelligent pause/resume)
- ✅ **Web interface** (device discovery + control)
- ✅ **All resolutions supported** (QVGA to UXGA)
- ✅ **Extensive documentation** and testing tools

The system is designed to be reliable, configurable, and easy to use. All major issues have been resolved, and the system is ready for production deployment.

## 📁 SD Card File Management

### File Operations
The system provides comprehensive SD card file management through API endpoints and web interface.

#### List Files
```bash
GET http://DEVICE_IP/files
```
Returns JSON with all files on SD card:
```json
{
  "files": [
    {"name": "20250930230538.avi", "size": 149092, "path": "/20250930230538.avi"},
    {"name": "photo_123456.jpg", "size": 25600, "path": "/photo_123456.jpg"}
  ],
  "upload_queue_size": 2,
  "total_files": 2
}
```

#### Clear All Files
```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "clear_sd"}
```
**Safety Features:**
- Deletes only `.avi` and `.jpg` files
- Preserves system files
- Clears upload queue automatically
- Provides deletion count

#### List Files (Serial Monitor)
```bash
POST http://DEVICE_IP/command
Content-Type: application/json
{"command": "list_files"}
```
Prints all files to Serial Monitor for debugging.

### Web Interface
Access file management via: `http://SERVER_IP:8000/system-status`
- **📂 List Files Button** - Shows all files in table format
- **🗑️ Clear All Files Button** - Deletes with confirmation dialog
- **Real-time Updates** - Auto-refreshes after operations

### Camera Protection
The system prevents camera conflicts:
- **Image streaming** disabled during recording
- **Photo capture** blocked during recording
- **Upload operations** paused during recording
- **SD card access** protected from simultaneous operations

## 🚨 Critical SD Card Issues

### Common Problems

#### Files Written But Not Persisting
**Symptoms:**
```
DEBUG: Total bytes written to file: 143795
ERROR: Could not open file to check size: /20250930224236.avi
DEBUG: Listing SD card root directory: (nothing listed!)
```

**Root Causes:**
1. **SD Card Corruption** (Most Common)
   - Files written to cache but not persisting
   - Common with low-quality or fake SD cards
   - Filesystem corruption from improper unmounting

2. **SD Card Compatibility Issues**
   - Some cards don't work well with ESP32
   - Timing issues with specific models
   - Speed compatibility problems

3. **Hardware Connection Problems**
   - Loose SD card socket
   - Intermittent connection during write
   - Insufficient power during write operations

### Diagnostic Steps

#### Step 1: Test SD Card Health
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "test_sd"}'
```
Watch Serial Monitor for:
```
=== SD CARD TEST ===
Write: OK
Read: OK (23 bytes)
Delete: OK
```

#### Step 2: Check File Listing
```bash
curl -X POST http://DEVICE_IP/command \
  -H "Content-Type: application/json" \
  -d '{"command": "list_files"}'
```

#### Step 3: Verify Files via API
```bash
curl http://DEVICE_IP/files
```

### Solutions (Try in Order)

1. **Reinsert SD Card**
   - Power off ESP32 completely
   - Remove and reinsert SD card firmly
   - Power on and test

2. **Format SD Card**
   - Remove SD card
   - Format as **FAT32** (NOT exFAT or NTFS)
   - Reinsert and test

3. **Try Different SD Card**
   - Use **branded** cards (SanDisk, Samsung, Kingston)
   - Avoid fake cards (common on cheap marketplaces)
   - Recommended: 4GB-32GB, Class 10

4. **Reduce SD Write Speed**
   ```cpp
   // Add to setup() after SD.begin()
   SD.setClockFrequency(10000000); // Reduce to 10MHz
   ```

### Verification
After fixes, look for:
```
✓ Video saved successfully!
  File: /20250930224236.avi
  Size: 0.14 MB (143795 bytes)
  Frames: 85
  Duration: 9983 ms
```

## 🔄 Upload System Details

### Upload Queue Management
The system uses intelligent upload management:
- **Pause During Recording** - Prevents SD card conflicts
- **Resume After Recording** - Automatic queue processing
- **Chunked Uploads** - Handles large files efficiently
- **Retry Logic** - Exponential backoff for failures

### Path Normalization
Handles file path inconsistencies:
```cpp
// Tries multiple path variations:
1. With leading slash: "/video.avi"
2. Without leading slash: "video.avi"  
3. Original path as provided
```

### Image Streaming
Separate from video uploads:
- **Real-time streaming** every 5 seconds
- **Disabled during recording** to prevent conflicts
- **10-second timeout** for reliability
- **Detailed error logging** for troubleshooting

### Upload Monitoring
Watch Serial Monitor for:
```
Processing upload: /20250930224236.avi
Starting upload: /20250930224236.avi (0.14MB)
Connecting to: 192.168.1.57:8000/upload
Connected! Sending HTTP request...
Upload successful: /20250930224236.avi (Response: 200)
```

### Error Handling
Common upload errors and solutions:
- **"Failed to open file"** - Check SD card health
- **"Connection refused"** - Verify server is running
- **"Timeout"** - Check network connectivity
- **"File not found"** - Verify file persistence


## 🏗️ System Architecture

```
┌─────────────────┐    HTTP API     ┌─────────────────┐
│   Web Browser   │◄──────────────►│   Web Server    │
│  (User Interface)│                │   (FastAPI)     │
└─────────────────┘                └─────────────────┘
                                            │
                                            │ HTTP Requests
                                            │ Image Upload
                                            ▼
                                   ┌─────────────────┐
                                   │  Edge Device    │
                                   │   (ESP32-S3)    │
                                   │  - Camera       │
                                   │  - SD Storage   │
                                   │  - WiFi         │
                                   └─────────────────┘
```

## 🎛️ Complete Camera Settings Reference

### Frame Sizes (All Available)
- **UXGA (1600x1200)** - Highest quality, largest files, requires quality 35+
- **SXGA (1280x1024)** - Very high quality, good for detail
- **HD (1280x720)** - **Default**, widescreen, excellent balance
- **XGA (1024x768)** - High quality, good for most applications
- **SVGA (800x600)** - Medium-high quality
- **VGA (640x480)** - Good balance of quality/performance
- **HVGA (480x320)** - Medium quality
- **CIF (400x296)** - Lower quality
- **QVGA (320x240)** - Recommended for memory-constrained devices
- **HQVGA (240x176)** - Low quality
- **QCIF (176x144)** - Very low quality
- **QQVGA (160x120)** - Smallest, fastest, minimal memory usage

### Quality Settings Guide
- **4-10**: Highest quality, largest files (use with caution)
- **10-15**: High quality (recommended for important recordings)
- **15-25**: Medium quality (good balance)
- **25-35**: Lower quality, smaller files (recommended for high resolution)
- **35-63**: Lowest quality, smallest files (for UXGA stability)

### Image Adjustments
- **Brightness**: -2 to +2 (0 = default)
- **Contrast**: -2 to +2 (0 = default)  
- **Saturation**: -2 to +2 (0 = default)

## ⚙️ Recording Configuration Details

### Timing Settings
- **Capture Interval**: Time between recordings (10-3600 seconds)
- **Capture Duration**: Length of each recording (1-300 seconds)
- **Stream Interval**: How often to send images to server (1-60 seconds)

### Storage Management Features
- **Automatic cleanup** of old files when storage is full
- **Configurable storage limits** (default: 24MB max, 1MB min free)
- **Circular buffer management** - keeps newest files
- **Upload queue integration** - manages file uploads intelligently

## 🔧 Complete System Commands Reference

### Recording Control Commands
- **start** - Begin automatic recording
- **stop** - Stop all recording
- **pause** - Pause/resume system operation
- **photo** - Capture single photo (saved to SD card)

### System Control Commands
- **restart** - Reboot the edge device
- **test_camera** - Test camera functionality (3-frame test)
- **test_sd** - Test SD card write/read/delete operations
- **list_files** - List all files on SD card (Serial Monitor output)
- **clear_sd** - Delete all video and photo files from SD card

### Diagnostic Commands
- **refresh_status** - Update system status
- **apply_settings** - Save all current settings to flash

## 📊 Advanced Monitoring Features

### Real-time Status Monitoring
- **Camera connection status** - Ready/not ready
- **WiFi connectivity** - Connected/disconnected with signal strength
- **Recording state** - Active/stopped/paused
- **Storage usage** - Used space, available space, file count
- **System uptime** - Device runtime and memory usage
- **Capture statistics** - Success/failure rates, consecutive failures

### Live Preview Capabilities
- **Current camera image** updated every 2-5 seconds
- **Resolution and frame rate** display
- **File count and storage** statistics
- **System health indicators** with color coding

### System Logging
- **Real-time activity logging** in web interface
- **Error reporting and diagnostics** with timestamps
- **Command execution status** with success/failure feedback
- **Serial Monitor output** for detailed debugging

## 🔄 Network Configuration Details

### Device Discovery Process
The system automatically discovers edge devices by:
1. **mDNS Discovery** - Most reliable method using Bonjour/Avahi
2. **Common IP Scanning** - Tests known ESP32 IP addresses
3. **Network Scanning** - Scans local subnet (last resort)
4. **Device Identification** - Verifies edge monitor device type
5. **Registry Maintenance** - Keeps track of discovered devices

### Image Streaming System
- **Automatic streaming** - Edge device streams images to web server
- **Configurable interval** - Default 5 seconds, adjustable 1-60 seconds
- **Smart scheduling** - Only streams when not recording
- **Automatic retry** - Handles connection failures gracefully
- **Conflict prevention** - Pauses during recording to avoid SD card conflicts

## 📈 Performance Optimization Guide

### For Better Performance (Speed Priority)
- **Use QVGA (320x240)** for fastest processing
- **Increase JPEG quality number** (25-35) for smaller files
- **Reduce streaming frequency** (10-30 seconds) for slower networks
- **Monitor memory usage** and adjust frame buffer settings
- **Use Class 10+ SD cards** for faster write speeds

### For Better Quality (Quality Priority)
- **Use VGA or higher resolution** (640x480+)
- **Decrease JPEG quality number** (10-20) for better image quality
- **Ensure stable power supply** (5V 2A minimum)
- **Use high-speed SD cards** (Class 10 or better)
- **Limit FPS** (5-15) for high resolution stability

### Hardware Recommendations
- **Power Supply**: 5V 2A minimum (3A recommended for UXGA)
- **SD Card**: Class 10, 8-32GB, branded (SanDisk, Samsung, Kingston)
- **WiFi**: Strong signal (-50 dBm or better)
- **Memory**: PSRAM enabled for resolutions above VGA


## 🔍 Enhanced Troubleshooting

### Additional Common Issues

#### Device Not Found
**Symptoms**: Web interface shows "No devices found"
**Solutions**:
1. Check WiFi connection on edge device
2. Verify device and server are on same network
3. Check firewall settings
4. Ensure device HTTP server is running
5. Try manual device configuration

#### Image Streaming Issues
**Symptoms**: Live preview not updating
**Solutions**:
1. Verify web server URL in device configuration
2. Check network connectivity between devices
3. Monitor device memory usage
4. Adjust streaming interval (increase to 10+ seconds)
5. Check if device is recording (streaming disabled during recording)

#### Settings Not Applying
**Symptoms**: Changes don't take effect
**Solutions**:
1. Check device response in browser developer tools (F12)
2. Verify JSON format in requests
3. Monitor device serial output for errors
4. Try applying settings individually
5. Restart device after major changes

#### Camera Initialization Failed
**Symptoms**: Camera not working, error codes in Serial Monitor
**Solutions**:
1. Check camera module connections
2. Verify PSRAM availability (Tools → PSRAM → Enabled)
3. Try lower resolution settings
4. Check power supply stability (5V 2A minimum)
5. Restart device to reinitialize camera

### Debug Information

Enable detailed logging by:
1. **Browser Developer Tools** (F12) - Monitor console and network tabs
2. **Serial Monitor** - View device output at 115200 baud
3. **Web Server Logs** - Check terminal output for server errors
4. **Network Analysis** - Use tools like Wireshark for network issues

### Performance Monitoring

Monitor these key metrics:
- **Capture Failure Rate** - Should be < 5%
- **Memory Usage** - Heap and PSRAM levels
- **Upload Success Rate** - Check upload queue status
- **WiFi Signal Strength** - Should be > -70 dBm
- **SD Card Write Speed** - Monitor file persistence

