from fastapi import FastAPI, File, UploadFile, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
import uvicorn
import os
import json
import requests
import time
import io
from datetime import datetime
from typing import Dict, Any, Optional
from pydantic import BaseModel
import logging
import io

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)



# Background device discovery
import asyncio
import threading
from concurrent.futures import ThreadPoolExecutor

executor = ThreadPoolExecutor(max_workers=1)

def discover_devices_background():
    """Run device discovery in background thread"""
    try:
        devices = discover_edge_devices(force=True)
        if devices:
            print(f"Background discovery found {len(devices)} devices")
        else:
            print("Background discovery found no devices")
    except Exception as e:
        print(f"Background discovery error: {e}")

def start_background_discovery():
    """Start device discovery in background"""
    executor.submit(discover_devices_background)

app = FastAPI(title="Edge Monitor Web Server")

# Mount static files and templates
import os
from pathlib import Path

# Get the directory where this script is located
BASE_DIR = Path(__file__).parent

app.mount("/static", StaticFiles(directory=str(BASE_DIR / "static")), name="static")
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))
# Add this after: app = FastAPI(title="Edge Monitor Web Server")

@app.middleware("http")
async def log_requests(request: Request, call_next):
    """Log all incoming HTTP requests"""
    start_time = time.time()
    
    # Log incoming request
    logger.info(f"�� {request.method} {request.url.path} from {request.client.host if request.client else 'unknown'}")
    
    # Process request
    response = await call_next(request)
    
    # Log response
    process_time = time.time() - start_time
    logger.info(f"📤 {request.method} {request.url.path} -> {response.status_code} ({process_time:.3f}s)")
    
    return response



# Global variables for device management
connected_devices: Dict[str, Dict[str, Any]] = {}
last_device_discovery = 0
DISCOVERY_INTERVAL = 30  # seconds

# Device configuration storage
DEVICE_CONFIG_FILE = BASE_DIR / "device_config.json"
configured_device = None  # Will store {"ip": "x.x.x.x", "port": 80}

def load_device_config():
    """Load saved device configuration from file"""
    global configured_device
    if DEVICE_CONFIG_FILE.exists():
        try:
            with open(DEVICE_CONFIG_FILE, 'r') as f:
                configured_device = json.load(f)
                logger.info(f"Loaded device config: {configured_device}")
                return configured_device
        except Exception as e:
            logger.error(f"Error loading device config: {e}")
    return None

def save_device_config(ip: str, port: int):
    """Save device configuration to file"""
    global configured_device
    configured_device = {"ip": ip, "port": port}
    try:
        with open(DEVICE_CONFIG_FILE, 'w') as f:
            json.dump(configured_device, f)
        logger.info(f"Saved device config: {configured_device}")
        return True
    except Exception as e:
        logger.error(f"Error saving device config: {e}")
        return False

def get_primary_device():
    """Get the primary edge device IP from saved configuration"""
    global configured_device
    
    # Load device config if not already loaded
    if configured_device is None:
        load_device_config()
    
    # Use the configured device if available
    if configured_device and configured_device.get('ip'):
        return f"http://{configured_device['ip']}:{configured_device.get('port', 80)}"
    
    # Fallback to discovered devices if any exist
    if connected_devices:
        device_key = list(connected_devices.keys())[0]
        device_info = connected_devices[device_key]
        return f"http://{device_info['ip']}:{device_info['port']}"
    
    # No device configured
    return None

# Pydantic models for API requests
class CameraSetting(BaseModel):
    setting: str
    value: int

class RecordingSetting(BaseModel):
    setting: str
    value: int

class Command(BaseModel):
    command: str

class CameraSettings(BaseModel):
    framesize: Optional[int] = None
    quality: Optional[int] = None
    brightness: Optional[int] = None
    contrast: Optional[int] = None
    saturation: Optional[int] = None
    capture_interval: Optional[int] = None
    capture_duration: Optional[int] = None
    stream_interval: Optional[int] = None
    fps: Optional[int] = None
    frame_delay: Optional[int] = None

# Device discovery and management
def discover_edge_devices(force=False):
    """Discover edge monitoring devices on the network using multiple methods"""
    global last_device_discovery, connected_devices
    
    current_time = time.time()
    if not force and current_time - last_device_discovery < DISCOVERY_INTERVAL:
        return list(connected_devices.keys())
    
    logger.info("Discovering edge devices...")
    discovered = []
    
    # Method 1: Try mDNS discovery first (most reliable)
    try:
        import subprocess
        result = subprocess.run(['avahi-browse', '-rt', '_http._tcp'], 
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if 'edge-monitor' in line and 'IPv4' in line:
                    parts = line.split()
                    if len(parts) >= 7:
                        ip = parts[7]
                        if try_connect_device(ip, 80, discovered, current_time):
                            logger.info(f"Found device via mDNS: {ip}")
    except:
        pass  # mDNS not available, continue with other methods
    
    # Method 2: Try known common ESP32 IPs (faster than scanning)
    common_ips = ["192.168.1.52", "192.168.1.100", "192.168.1.101", "192.168.1.102"]
    for ip in common_ips:
        if try_connect_device(ip, 80, discovered, current_time):
            continue
    
    # Method 3: Network scanning (slowest, last resort)
    import ipaddress
    import socket
    
    try:
        # Get local IP to determine subnet
        hostname = socket.gethostname()
        local_ip = socket.gethostbyname(hostname)
        network = ipaddress.IPv4Network(f"{local_ip}/24", strict=False)
        
        # Common ports for ESP32 devices
        test_ports = [80, 8080, 81]
        
        # Scan network (limit to 30 IPs for speed)
        for ip in list(network.hosts())[:30]:
            ip_str = str(ip)
            if ip_str in [d.split(':')[0] for d in discovered]:
                continue  # Skip already discovered
                
            for port in test_ports:
                if try_connect_device(ip_str, port, discovered, current_time):
                    break
                    
    except Exception as e:
        logger.error(f"Network discovery error: {e}")
    
    last_device_discovery = current_time
    return discovered

def try_connect_device(ip_str, port, discovered, current_time):
    """Try to connect to a device and add it if it's an edge monitor"""
    try:
        # Try status endpoint first (most reliable)
        response = requests.get(f"http://{ip_str}:{port}/status", timeout=3)
        if response.status_code == 200:
            try:
                data = response.json()
                # Check if it's our edge monitor device
                if (data.get("device_type") == "edge_monitor" or 
                    "camera_ready" in data or 
                    "wifi_connected" in data or
                    "is_recording" in data):
                    device_info = {
                        "ip": ip_str,
                        "port": port,
                        "last_seen": current_time,
                        "info": data
                    }
                    connected_devices[f"{ip_str}:{port}"] = device_info
                    discovered.append(f"{ip_str}:{port}")
                    logger.info(f"Discovered edge device at {ip_str}:{port}")
                    return True
            except:
                pass
                
    except requests.exceptions.RequestException:
        # Try root endpoint as fallback
        try:
            response = requests.get(f"http://{ip_str}:{port}/", timeout=2)
            if response.status_code == 200:
                text = response.text.lower()
                if ("esp32" in text and "edge monitor" in text) or "edge_monitor" in text:
                    # Found an ESP32 edge monitor device
                    device_info = {
                        "ip": ip_str,
                        "port": port,
                        "last_seen": current_time,
                        "info": {
                            "device_type": "edge_monitor", 
                            "detected_via": "root_endpoint",
                            "wifi_connected": True,
                            "camera_ready": True,  # Assume true if serving web page
                            "sd_ready": True
                        }
                    }
                    connected_devices[f"{ip_str}:{port}"] = device_info
                    discovered.append(f"{ip_str}:{port}")
                    logger.info(f"Discovered ESP32 edge monitor at {ip_str}:{port} via root endpoint")
                    return True
        except:
            pass
    except:
        pass
    
    return False

def make_device_request(endpoint: str, method: str = "GET", data: dict = None):
    """Make a request to the edge device"""
    device_url = get_primary_device()
    if not device_url:
        raise HTTPException(status_code=503, detail="No edge device connected")
    
    try:
        url = f"{device_url}{endpoint}"
        # ESP32 devices can be slow to respond, especially when busy with recording/uploading
        timeout = 20
        if method == "GET":
            response = requests.get(url, timeout=timeout)
        elif method == "POST":
            response = requests.post(url, json=data, timeout=timeout)
        else:
            raise ValueError(f"Unsupported method: {method}")
        
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        logger.error(f"Device request failed: {e}")
        raise HTTPException(status_code=503, detail=f"Device communication error: {str(e)}")

# Web interface routes
@app.get("/", response_class=HTMLResponse)
async def read_root(request: Request):
    """Main landing page with navigation and API documentation"""
    return templates.TemplateResponse("index.html", {"request": request})

@app.get("/camera-control", response_class=HTMLResponse)
async def camera_control(request: Request):
    """Camera control interface"""
    return templates.TemplateResponse("camera_control.html", {"request": request})

@app.get("/api-docs", response_class=HTMLResponse)
async def api_documentation(request: Request):
    """API documentation page"""
    return templates.TemplateResponse("api_docs.html", {"request": request})

@app.get("/system-status", response_class=HTMLResponse)
async def system_status_page(request: Request):
    """System status and monitoring page"""
    return templates.TemplateResponse("system_status.html", {"request": request})

@app.get("/test", response_class=HTMLResponse)
async def compatibility_test(request: Request):
    """ESP32 compatibility test page"""
    return templates.TemplateResponse("test_compatibility.html", {"request": request})

@app.get("/api/info")
async def api_info():
    """Get API information and available endpoints"""
    return {
        "title": "Edge Monitor Web Server API",
        "version": "1.0.0",
        "description": "API for controlling and monitoring ESP32-based edge camera devices",
        "endpoints": {
            "device_discovery": {
                "url": "/api/camera/discover",
                "method": "POST",
                "description": "Discover edge monitoring devices on the network",
                "response": "List of discovered devices with IP addresses"
            },
            "add_device": {
                "url": "/api/camera/add-device",
                "method": "POST",
                "description": "Manually add a device by IP address",
                "parameters": {"ip": "string", "port": "integer (optional, default 80)"},
                "response": "Success confirmation with device info"
            },
            "device_status": {
                "url": "/api/camera/status",
                "method": "GET", 
                "description": "Get current camera and system status",
                "response": "Device status including camera, WiFi, SD card, memory info"
            },
            "camera_settings": {
                "url": "/api/camera/setting",
                "method": "POST",
                "description": "Update individual camera settings",
                "parameters": {"setting": "string", "value": "integer"},
                "examples": ["framesize", "quality", "brightness", "contrast", "saturation"]
            },
            "recording_settings": {
                "url": "/api/camera/recording-setting", 
                "method": "POST",
                "description": "Update recording configuration",
                "parameters": {"setting": "string", "value": "integer"},
                "examples": ["interval", "duration", "stream_interval"]
            },
            "device_commands": {
                "url": "/api/camera/command",
                "method": "POST", 
                "description": "Send commands to the device",
                "parameters": {"command": "string"},
                "examples": ["start", "stop", "pause", "restart", "photo"]
            },
            "bulk_settings": {
                "url": "/api/camera/apply-settings",
                "method": "POST",
                "description": "Apply multiple settings at once",
                "parameters": "JSON object with multiple setting key-value pairs"
            },
            "camera_stream": {
                "url": "/api/camera/stream",
                "method": "GET",
                "description": "Get current camera image",
                "response": "JPEG image stream"
            },
            "image_upload": {
                "url": "/api/upload-image", 
                "method": "POST",
                "description": "Receive images from edge devices",
                "parameters": "Multipart file upload"
            },
            "video_upload": {
                "url": "/upload",
                "method": "POST", 
                "description": "Upload video files from edge devices",
                "parameters": "Multipart file upload"
            }
        },
        "web_pages": {
            "home": {
                "url": "/",
                "description": "Main landing page with navigation and overview"
            },
            "camera_control": {
                "url": "/camera-control",
                "description": "Interactive camera control interface"
            },
            "api_documentation": {
                "url": "/api-docs", 
                "description": "Detailed API documentation and examples"
            },
            "system_status": {
                "url": "/system-status",
                "description": "System monitoring and device status dashboard"
            }
        },
        "device_requirements": {
            "device_type": "edge_monitor",
            "required_endpoints": ["/status", "/control", "/command", "/capture"],
            "optional_endpoints": ["/recording-config", "/apply-settings"],
            "communication": "HTTP REST API over WiFi"
        }
    }

# API endpoints for camera control


@app.post("/api/camera/discover-manual")
async def discover_camera_manual():
    """Manually discover edge monitoring devices with UI feedback"""
    try:
        # Start background discovery
        start_background_discovery()
        return {
            "success": True,
            "message": "Device discovery started in background",
            "status": "discovering"
        }
    except Exception as e:
        logger.error(f"Manual discovery error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.get("/api/camera/discovery-status")
async def get_discovery_status():
    """Get current device discovery status"""
    return {
        "devices_found": len(connected_devices),
        "device_list": list(connected_devices.keys()),
        "last_discovery": last_device_discovery
    }

@app.post("/api/camera/discover")
async def discover_camera():
    """Discover edge monitoring devices"""
    try:
        devices = discover_edge_devices(force=True)
        if devices:
            device_key = devices[0]
            device_info = connected_devices[device_key]
            return {
                "success": True,
                "device_ip": f"{device_info['ip']}:{device_info['port']}",
                "devices": devices
            }
        else:
            return {
                "success": False,
                "message": "No devices found"
            }
    except Exception as e:
        logger.error(f"Discovery error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/camera/add-device")
async def add_device(request: Request):
    """Manually add a device by IP address"""
    try:
        data = await request.json()
        ip = data.get("ip")
        port = data.get("port", 80)
        
        if not ip:
            return {"success": False, "error": "IP address required"}
        
        # Test connection to the device
        device_url = f"http://{ip}:{port}"
        try:
            response = requests.get(f"{device_url}/status", timeout=5)
            if response.status_code == 200:
                device_info = {
                    "ip": ip,
                    "port": port,
                    "last_seen": time.time(),
                    "info": response.json()
                }
                connected_devices[f"{ip}:{port}"] = device_info
                logger.info(f"Manually added device at {ip}:{port}")
                return {
                    "success": True,
                    "message": f"Device added successfully: {ip}:{port}",
                    "device_ip": f"{ip}:{port}"
                }
        except:
            pass
        
        return {
            "success": False,
            "error": f"Could not connect to device at {ip}:{port}"
        }
        
    except Exception as e:
        logger.error(f"Add device error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/device/configure")
async def configure_device(request: Request):
    """Configure the device IP and port - saves to persistent storage"""
    try:
        data = await request.json()
        ip = data.get("ip")
        port = data.get("port", 80)
        
        logger.info(f"Configure device request: {ip}:{port}")
        
        if not ip:
            return {"success": False, "error": "IP address required"}
        
        # First test basic connectivity with nc (much faster)
        import subprocess
        try:
            logger.info(f"Testing basic connectivity to {ip}:{port} with nc")
            result = subprocess.run(['nc', '-zv', ip, str(port)], 
                                  capture_output=True, text=True, timeout=3)
            if result.returncode != 0:
                logger.error(f"nc connectivity test failed: {result.stderr}")
                return {
                    "success": False,
                    "error": f"Cannot reach device at {ip}:{port}. Check IP address and network connection."
                }
            logger.info(f"nc connectivity test passed: {result.stdout}")
        except subprocess.TimeoutExpired:
            logger.error(f"nc connectivity test timed out")
            return {
                "success": False,
                "error": f"Connection timeout to device at {ip}:{port}"
            }
        except FileNotFoundError:
            logger.warning("nc command not found, skipping connectivity test")
        except Exception as e:
            logger.warning(f"nc test failed: {e}, continuing with HTTP test")
        
        # Now test HTTP endpoint (this can be slow)
        device_url = f"http://{ip}:{port}"
        try:
            logger.info(f"Testing HTTP endpoint {device_url}/status")
            response = requests.get(f"{device_url}/status", timeout=15)
            logger.info(f"HTTP test response: {response.status_code}")
            
            if response.status_code == 200:
                device_status = response.json()
                logger.info(f"Device status retrieved: {device_status.get('device_type')}")
                
                # Save configuration
                logger.info(f"Saving device config to: {DEVICE_CONFIG_FILE}")
                if save_device_config(ip, port):
                    logger.info(f"Device configured successfully: {ip}:{port}")
                    return {
                        "success": True,
                        "message": f"Device configured successfully: {ip}:{port}",
                        "device": {
                            "ip": ip,
                            "port": port,
                            "status": device_status
                        }
                    }
                else:
                    logger.error(f"Failed to save device configuration to {DEVICE_CONFIG_FILE}")
                    return {
                        "success": False,
                        "error": "Failed to save device configuration"
                    }
            else:
                return {
                    "success": False,
                    "error": f"Device returned status code {response.status_code}"
                }
        except requests.exceptions.Timeout:
            logger.error(f"HTTP request timeout to {device_url}")
            return {
                "success": False,
                "error": f"Device HTTP response timeout at {ip}:{port} (device may be busy)"
            }
        except requests.exceptions.RequestException as e:
            logger.error(f"HTTP request error to {device_url}: {str(e)}")
            return {
                "success": False,
                "error": f"HTTP request failed to device at {ip}:{port}. Error: {str(e)}"
            }
        
    except Exception as e:
        logger.error(f"Configure device error: {e}")
        import traceback
        traceback.print_exc()
        return {
            "success": False,
            "error": str(e)
        }

@app.get("/api/device/config")
async def get_device_config():
    """Get current device configuration"""
    global configured_device
    if configured_device:
        # Try to get current status (with quick timeout for status check)
        try:
            device_url = f"http://{configured_device['ip']}:{configured_device.get('port', 80)}"
            response = requests.get(f"{device_url}/status", timeout=5)
            if response.status_code == 200:
                return {
                    "success": True,
                    "configured": True,
                    "device": configured_device,
                    "status": response.json(),
                    "online": True
                }
        except:
            pass
        
        return {
            "success": True,
            "configured": True,
            "device": configured_device,
            "online": False
        }
    else:
        return {
            "success": True,
            "configured": False,
            "device": None
        }

@app.get("/api/camera/status")
async def get_camera_status():
    """Get current camera and system status"""
    try:
        status = make_device_request("/status")
        return {
            "success": True,
            "status": status
        }
    except Exception as e:
        logger.error(f"Status error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/camera/setting")
async def update_camera_setting(setting: CameraSetting):
    """Update a camera setting"""
    try:
        result = make_device_request("/control", "POST", {
            "var": setting.setting,
            "val": setting.value
        })
        return {
            "success": True,
            "result": result
        }
    except Exception as e:
        logger.error(f"Setting update error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/camera/recording-setting")
async def update_recording_setting(setting: RecordingSetting):
    """Update a recording setting"""
    try:
        result = make_device_request("/recording-config", "POST", {
            "setting": setting.setting,
            "value": setting.value
        })
        return {
            "success": True,
            "result": result
        }
    except Exception as e:
        logger.error(f"Recording setting error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/camera/command")
async def send_camera_command(command: Command):
    """Send a command to the camera"""
    try:
        result = make_device_request("/command", "POST", {
            "command": command.command
        })
        return {
            "success": True,
            "result": result
        }
    except Exception as e:
        logger.error(f"Command error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.post("/api/camera/apply-settings")
async def apply_all_settings(settings: CameraSettings):
    """Apply multiple settings at once"""
    try:
        # Convert settings to dict and filter out None values
        settings_dict = {k: v for k, v in settings.dict().items() if v is not None}
        
        result = make_device_request("/apply-settings", "POST", settings_dict)
        return {
            "success": True,
            "result": result
        }
    except Exception as e:
        logger.error(f"Apply settings error: {e}")
        return {
            "success": False,
            "error": str(e)
        }

@app.get("/api/camera/stream")
async def get_camera_stream():
    """Get current camera image"""
    try:
        device_url = get_primary_device()
        if not device_url:
            raise HTTPException(status_code=503, detail="No edge device connected")
        
        # Get image from device
        response = requests.get(f"{device_url}/capture", timeout=10)
        response.raise_for_status()
        
        return StreamingResponse(
            io.BytesIO(response.content),
            media_type="image/jpeg"
        )
    except Exception as e:
        logger.error(f"Stream error: {e}")
        # Return placeholder image
        placeholder_path = BASE_DIR / "static" / "placeholder-camera.png"
        if placeholder_path.exists():
            with open(placeholder_path, "rb") as f:
                return StreamingResponse(
                    io.BytesIO(f.read()),
                    media_type="image/png"
                )
        raise HTTPException(status_code=503, detail="Camera stream unavailable")

# Endpoint for receiving images from edge devices (raw JPEG data)
@app.post("/api/upload-image")
async def upload_image(request: Request):
    """Receive raw JPEG images from edge devices"""
    try:
        # Create uploads directory if it doesn't exist
        upload_dir = BASE_DIR / "uploads" / "images"
        os.makedirs(upload_dir, exist_ok=True)
        
        # Read raw image data from request body
        content = await request.body()
        
        if not content:
            raise HTTPException(status_code=400, detail="No image data received")
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{timestamp}_stream.jpg"
        file_path = upload_dir / filename
        
        # Save the raw JPEG data
        with open(file_path, "wb") as buffer:
            buffer.write(content)
        
        # Print full file path where image was saved
        full_path = str(file_path.absolute())
        logger.info(f"Received image stream: {filename} ({len(content)} bytes)")
        logger.info(f"Image saved to: {full_path}")
        print(f"✓ Image uploaded and saved to: {full_path}")
        
        return {
            "success": True,
            "filename": filename,
            "size": len(content),
            "timestamp": timestamp,
            "file_path": full_path
        }
    except Exception as e:
        logger.error(f"Image upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# Alternative endpoint for multipart file uploads (for compatibility)
@app.post("/api/upload-image-file")
async def upload_image_file(file: UploadFile = File(...)):
    """Receive images as multipart form data"""
    try:
        # Create uploads directory if it doesn't exist
        upload_dir = BASE_DIR / "uploads" / "images"
        os.makedirs(upload_dir, exist_ok=True)
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{timestamp}_{file.filename or 'upload.jpg'}"
        file_path = upload_dir / filename
        
        # Save the file
        with open(file_path, "wb") as buffer:
            content = await file.read()
            buffer.write(content)
        
        # Print full file path where image was saved
        full_path = str(file_path.absolute())
        logger.info(f"Received image file: {filename} ({len(content)} bytes)")
        logger.info(f"Image file saved to: {full_path}")
        print(f"✓ Image file uploaded and saved to: {full_path}")
        
        return {
            "success": True,
            "filename": filename,
            "size": len(content),
            "timestamp": timestamp,
            "file_path": full_path
        }
    except Exception as e:
        logger.error(f"Image file upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# Original upload endpoint (for videos)
@app.post("/upload")
async def upload_file(file: UploadFile = File(...)):
    """Upload endpoint for video files"""
    try:
        # Create uploads directory if it doesn't exist
        upload_dir = BASE_DIR / "uploads"
        os.makedirs(upload_dir, exist_ok=True)
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        file_extension = os.path.splitext(file.filename)[1] if file.filename else ""
        filename = f"{timestamp}{file_extension}"
        file_path = upload_dir / filename
        
        # Save the file
        with open(file_path, "wb") as buffer:
            content = await file.read()
            buffer.write(content)
        
        # Print full file path where video was saved
        full_path = str(file_path.absolute())
        logger.info(f"File uploaded: {filename} ({len(content)} bytes)")
        logger.info(f"Video file saved to: {full_path}")
        print(f"✓ Video uploaded and saved to: {full_path}")
        
        return {
            "message": "Upload successful",
            "filename": filename,
            "size": len(content),
            "file_path": full_path
        }
    except Exception as e:
        logger.error(f"Video upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# File Management APIs
@app.get("/api/files")
async def list_server_files():
    """List all files on the server"""
    try:
        upload_dir = BASE_DIR / "uploads"
        images_dir = upload_dir / "images"
        
        files = []
        total_size = 0
        
        # List video files
        if upload_dir.exists():
            for file_path in upload_dir.iterdir():
                if file_path.is_file() and not file_path.name.startswith('.'):
                    file_size = file_path.stat().st_size
                    files.append({
                        "name": file_path.name,
                        "size": file_size,
                        "path": str(file_path.relative_to(BASE_DIR)),
                        "type": "video",
                        "modified": datetime.fromtimestamp(file_path.stat().st_mtime).isoformat()
                    })
                    total_size += file_size
        
        # List image files
        if images_dir.exists():
            for file_path in images_dir.iterdir():
                if file_path.is_file() and not file_path.name.startswith('.'):
                    file_size = file_path.stat().st_size
                    files.append({
                        "name": file_path.name,
                        "size": file_size,
                        "path": str(file_path.relative_to(BASE_DIR)),
                        "type": "image",
                        "modified": datetime.fromtimestamp(file_path.stat().st_mtime).isoformat()
                    })
                    total_size += file_size
        
        # Sort by modification time (newest first)
        files.sort(key=lambda x: x["modified"], reverse=True)
        
        return {
            "files": files,
            "total_files": len(files),
            "total_size": total_size,
            "total_size_mb": round(total_size / (1024 * 1024), 2)
        }
    except Exception as e:
        logger.error(f"List files error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/clear-files")
async def clear_server_files():
    """Clear all uploaded files from the server"""
    try:
        upload_dir = BASE_DIR / "uploads"
        images_dir = upload_dir / "images"
        
        deleted_count = 0
        failed_count = 0
        total_size_freed = 0
        
        # Delete video files
        if upload_dir.exists():
            for file_path in upload_dir.iterdir():
                if file_path.is_file() and not file_path.name.startswith('.'):
                    try:
                        file_size = file_path.stat().st_size
                        file_path.unlink()
                        deleted_count += 1
                        total_size_freed += file_size
                        logger.info(f"Deleted video file: {file_path.name}")
                    except Exception as e:
                        failed_count += 1
                        logger.error(f"Failed to delete {file_path.name}: {e}")
        
        # Delete image files
        if images_dir.exists():
            for file_path in images_dir.iterdir():
                if file_path.is_file() and not file_path.name.startswith('.'):
                    try:
                        file_size = file_path.stat().st_size
                        file_path.unlink()
                        deleted_count += 1
                        total_size_freed += file_size
                        logger.info(f"Deleted image file: {file_path.name}")
                    except Exception as e:
                        failed_count += 1
                        logger.error(f"Failed to delete {file_path.name}: {e}")
        
        return {
            "success": True,
            "message": f"Cleared server files: deleted {deleted_count} files, freed {round(total_size_freed / (1024 * 1024), 2)} MB",
            "deleted_count": deleted_count,
            "failed_count": failed_count,
            "size_freed": total_size_freed,
            "size_freed_mb": round(total_size_freed / (1024 * 1024), 2)
        }
    except Exception as e:
        logger.error(f"Clear files error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/device/files")
async def list_device_files():
    """List files on the connected ESP32 device"""
    try:
        device_url = get_primary_device()
        if not device_url:
            raise HTTPException(status_code=503, detail="No edge device connected")
        
        # Get files from device
        response = requests.get(f"{device_url}/files", timeout=10)
        response.raise_for_status()
        
        device_data = response.json()
        
        return {
            "device_url": device_url,
            "files": device_data.get("files", []),
            "total_files": device_data.get("total_files", 0),
            "upload_queue_size": device_data.get("upload_queue_size", 0)
        }
    except Exception as e:
        logger.error(f"Device files error: {e}")
        raise HTTPException(status_code=503, detail=f"Device files unavailable: {str(e)}")

@app.post("/api/device/clear-files")
async def clear_device_files():
    """Clear files on the connected ESP32 device"""
    try:
        device_url = get_primary_device()
        if not device_url:
            raise HTTPException(status_code=503, detail="No edge device connected")
        
        # Send clear command to device
        response = requests.post(
            f"{device_url}/command",
            json={"command": "clear_sd"},
            timeout=30
        )
        response.raise_for_status()
        
        result = response.json()
        
        return {
            "success": result.get("success", False),
            "message": result.get("message", "Unknown result"),
            "device_url": device_url
        }
    except Exception as e:
        logger.error(f"Device clear files error: {e}")
        raise HTTPException(status_code=503, detail=f"Device clear failed: {str(e)}")


if __name__ == "__main__":
    # Create necessary directories using absolute paths
    os.makedirs(BASE_DIR / "uploads", exist_ok=True)
    os.makedirs(BASE_DIR / "uploads" / "images", exist_ok=True)
    os.makedirs(BASE_DIR / "static", exist_ok=True)
    os.makedirs(BASE_DIR / "templates", exist_ok=True)
    
    # Create placeholder image if it doesn't exist
    placeholder_path = BASE_DIR / "static" / "placeholder-camera.png"
    if not placeholder_path.exists():
        # Create a simple placeholder (you can replace with actual image)
        with open(placeholder_path, "w") as f:
            f.write("Placeholder for camera image")
    
    # Load saved device configuration
    load_device_config()
    if configured_device:
        logger.info(f"Using configured device: {configured_device['ip']}:{configured_device.get('port', 80)}")
    else:
        logger.info("No device configured. Please configure a device through the web interface.")
    
    uvicorn.run(app, host="0.0.0.0", port=8000)
