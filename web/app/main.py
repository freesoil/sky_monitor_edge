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

app = FastAPI(title="Edge Monitor Web Server")

# Mount static files and templates
app.mount("/static", StaticFiles(directory="static"), name="static")
templates = Jinja2Templates(directory="templates")

# Global variables for device management
connected_devices: Dict[str, Dict[str, Any]] = {}
last_device_discovery = 0
DISCOVERY_INTERVAL = 30  # seconds

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

# Device discovery and management
def discover_edge_devices():
    """Discover edge monitoring devices on the network"""
    global last_device_discovery, connected_devices
    
    current_time = time.time()
    if current_time - last_device_discovery < DISCOVERY_INTERVAL:
        return list(connected_devices.keys())
    
    logger.info("Discovering edge devices...")
    discovered = []
    
    # Try common IP ranges (adjust based on your network)
    import ipaddress
    import socket
    
    try:
        # Get local IP to determine subnet
        hostname = socket.gethostname()
        local_ip = socket.gethostbyname(hostname)
        network = ipaddress.IPv4Network(f"{local_ip}/24", strict=False)
        
        # Common ports for ESP32 devices
        test_ports = [80, 8080, 81]
        
        for ip in list(network.hosts())[:50]:  # Test first 50 IPs
            ip_str = str(ip)
            for port in test_ports:
                try:
                    # Try to connect to device status endpoint
                    response = requests.get(f"http://{ip_str}:{port}/status", timeout=1)
                    if response.status_code == 200:
                        data = response.json()
                        if data.get("device_type") == "edge_monitor":
                            device_info = {
                                "ip": ip_str,
                                "port": port,
                                "last_seen": current_time,
                                "info": data
                            }
                            connected_devices[f"{ip_str}:{port}"] = device_info
                            discovered.append(f"{ip_str}:{port}")
                            logger.info(f"Discovered edge device at {ip_str}:{port}")
                            break
                except:
                    continue
    except Exception as e:
        logger.error(f"Device discovery error: {e}")
    
    last_device_discovery = current_time
    return discovered

def get_primary_device():
    """Get the primary edge device IP"""
    devices = discover_edge_devices()
    if devices:
        device_key = devices[0]
        device_info = connected_devices[device_key]
        return f"http://{device_info['ip']}:{device_info['port']}"
    return None

def make_device_request(endpoint: str, method: str = "GET", data: dict = None):
    """Make a request to the edge device"""
    device_url = get_primary_device()
    if not device_url:
        raise HTTPException(status_code=503, detail="No edge device connected")
    
    try:
        url = f"{device_url}{endpoint}"
        if method == "GET":
            response = requests.get(url, timeout=10)
        elif method == "POST":
            response = requests.post(url, json=data, timeout=10)
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
    return templates.TemplateResponse("camera_control.html", {"request": request})

@app.get("/camera-control", response_class=HTMLResponse)
async def camera_control(request: Request):
    return templates.TemplateResponse("camera_control.html", {"request": request})

# API endpoints for camera control
@app.post("/api/camera/discover")
async def discover_camera():
    """Discover edge monitoring devices"""
    try:
        devices = discover_edge_devices()
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
        placeholder_path = "static/placeholder-camera.png"
        if os.path.exists(placeholder_path):
            with open(placeholder_path, "rb") as f:
                return StreamingResponse(
                    io.BytesIO(f.read()),
                    media_type="image/png"
                )
        raise HTTPException(status_code=503, detail="Camera stream unavailable")

# Endpoint for receiving images from edge devices
@app.post("/api/upload-image")
async def upload_image(file: UploadFile = File(...)):
    """Receive images from edge devices"""
    try:
        # Create uploads directory if it doesn't exist
        upload_dir = "uploads/images"
        os.makedirs(upload_dir, exist_ok=True)
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{timestamp}_{file.filename}"
        file_path = os.path.join(upload_dir, filename)
        
        # Save the file
        with open(file_path, "wb") as buffer:
            content = await file.read()
            buffer.write(content)
        
        logger.info(f"Received image: {filename} ({len(content)} bytes)")
        
        return {
            "success": True,
            "filename": filename,
            "size": len(content),
            "timestamp": timestamp
        }
    except Exception as e:
        logger.error(f"Image upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# Original upload endpoint (for videos)
@app.post("/upload")
async def upload_file(file: UploadFile = File(...)):
    """Upload endpoint for video files"""
    try:
        # Create uploads directory if it doesn't exist
        upload_dir = "uploads"
        os.makedirs(upload_dir, exist_ok=True)
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        file_extension = os.path.splitext(file.filename)[1] if file.filename else ""
        filename = f"{timestamp}{file_extension}"
        file_path = os.path.join(upload_dir, filename)
        
        # Save the file
        with open(file_path, "wb") as buffer:
            content = await file.read()
            buffer.write(content)
        
        logger.info(f"File uploaded: {filename} ({len(content)} bytes)")
        
        return {
            "message": "Upload successful",
            "filename": filename,
            "size": len(content)
        }
    except Exception as e:
        logger.error(f"Upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

if __name__ == "__main__":
    # Create necessary directories
    os.makedirs("uploads", exist_ok=True)
    os.makedirs("uploads/images", exist_ok=True)
    os.makedirs("static", exist_ok=True)
    os.makedirs("templates", exist_ok=True)
    
    # Create placeholder image if it doesn't exist
    placeholder_path = "static/placeholder-camera.png"
    if not os.path.exists(placeholder_path):
        # Create a simple placeholder (you can replace with actual image)
        with open(placeholder_path, "w") as f:
            f.write("Placeholder for camera image")
    
    uvicorn.run(app, host="0.0.0.0", port=8000)
