#!/usr/bin/env python3
"""
ESP32 Edge Monitor API Test Script with Timing Analysis

This script tests all the API endpoints of the ESP32 edge monitor device
and measures response times for performance analysis.

Usage:
    python3 test_edge_monitor_api.py [device_ip] [port]
    
Example:
    python3 test_edge_monitor_api.py 192.168.1.52 80
"""

import requests
import json
import time
import sys
import os
import statistics
from datetime import datetime
from typing import Dict, Any, Optional, Tuple, List

class EdgeMonitorAPITester:
    def __init__(self, device_ip: str = "192.168.1.52", port: int = 80):
        self.device_ip = device_ip
        self.port = port
        self.base_url = f"http://{device_ip}:{port}"
        self.session = requests.Session()
        self.session.timeout = 30  # Increased timeout for ESP32
        self.timing_results = {}  # Store timing data for analysis
        
    def log(self, message: str, level: str = "INFO"):
        """Log a message with timestamp"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] {level}: {message}")
        
    def log_with_timing(self, message: str, duration: float, level: str = "INFO"):
        """Log a message with timing information"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] {level}: {message} ({duration:.3f}s)")
        
    def make_timed_request(self, method: str, url: str, **kwargs) -> Tuple[Optional[requests.Response], float]:
        """Make a request and measure its duration"""
        start_time = time.time()
        try:
            if method.upper() == 'GET':
                response = self.session.get(url, **kwargs)
            elif method.upper() == 'POST':
                response = self.session.post(url, **kwargs)
            else:
                raise ValueError(f"Unsupported HTTP method: {method}")
            
            duration = time.time() - start_time
            return response, duration
        except requests.exceptions.RequestException as e:
            duration = time.time() - start_time
            self.log(f"Request failed after {duration:.3f}s: {e}", "ERROR")
            return None, duration
        
    def test_connectivity(self) -> Tuple[bool, float]:
        """Test basic connectivity to the device"""
        try:
            self.log(f"Testing connectivity to {self.base_url}")
            response, duration = self.make_timed_request('GET', f"{self.base_url}/", timeout=5)
            
            if response and response.status_code == 200:
                self.log_with_timing("✅ Device is reachable", duration)
                return True, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Device returned status {status_code}", duration, "ERROR")
                return False, duration
        except Exception as e:
            self.log(f"❌ Connection test error: {e}", "ERROR")
            return False, 0.0
    
    def test_status_endpoint(self, iterations: int = 3) -> Tuple[Optional[Dict[str, Any]], List[float]]:
        """Test the /status endpoint with timing analysis"""
        self.log(f"Testing /status endpoint ({iterations} iterations)...")
        durations = []
        
        for i in range(iterations):
            try:
                response, duration = self.make_timed_request('GET', f"{self.base_url}/status")
                durations.append(duration)
                
                if response and response.status_code == 200:
                    data = response.json()
                    if i == 0:  # Only log details for first iteration
                        self.log_with_timing("✅ Status endpoint working", duration)
                        self.log(f"   Device Type: {data.get('device_type', 'Unknown')}")
                        self.log(f"   WiFi Connected: {data.get('wifi_connected', False)}")
                        self.log(f"   Camera Ready: {data.get('camera_ready', False)}")
                        self.log(f"   SD Ready: {data.get('sd_ready', False)}")
                        self.log(f"   Free Heap: {data.get('free_heap', 0)} bytes")
                        self.log(f"   Uptime: {data.get('uptime', 0)} ms")
                        self.log(f"   File Count: {data.get('file_count', 0)}")
                        self.log(f"   Storage Used: {data.get('storage_used', 0)} MB")
                    else:
                        self.log_with_timing(f"   Iteration {i+1}", duration)
                else:
                    status_code = response.status_code if response else "No response"
                    self.log_with_timing(f"❌ Status endpoint failed: {status_code}", duration, "ERROR")
                    return None, durations
                    
                time.sleep(0.5)  # Brief pause between requests
                
            except Exception as e:
                self.log_with_timing(f"❌ Status endpoint error: {e}", duration, "ERROR")
                return None, durations
        
        # Calculate timing statistics
        if durations:
            avg_duration = statistics.mean(durations)
            min_duration = min(durations)
            max_duration = max(durations)
            self.log(f"   📊 Status timing: avg={avg_duration:.3f}s, min={min_duration:.3f}s, max={max_duration:.3f}s")
            self.timing_results['status'] = {
                'durations': durations,
                'avg': avg_duration,
                'min': min_duration,
                'max': max_duration
            }
        
        return data if 'data' in locals() else None, durations
    
    def test_capture_endpoint(self, iterations: int = 3) -> Tuple[bool, List[float]]:
        """Test the /capture endpoint with timing analysis"""
        self.log(f"Testing /capture endpoint ({iterations} iterations)...")
        durations = []
        success_count = 0
        
        for i in range(iterations):
            try:
                response, duration = self.make_timed_request('GET', f"{self.base_url}/capture")
                durations.append(duration)
                
                if response and response.status_code == 200:
                    content_type = response.headers.get('content-type', '')
                    if 'image/jpeg' in content_type:
                        self.log_with_timing(f"   ✅ Image {i+1}: {len(response.content)} bytes", duration)
                        success_count += 1
                    else:
                        self.log_with_timing(f"   ❌ Image {i+1} wrong content type: {content_type}", duration, "ERROR")
                else:
                    status_code = response.status_code if response else "No response"
                    self.log_with_timing(f"   ❌ Image {i+1} failed: {status_code}", duration, "ERROR")
                
                time.sleep(1)  # Wait between captures
                
            except Exception as e:
                self.log_with_timing(f"   ❌ Image {i+1} error: {e}", duration, "ERROR")
        
        # Calculate timing statistics
        if durations:
            avg_duration = statistics.mean(durations)
            min_duration = min(durations)
            max_duration = max(durations)
            self.log(f"   📊 Capture timing: avg={avg_duration:.3f}s, min={min_duration:.3f}s, max={max_duration:.3f}s")
            self.timing_results['capture'] = {
                'durations': durations,
                'avg': avg_duration,
                'min': min_duration,
                'max': max_duration
            }
        
        success = success_count == iterations
        if success:
            self.log("✅ All capture tests passed")
        else:
            self.log(f"❌ Only {success_count}/{iterations} capture tests succeeded", "ERROR")
        
        return success, durations
    
    def test_control_endpoint(self) -> Tuple[bool, List[float]]:
        """Test the /control endpoint with various camera settings and timing"""
        self.log("Testing /control endpoint...")
        
        # Test different camera settings
        test_settings = [
            {"var": "quality", "val": 10},
            {"var": "brightness", "val": 1},
            {"var": "contrast", "val": 0},
            {"var": "saturation", "val": -1},
            {"var": "framesize", "val": 1}  # QVGA
        ]
        
        durations = []
        success_count = 0
        
        for setting in test_settings:
            try:
                self.log(f"   Testing {setting['var']} = {setting['val']}")
                response, duration = self.make_timed_request(
                    'POST', 
                    f"{self.base_url}/control",
                    json=setting,
                    headers={'Content-Type': 'application/json'}
                )
                durations.append(duration)
                
                if response and response.status_code == 200:
                    data = response.json()
                    if data.get('success', False):
                        self.log_with_timing(f"   ✅ {setting['var']} updated successfully", duration)
                        success_count += 1
                    else:
                        self.log_with_timing(f"   ❌ {setting['var']} update failed: {data.get('message', 'Unknown error')}", duration, "ERROR")
                else:
                    status_code = response.status_code if response else "No response"
                    self.log_with_timing(f"   ❌ {setting['var']} request failed: {status_code}", duration, "ERROR")
                    
                time.sleep(1)  # Give device time to process
                
            except Exception as e:
                self.log_with_timing(f"   ❌ {setting['var']} error: {e}", duration, "ERROR")
        
        # Calculate timing statistics
        if durations:
            avg_duration = statistics.mean(durations)
            min_duration = min(durations)
            max_duration = max(durations)
            self.log(f"   📊 Control timing: avg={avg_duration:.3f}s, min={min_duration:.3f}s, max={max_duration:.3f}s")
            self.timing_results['control'] = {
                'durations': durations,
                'avg': avg_duration,
                'min': min_duration,
                'max': max_duration
            }
        
        success = success_count == len(test_settings)
        if success:
            self.log("✅ All control settings updated successfully")
        else:
            self.log(f"❌ Only {success_count}/{len(test_settings)} control settings succeeded", "ERROR")
        
        return success, durations
    
    def test_files_endpoint(self) -> Tuple[Optional[Dict[str, Any]], float]:
        """Test the /files endpoint"""
        self.log("Testing /files endpoint...")
        
        try:
            response, duration = self.make_timed_request('GET', f"{self.base_url}/files")
            
            if response and response.status_code == 200:
                data = response.json()
                self.log_with_timing(f"✅ Files endpoint working", duration)
                self.log(f"   Total files: {data.get('total_files', 0)}")
                self.log(f"   Upload queue: {data.get('upload_queue_size', 0)}")
                
                files = data.get('files', [])
                if files:
                    self.log(f"   Files on SD:")
                    for i, f in enumerate(files[:5]):
                        self.log(f"     {i+1}. {f['name']} ({f['size']} bytes)")
                    if len(files) > 5:
                        self.log(f"     ... and {len(files) - 5} more")
                else:
                    self.log("   ⚠️  No files found on SD card")
                
                self.timing_results['files'] = {'duration': duration}
                return data, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Files endpoint failed: {status_code}", duration, "ERROR")
                return None, duration
        except Exception as e:
            self.log(f"❌ Files endpoint error: {e}", "ERROR")
            return None, 0.0
    
    def test_command_endpoint(self) -> Tuple[bool, List[float]]:
        """Test the /command endpoint with various commands and timing"""
        self.log("Testing /command endpoint...")
        
        # Test safe commands (avoid restart and clear_sd for now)
        test_commands = [
            {"command": "start"},
            {"command": "stop"},
            {"command": "pause"},
            {"command": "photo"},
            {"command": "list_files"},
            {"command": "test_sd"}
        ]
        
        durations = []
        success_count = 0
        
        for cmd in test_commands:
            try:
                self.log(f"   Testing command: {cmd['command']}")
                response, duration = self.make_timed_request(
                    'POST', 
                    f"{self.base_url}/command",
                    json=cmd,
                    headers={'Content-Type': 'application/json'}
                )
                durations.append(duration)
                
                if response and response.status_code == 200:
                    data = response.json()
                    if data.get('success', False):
                        self.log_with_timing(f"   ✅ Command '{cmd['command']}' executed: {data.get('message', '')}", duration)
                        success_count += 1
                    else:
                        self.log_with_timing(f"   ❌ Command '{cmd['command']}' failed: {data.get('message', 'Unknown error')}", duration, "ERROR")
                else:
                    status_code = response.status_code if response else "No response"
                    self.log_with_timing(f"   ❌ Command '{cmd['command']}' request failed: {status_code}", duration, "ERROR")
                    
                time.sleep(2)  # Give device time to process
                
            except Exception as e:
                self.log_with_timing(f"   ❌ Command '{cmd['command']}' error: {e}", duration, "ERROR")
        
        # Calculate timing statistics
        if durations:
            avg_duration = statistics.mean(durations)
            min_duration = min(durations)
            max_duration = max(durations)
            self.log(f"   📊 Command timing: avg={avg_duration:.3f}s, min={min_duration:.3f}s, max={max_duration:.3f}s")
            self.timing_results['command'] = {
                'durations': durations,
                'avg': avg_duration,
                'min': min_duration,
                'max': max_duration
            }
        
        success = success_count == len(test_commands)
        if success:
            self.log("✅ All commands executed successfully")
        else:
            self.log(f"❌ Only {success_count}/{len(test_commands)} commands succeeded", "ERROR")
        
        return success, durations
    
    def test_recording_config_endpoint(self) -> Tuple[bool, List[float]]:
        """Test the /recording-config endpoint with timing"""
        self.log("Testing /recording-config endpoint...")
        
        test_configs = [
            {"setting": "interval", "value": 30},
            {"setting": "duration", "value": 15},
            {"setting": "stream_interval", "value": 5}
        ]
        
        durations = []
        success_count = 0
        
        for config in test_configs:
            try:
                self.log(f"   Testing {config['setting']} = {config['value']} seconds")
                response, duration = self.make_timed_request(
                    'POST', 
                    f"{self.base_url}/recording-config",
                    json=config,
                    headers={'Content-Type': 'application/json'}
                )
                durations.append(duration)
                
                if response and response.status_code == 200:
                    data = response.json()
                    if data.get('success', False):
                        self.log_with_timing(f"   ✅ {config['setting']} updated successfully", duration)
                        success_count += 1
                    else:
                        self.log_with_timing(f"   ❌ {config['setting']} update failed: {data.get('message', 'Unknown error')}", duration, "ERROR")
                else:
                    status_code = response.status_code if response else "No response"
                    self.log_with_timing(f"   ❌ {config['setting']} request failed: {status_code}", duration, "ERROR")
                    
                time.sleep(1)
                
            except Exception as e:
                self.log_with_timing(f"   ❌ {config['setting']} error: {e}", duration, "ERROR")
        
        # Calculate timing statistics
        if durations:
            avg_duration = statistics.mean(durations)
            min_duration = min(durations)
            max_duration = max(durations)
            self.log(f"   📊 Recording config timing: avg={avg_duration:.3f}s, min={min_duration:.3f}s, max={max_duration:.3f}s")
            self.timing_results['recording_config'] = {
                'durations': durations,
                'avg': avg_duration,
                'min': min_duration,
                'max': max_duration
            }
        
        success = success_count == len(test_configs)
        if success:
            self.log("✅ All recording configs updated successfully")
        else:
            self.log(f"❌ Only {success_count}/{len(test_configs)} recording configs succeeded", "ERROR")
        
        return success, durations
    
    def test_apply_settings_endpoint(self) -> Tuple[bool, float]:
        """Test the /apply-settings endpoint with timing"""
        try:
            self.log("Testing /apply-settings endpoint...")
            
            # Test bulk settings update
            bulk_settings = {
                "framesize": 1,  # QVGA
                "quality": 15,
                "brightness": 0,
                "contrast": 1,
                "saturation": 0,
                "capture_interval": 60,
                "capture_duration": 10,
                "stream_interval": 5
            }
            
            response, duration = self.make_timed_request(
                'POST', 
                f"{self.base_url}/apply-settings",
                json=bulk_settings,
                headers={'Content-Type': 'application/json'}
            )
            
            if response and response.status_code == 200:
                data = response.json()
                if data.get('success', False):
                    self.log_with_timing("✅ Bulk settings applied successfully", duration)
                    self.timing_results['apply_settings'] = {'duration': duration}
                    return True, duration
                else:
                    self.log_with_timing(f"❌ Bulk settings failed: {data.get('message', 'Unknown error')}", duration, "ERROR")
                    return False, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Apply settings request failed: {status_code}", duration, "ERROR")
                return False, duration
                
        except Exception as e:
            self.log_with_timing(f"❌ Apply settings error: {e}", duration, "ERROR")
            return False, duration
    
    def print_timing_summary(self):
        """Print a comprehensive timing summary"""
        self.log("=" * 80)
        self.log("TIMING ANALYSIS SUMMARY")
        self.log("=" * 80)
        
        if not self.timing_results:
            self.log("No timing data available")
            return
        
        # Sort endpoints by average response time
        sorted_endpoints = sorted(
            self.timing_results.items(),
            key=lambda x: x[1].get('avg', x[1].get('duration', 0))
        )
        
        self.log(f"{'Endpoint':<20} {'Avg (s)':<10} {'Min (s)':<10} {'Max (s)':<10} {'Status'}")
        self.log("-" * 80)
        
        for endpoint, data in sorted_endpoints:
            if 'avg' in data:  # Multiple iterations
                avg = data['avg']
                min_time = data['min']
                max_time = data['max']
                status = "📊 Multiple samples"
            else:  # Single iteration
                avg = data['duration']
                min_time = max_time = avg
                status = "📝 Single sample"
            
            self.log(f"{endpoint:<20} {avg:<10.3f} {min_time:<10.3f} {max_time:<10.3f} {status}")
        
        # Performance analysis
        self.log("-" * 80)
        self.log("PERFORMANCE ANALYSIS:")
        
        fastest_endpoint = min(sorted_endpoints, key=lambda x: x[1].get('avg', x[1].get('duration', 0)))
        slowest_endpoint = max(sorted_endpoints, key=lambda x: x[1].get('avg', x[1].get('duration', 0)))
        
        self.log(f"🚀 Fastest endpoint: {fastest_endpoint[0]} ({fastest_endpoint[1].get('avg', fastest_endpoint[1].get('duration', 0)):.3f}s)")
        self.log(f"🐌 Slowest endpoint: {slowest_endpoint[0]} ({slowest_endpoint[1].get('avg', slowest_endpoint[1].get('duration', 0)):.3f}s)")
        
        # Performance recommendations
        self.log("-" * 80)
        self.log("RECOMMENDATIONS:")
        
        for endpoint, data in self.timing_results.items():
            avg_time = data.get('avg', data.get('duration', 0))
            if avg_time > 5.0:
                self.log(f"⚠️  {endpoint}: Very slow ({avg_time:.3f}s) - Consider optimization")
            elif avg_time > 2.0:
                self.log(f"⚡ {endpoint}: Slow ({avg_time:.3f}s) - Monitor performance")
            else:
                self.log(f"✅ {endpoint}: Good performance ({avg_time:.3f}s)")
    
    def test_clear_sd_command(self) -> Tuple[bool, float]:
        """Test the clear_sd command (WARNING: Destructive!)"""
        self.log("Testing clear_sd command...")
        self.log("⚠️  WARNING: This will delete all files on SD card!")
        
        # Ask for confirmation
        try:
            user_input = input("   Do you want to proceed? (yes/no): ").strip().lower()
            if user_input != 'yes':
                self.log("   Skipped clear_sd test")
                return True, 0.0  # Consider it passed (user choice)
        except:
            self.log("   Skipped clear_sd test (no user input)")
            return True, 0.0
        
        try:
            response, duration = self.make_timed_request(
                'POST', 
                f"{self.base_url}/command",
                json={"command": "clear_sd"},
                headers={'Content-Type': 'application/json'}
            )
            
            if response and response.status_code == 200:
                data = response.json()
                if data.get('success', False):
                    self.log_with_timing(f"✅ SD card cleared: {data.get('message', '')}", duration)
                    self.timing_results['clear_sd'] = {'duration': duration}
                    return True, duration
                else:
                    self.log_with_timing(f"❌ Clear SD failed: {data.get('message', 'Unknown error')}", duration, "ERROR")
                    return False, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Clear SD request failed: {status_code}", duration, "ERROR")
                return False, duration
        except Exception as e:
            self.log(f"❌ Clear SD error: {e}", "ERROR")
            return False, 0.0
    
    def test_server_files_endpoint(self) -> Tuple[Optional[Dict[str, Any]], float]:
        """Test the server files endpoint"""
        self.log("Testing server files endpoint...")
        
        try:
            response, duration = self.make_timed_request('GET', f"http://localhost:8000/api/files")
            
            if response and response.status_code == 200:
                data = response.json()
                self.log_with_timing(f"✅ Server files endpoint working", duration)
                self.log(f"   📁 Total files: {data.get('total_files', 0)}")
                self.log(f"   💾 Total size: {data.get('total_size_mb', 0)} MB")
                
                files = data.get('files', [])
                if files:
                    self.log(f"   📋 Recent server files:")
                    for i, f in enumerate(files[:3]):
                        self.log(f"     {i+1}. {f['name']} ({f['size']} bytes, {f['type']})")
                    if len(files) > 3:
                        self.log(f"     ... and {len(files) - 3} more")
                else:
                    self.log("   ⚠️  No files found on server")
                
                self.timing_results['server_files'] = {'duration': duration}
                return data, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Server files endpoint failed: {status_code}", duration, "ERROR")
                return None, duration
        except Exception as e:
            self.log(f"❌ Server files endpoint error: {e}", "ERROR")
            return None, 0.0

    def test_server_device_files_proxy(self) -> Tuple[Optional[Dict[str, Any]], float]:
        """Test the server proxy to device files"""
        self.log("Testing server proxy to device files...")
        
        try:
            response, duration = self.make_timed_request('GET', f"http://localhost:8000/api/device/files")
            
            if response and response.status_code == 200:
                data = response.json()
                self.log_with_timing(f"✅ Server proxy to device works", duration)
                self.log(f"   📱 Device: {data.get('device_url', 'Unknown')}")
                self.log(f"   📁 Total files: {data.get('total_files', 0)}")
                self.log(f"   📤 Upload queue: {data.get('upload_queue_size', 0)}")
                
                files = data.get('files', [])
                if files:
                    self.log(f"   📋 Device files:")
                    for i, f in enumerate(files[:3]):
                        self.log(f"     {i+1}. {f['name']} ({f['size']} bytes)")
                    if len(files) > 3:
                        self.log(f"     ... and {len(files) - 3} more")
                else:
                    self.log("   ℹ️  No files found on device SD card")
                
                self.timing_results['server_device_proxy'] = {'duration': duration}
                return data, duration
            elif response and response.status_code == 503:
                self.log_with_timing(f"⚠️  No device connected: {response.json().get('detail', 'Unknown')}", duration, "WARNING")
                return None, duration
            else:
                status_code = response.status_code if response else "No response"
                self.log_with_timing(f"❌ Server proxy failed: {status_code}", duration, "ERROR")
                return None, duration
        except Exception as e:
            self.log(f"❌ Server proxy error: {e}", "ERROR")
            return None, 0.0

    def test_complete_file_pipeline(self) -> Dict[str, Any]:
        """Test the complete file management pipeline"""
        self.log("=" * 70)
        self.log("COMPLETE FILE MANAGEMENT PIPELINE TEST")
        self.log("=" * 70)
        
        pipeline_results = {}
        
        # Test 1: Server file listing
        self.log("\n[1] Server Files (via Web API)")
        self.log("-" * 70)
        server_data, server_time = self.test_server_files_endpoint()
        pipeline_results["server_files"] = server_data is not None
        
        # Test 2: Device file listing (via server proxy)
        self.log("\n[2] Device Files (via Server Proxy API)")
        self.log("-" * 70)
        proxy_data, proxy_time = self.test_server_device_files_proxy()
        pipeline_results["server_proxy"] = proxy_data is not None
        
        # Test 3: Direct device file listing
        self.log("\n[3] Device Files (Direct ESP32 API)")
        self.log("-" * 70)
        device_data, device_time = self.test_files_endpoint()
        pipeline_results["direct_device"] = device_data is not None
        
        # Test 4: Device status
        self.log("\n[4] Device Status")
        self.log("-" * 70)
        try:
            response, duration = self.make_timed_request('GET', f"{self.base_url}/status")
            if response and response.status_code == 200:
                data = response.json()
                self.log_with_timing(f"✅ Device status API works", duration)
                self.log(f"   📹 Camera: {'OK' if data.get('camera_ready') else 'FAILED'}")
                self.log(f"   💾 SD Card: {'OK' if data.get('sd_ready') else 'FAILED'}")
                self.log(f"   📡 WiFi: {'Connected' if data.get('wifi_connected') else 'Disconnected'}")
                self.log(f"   🎥 Recording: {'YES' if data.get('is_recording') else 'NO'}")
                self.log(f"   📁 File count: {data.get('file_count', 0)}")
                self.log(f"   💾 Storage used: {data.get('storage_used', 0)} MB")
                pipeline_results["device_status"] = True
            else:
                self.log(f"❌ Device status failed: {response.status_code if response else 'No response'}")
                pipeline_results["device_status"] = False
        except Exception as e:
            self.log(f"❌ Device status error: {e}")
            pipeline_results["device_status"] = False
        
        # Test 5: Command test (list_files)
        self.log("\n[5] Device Command Test (list_files)")
        self.log("-" * 70)
        try:
            response, duration = self.make_timed_request(
                'POST', 
                f"{self.base_url}/command",
                json={"command": "list_files"},
                headers={'Content-Type': 'application/json'}
            )
            if response and response.status_code == 200:
                result = response.json()
                self.log_with_timing(f"✅ Command executed: {result.get('message', 'OK')}", duration)
                self.log(f"   ℹ️  Check device Serial Monitor for file listing")
                pipeline_results["list_command"] = True
            else:
                self.log(f"❌ Command failed: {response.status_code if response else 'No response'}")
                pipeline_results["list_command"] = False
        except Exception as e:
            self.log(f"❌ Command error: {e}")
            pipeline_results["list_command"] = False
        
        # Summary
        self.log("\n" + "=" * 70)
        self.log("PIPELINE TEST COMPLETE")
        self.log("=" * 70)
        
        passed = sum(pipeline_results.values())
        total = len(pipeline_results)
        
        self.log(f"\n📝 Pipeline Summary:")
        for test_name, result in pipeline_results.items():
            status = "✅ PASS" if result else "❌ FAIL"
            self.log(f"   {test_name.upper():<20} {status}")
        
        self.log(f"\nTOTAL: {passed}/{total} pipeline tests passed")
        
        if passed == total:
            self.log("\n🎉 Complete file management pipeline is working!")
        else:
            self.log(f"\n⚠️  {total - passed} pipeline tests failed. Check connections.")
        
        self.log("\n💡 Next Steps:")
        self.log("   1. Flash the updated edge_monitor.ino to ESP32")
        self.log("   2. Record some videos to test file listing")
        self.log("   3. Test clear operations (destructive!)")
        self.log("   4. Use Web UI at http://localhost:8000/system-status")
        
        return pipeline_results

    def run_all_tests(self) -> Dict[str, Any]:
        """Run all API tests and return results with timing data"""
        start_time = time.time()
        self.log("=" * 80)
        self.log("ESP32 Edge Monitor API Test Suite with Timing Analysis")
        self.log("=" * 80)
        
        results = {}
        
        # Test connectivity first
        connectivity_success, connectivity_time = self.test_connectivity()
        if not connectivity_success:
            self.log("❌ Cannot connect to device. Aborting tests.", "ERROR")
            return {"connectivity": False, "total_time": time.time() - start_time}
        
        results["connectivity"] = True
        self.timing_results['connectivity'] = {'duration': connectivity_time}
        
        # Run all tests
        status_data, status_times = self.test_status_endpoint()
        results["status"] = status_data is not None
        
        # Test files endpoint
        files_data, files_time = self.test_files_endpoint()
        results["files"] = files_data is not None
        
        results["capture"], capture_times = self.test_capture_endpoint()
        results["control"], control_times = self.test_control_endpoint()
        results["command"], command_times = self.test_command_endpoint()
        results["recording_config"], recording_times = self.test_recording_config_endpoint()
        results["apply_settings"], apply_time = self.test_apply_settings_endpoint()
        
        # Optionally test clear_sd (destructive)
        results["clear_sd"], clear_time = self.test_clear_sd_command()
        
        # Test complete file management pipeline
        self.log("\n" + "=" * 80)
        self.log("RUNNING COMPLETE FILE MANAGEMENT PIPELINE TEST")
        self.log("=" * 80)
        pipeline_results = self.test_complete_file_pipeline()
        results.update(pipeline_results)
        
        # Summary
        total_time = time.time() - start_time
        self.log("\n" + "=" * 80)
        self.log("FINAL TEST SUMMARY")
        self.log("=" * 80)
        
        passed = sum(results.values())
        total = len(results)
        
        for test_name, result in results.items():
            status = "✅ PASS" if result else "❌ FAIL"
            self.log(f"{test_name.upper():<20} {status}")
        
        self.log("-" * 80)
        self.log(f"TOTAL: {passed}/{total} tests passed")
        self.log(f"TOTAL TEST TIME: {total_time:.3f} seconds")
        
        # Print timing analysis
        self.print_timing_summary()
        
        if passed == total:
            self.log("🎉 All tests passed! Device API is working correctly.")
        else:
            self.log(f"⚠️  {total - passed} tests failed. Check device status.")
        
        results["total_time"] = total_time
        results["timing_data"] = self.timing_results
        return results

def main():
    """Main function"""
    # Parse command line arguments
    device_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.52"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 80
    
    print(f"Testing ESP32 Edge Monitor at {device_ip}:{port}")
    print(f"Started at {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print()
    
    # Create tester and run tests
    tester = EdgeMonitorAPITester(device_ip, port)
    results = tester.run_all_tests()
    
    # Exit with appropriate code
    if all(results.get(k, False) for k in results.keys() if k not in ['total_time', 'timing_data']):
        sys.exit(0)  # All tests passed
    else:
        sys.exit(1)  # Some tests failed

if __name__ == "__main__":
    main()
