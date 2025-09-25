#!/usr/bin/env python3
"""
Video Frames Converter

Bidirectional converter:
1. Convert AVI video file to individual frame images
2. Convert frame images back to MP4 video

Each frame is saved as 'frame_<number>.<format>' in the specified output directory.
"""

import argparse
import cv2
import os
import sys
import re
import glob
from pathlib import Path


def validate_video_file(video_path):
    """
    Validate that the video file is readable and contains actual video data.
    
    Args:
        video_path (str): Path to the video file
        
    Returns:
        tuple: (is_valid, info_message)
    """
    try:
        cap = cv2.VideoCapture(video_path)
        
        if not cap.isOpened():
            return False, "Could not open video file"
        
        # Try to read the first frame
        ret, frame = cap.read()
        cap.release()
        
        if not ret or frame is None:
            return False, "Video file contains no readable frames"
        
        return True, "Video file appears valid"
        
    except Exception as e:
        return False, f"Error validating video: {str(e)}"


def extract_frames(video_path, output_dir, image_format='jpg', start_frame=0, end_frame=None, step=1):
    """
    Extract frames from a video file and save them as images.
    
    Args:
        video_path (str): Path to the input video file
        output_dir (str): Directory to save the extracted frames
        image_format (str): Output image format (jpg, png, bmp, etc.)
        start_frame (int): Starting frame number (0-based)
        end_frame (int): Ending frame number (None for all frames)
        step (int): Frame step (1 = every frame, 2 = every other frame, etc.)
    
    Returns:
        int: Number of frames extracted
    """
    
    # Open the video file
    cap = cv2.VideoCapture(video_path)
    
    if not cap.isOpened():
        raise ValueError(f"Error: Could not open video file '{video_path}'")
    
    # Get video properties
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    
    # Check for invalid frame count (common with ESP32 AVI files)
    if total_frames <= 0 or total_frames > 1000000:  # Sanity check
        print(f"⚠️  Warning: Invalid frame count detected ({total_frames})")
        print("   This often happens with ESP32 AVI files or corrupted videos.")
        print("   Will attempt to process frame-by-frame until end of video.")
        total_frames = -1  # Use -1 as unknown
        use_frame_by_frame = True
    else:
        use_frame_by_frame = False
    
    print(f"Video Information:")
    if total_frames > 0:
        print(f"  Total frames: {total_frames}")
        print(f"  Duration: {total_frames/fps:.2f} seconds")
    else:
        print(f"  Total frames: Unknown (will detect dynamically)")
        print(f"  Duration: Unknown")
    print(f"  FPS: {fps:.2f}")
    print(f"  Resolution: {width}x{height}")
    print()
    
    # Handle frame range validation for unknown total frames
    if use_frame_by_frame:
        print("ℹ️  Frame count unknown - ignoring end_frame parameter, processing all frames")
        effective_end_frame = float('inf')  # Process until video ends
        
        # Validate start frame
        if start_frame < 0:
            start_frame = 0
    else:
        # Set end_frame if not specified
        if end_frame is None:
            effective_end_frame = total_frames - 1
        else:
            effective_end_frame = min(end_frame, total_frames - 1)
        
        # Validate frame range
        if start_frame < 0:
            start_frame = 0
        if start_frame >= total_frames:
            raise ValueError(f"Start frame {start_frame} is beyond video length ({total_frames} frames)")
        if effective_end_frame < start_frame:
            raise ValueError(f"End frame {effective_end_frame} must be >= start frame {start_frame}")
    
    # Create output directory
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    if use_frame_by_frame:
        print(f"Extracting frames starting from {start_frame} (step={step}) until end of video")
    else:
        print(f"Extracting frames {start_frame} to {effective_end_frame} (step={step})")
    print(f"Output directory: {output_path.absolute()}")
    print(f"Image format: {image_format.upper()}")
    print()
    
    # Extract frames
    frame_count = 0
    saved_count = 0
    
    # Set starting position (skip if problematic)
    try:
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)
    except:
        print(f"⚠️  Warning: Could not seek to frame {start_frame}, starting from beginning")
        start_frame = 0
    
    # Skip to start frame manually if seeking failed
    if start_frame > 0:
        for i in range(start_frame):
            ret, _ = cap.read()
            if not ret:
                print(f"⚠️  Warning: Could only read {i} frames, starting from frame {i}")
                start_frame = i
                break
    
    while True:
        ret, frame = cap.read()
        
        if not ret:
            break
        
        # Get current frame position (or calculate manually if unreliable)
        try:
            current_frame = int(cap.get(cv2.CAP_PROP_POS_FRAMES)) - 1
        except:
            current_frame = start_frame + frame_count
        
        # Check if we've reached the end frame (only if not using frame-by-frame mode)
        if not use_frame_by_frame and current_frame > effective_end_frame:
            break
        
        # Check if this frame should be saved (based on step)
        if (current_frame - start_frame) % step == 0:
            # Create filename
            filename = f"frame_{current_frame:06d}.{image_format}"
            file_path = output_path / filename
            
            # Save the frame
            success = cv2.imwrite(str(file_path), frame)
            
            if success:
                saved_count += 1
                if saved_count % 10 == 0 or saved_count <= 10:
                    print(f"Saved frame {current_frame} -> {filename}")
            else:
                print(f"Warning: Failed to save frame {current_frame}")
        
        frame_count += 1
    
    # Clean up
    cap.release()
    
    print(f"\nExtraction complete!")
    print(f"Processed {frame_count} frames")
    print(f"Saved {saved_count} images to '{output_path}'")
    
    return saved_count


def frames_to_video(frames_dir, output_video, fps=25.0, frame_pattern="frame_*.jpg", codec='mp4v'):
    """
    Convert a directory of frame images to an MP4 video.
    
    Args:
        frames_dir (str): Directory containing frame images
        output_video (str): Path for output video file
        fps (float): Frames per second for output video
        frame_pattern (str): Glob pattern to match frame files
        codec (str): Video codec ('mp4v', 'XVID', 'H264', etc.)
    
    Returns:
        int: Number of frames processed
    """
    
    frames_path = Path(frames_dir)
    
    if not frames_path.exists():
        raise ValueError(f"Frames directory '{frames_dir}' does not exist")
    
    # Find all frame files matching the pattern
    frame_files = glob.glob(str(frames_path / frame_pattern))
    
    if not frame_files:
        raise ValueError(f"No frame files found matching pattern '{frame_pattern}' in '{frames_dir}'")
    
    # Sort files naturally (frame_1.jpg, frame_2.jpg, ..., frame_10.jpg)
    def natural_sort_key(filename):
        # Extract numbers from filename for proper sorting
        numbers = re.findall(r'\d+', os.path.basename(filename))
        return [int(num) for num in numbers] if numbers else [0]
    
    frame_files.sort(key=natural_sort_key)
    
    print(f"Found {len(frame_files)} frame files")
    print(f"First frame: {os.path.basename(frame_files[0])}")
    print(f"Last frame: {os.path.basename(frame_files[-1])}")
    print()
    
    # Read the first frame to get dimensions
    first_frame = cv2.imread(frame_files[0])
    if first_frame is None:
        raise ValueError(f"Could not read first frame: {frame_files[0]}")
    
    height, width, channels = first_frame.shape
    
    print(f"Video Properties:")
    print(f"  Output file: {output_video}")
    print(f"  Resolution: {width}x{height}")
    print(f"  FPS: {fps}")
    print(f"  Codec: {codec}")
    print(f"  Total frames: {len(frame_files)}")
    print(f"  Duration: {len(frame_files)/fps:.2f} seconds")
    print()
    
    # Create output directory if needed
    output_path = Path(output_video)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    # Initialize video writer
    fourcc = cv2.VideoWriter_fourcc(*codec)
    out = cv2.VideoWriter(output_video, fourcc, fps, (width, height))
    
    if not out.isOpened():
        raise ValueError(f"Could not initialize video writer with codec '{codec}'")
    
    print("Converting frames to video...")
    
    # Process each frame
    processed_count = 0
    
    for i, frame_file in enumerate(frame_files):
        # Read frame
        frame = cv2.imread(frame_file)
        
        if frame is None:
            print(f"⚠️  Warning: Could not read frame {frame_file}, skipping")
            continue
        
        # Check if frame dimensions match
        if frame.shape[:2] != (height, width):
            print(f"⚠️  Warning: Frame {frame_file} has different dimensions, resizing")
            frame = cv2.resize(frame, (width, height))
        
        # Write frame to video
        out.write(frame)
        processed_count += 1
        
        # Progress indicator
        if processed_count % 10 == 0 or processed_count <= 10:
            print(f"Processed frame {processed_count}/{len(frame_files)}: {os.path.basename(frame_file)}")
    
    # Clean up
    out.release()
    
    print(f"\nVideo creation complete!")
    print(f"Processed {processed_count} frames")
    print(f"Output video: {output_video}")
    
    # Verify output file
    if os.path.exists(output_video) and os.path.getsize(output_video) > 0:
        print(f"✅ Video file created successfully ({os.path.getsize(output_video)} bytes)")
    else:
        print("❌ Warning: Output video file is empty or missing")
    
    return processed_count


def main():
    parser = argparse.ArgumentParser(
        description="""
🎬 Video Frames Converter - Bidirectional video/frames processing tool

This tool provides two main functions:
1. EXTRACT frames from video files (AVI, MP4, etc.) into individual images
2. COMPOSE frame images back into MP4 video files

Especially useful for:
• Processing ESP32/Arduino camera recordings
• Creating time-lapse videos from image sequences  
• Frame-by-frame video analysis and editing
• Recovering frames from corrupted video files
• Converting between different video formats and frame rates
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
📚 EXAMPLES & USAGE SCENARIOS:

🔸 BASIC VIDEO TO FRAMES EXTRACTION:
  %(prog)s input.avi frames/
  %(prog)s corrupted_video.avi extracted/ --format png

🔸 SELECTIVE FRAME EXTRACTION:
  %(prog)s video.avi frames/ --start 100 --end 500 --step 2
  %(prog)s long_video.mp4 samples/ --step 10 --format jpg

🔸 ESP32 CAMERA TROUBLESHOOTING:
  %(prog)s esp32_video.avi frames/ --quiet
  # (Handles corrupted ESP32 AVI files gracefully)

🔸 BASIC FRAMES TO VIDEO COMPOSITION:
  %(prog)s frames/ output.mp4 --compose --fps 30
  %(prog)s images/ timelapse.mp4 --compose --fps 60

🔸 ADVANCED VIDEO COMPOSITION:
  %(prog)s frames/ high_quality.mp4 --compose --fps 25 --codec H264
  %(prog)s photos/ slideshow.mp4 --compose --fps 2 --pattern "*.jpg"

🔸 TIME-LAPSE & SLOW MOTION:
  %(prog)s frames/ fast.mp4 --compose --fps 60    # Speed up
  %(prog)s frames/ slow.mp4 --compose --fps 5     # Slow motion

🔸 DIFFERENT FRAME PATTERNS:
  %(prog)s imgs/ video.mp4 --compose --pattern "img_*.png" --fps 24
  %(prog)s data/ result.mp4 --compose --pattern "capture_*.jpeg" --fps 30

📋 WORKFLOW EXAMPLES:

1️⃣  ESP32 Video Recovery:
    %(prog)s corrupted.avi frames/              # Extract recoverable frames
    %(prog)s frames/ clean.mp4 --compose --fps 25  # Create clean MP4

2️⃣  Time-lapse Creation:
    %(prog)s timelapse_imgs/ fast.mp4 --compose --fps 30 --codec H264

3️⃣  Video Analysis:
    %(prog)s surveillance.mp4 frames/ --step 30    # Every 30th frame
    # Analyze frames manually
    %(prog)s selected_frames/ evidence.mp4 --compose --fps 1

4️⃣  Format Conversion:
    %(prog)s old_video.avi frames/ --format png    # Extract as PNG
    %(prog)s frames/ new_video.mp4 --compose --fps 25 --codec H264

💡 TIPS & TROUBLESHOOTING:

• For corrupted ESP32 videos: The tool automatically handles invalid frame counts
• For large videos: Use --step option to extract every Nth frame
• For high quality: Use PNG format for extraction and H264 codec for composition  
• For time-lapse: Increase FPS (30-60) for smooth motion
• For slow motion: Decrease FPS (5-15) for slow playback
• Use --quiet flag for batch processing scripts

🔧 CODEC COMPATIBILITY:
• mp4v: Best compatibility, larger files
• H264: High quality, modern standard, smaller files  
• XVID: Legacy compatibility
• MJPG: Fast encoding, larger files

📂 OUTPUT FILE PATTERNS:
• Frames: frame_000001.jpg, frame_000002.jpg, ...
• Videos: Standard MP4 format compatible with all players

⚠️  COMMON ESP32 AVI ISSUES HANDLED:
• Invalid frame count metadata
• Interrupted recordings  
• Power loss during recording
• SD card corruption
• Non-standard AVI headers
        """
    )
    
    # Required arguments
    parser.add_argument(
        'input',
        help='''Input path:
        • EXTRACTION mode: Path to video file (e.g., video.avi, recording.mp4)
        • COMPOSITION mode: Directory containing frame images (e.g., frames/, images/)'''
    )
    
    parser.add_argument(
        'output',
        help='''Output path:
        • EXTRACTION mode: Directory to save frame images (created if doesn't exist)
        • COMPOSITION mode: Output video file path (e.g., output.mp4, timelapse.mp4)'''
    )
    
    # Mode selection
    parser.add_argument(
        '--compose', '-c',
        action='store_true',
        help='''🎬 COMPOSITION MODE: Combine frame images into MP4 video
        (Default: EXTRACTION mode - extract frames from video)
        
        Example: --compose frames/ output.mp4 --fps 30'''
    )
    
    # Frame extraction arguments
    extraction_group = parser.add_argument_group('📤 Frame Extraction Options', 
                                                 'Used when extracting frames FROM video files')
    extraction_group.add_argument(
        '--format', '-f',
        default='jpg',
        choices=['jpg', 'jpeg', 'png', 'bmp', 'tiff', 'webp'],
        help='''Image format for extracted frames (default: jpg)
        • jpg/jpeg: Smaller files, good for most uses
        • png: Lossless, larger files, best quality
        • bmp: Uncompressed, very large files
        • tiff: High quality, professional use
        • webp: Modern format, good compression'''
    )
    
    extraction_group.add_argument(
        '--start', '-s',
        type=int,
        default=0,
        help='''Starting frame number (0-based, default: 0)
        Example: --start 100 (skip first 100 frames)
        Useful for skipping intro/setup portions'''
    )
    
    extraction_group.add_argument(
        '--end', '-e',
        type=int,
        default=None,
        help='''Ending frame number (default: extract until end)
        Example: --end 500 (stop at frame 500)
        Useful for extracting specific video segments'''
    )
    
    extraction_group.add_argument(
        '--step',
        type=int,
        default=1,
        help='''Frame sampling interval (default: 1 = every frame)
        • --step 1: Every frame (full extraction)
        • --step 2: Every other frame (50%% of frames)
        • --step 10: Every 10th frame (10%% of frames)
        • --step 30: Every 30th frame (good for long videos)'''
    )
    
    # Video composition arguments
    composition_group = parser.add_argument_group('📥 Video Composition Options', 
                                                 'Used when composing frame images INTO video files')
    composition_group.add_argument(
        '--fps',
        type=float,
        default=25.0,
        help='''Frames per second for output video (default: 25.0)
        • 1-5 fps: Slow motion / slide show effect
        • 15-25 fps: Standard video playback
        • 30-60 fps: Smooth motion / time-lapse
        • 120+ fps: High-speed time-lapse
        
        Example: --fps 30 (smooth 30fps video)'''
    )
    
    composition_group.add_argument(
        '--pattern', '-p',
        default='frame_*.jpg',
        help='''Glob pattern to match frame files in directory (default: "frame_*.jpg")
        • "frame_*.jpg": Standard extraction output
        • "*.png": All PNG files in directory
        • "img_*.jpeg": Custom naming pattern
        • "photo_[0-9]*.jpg": Numbered photos
        
        Example: --pattern "capture_*.png"'''
    )
    
    composition_group.add_argument(
        '--codec',
        default='mp4v',
        choices=['mp4v', 'XVID', 'H264', 'MJPG'],
        help='''Video codec for output MP4 (default: mp4v)
        • mp4v: Universal compatibility, larger files
        • H264: Modern standard, high quality, smaller files
        • XVID: Legacy compatibility, good compression
        • MJPG: Fast encoding, larger files
        
        Recommended: H264 for best quality/size ratio'''
    )
    
    # General arguments
    general_group = parser.add_argument_group('⚙️  General Options', 
                                             'Options that apply to both extraction and composition modes')
    general_group.add_argument(
        '--quiet', '-q',
        action='store_true',
        help='''Suppress progress output and detailed information
        Useful for:
        • Batch processing scripts
        • Automated workflows  
        • When you only want error messages
        • Cleaner output in logs'''
    )
    
    # Parse arguments
    args = parser.parse_args()
    
    try:
        if args.compose:
            # Composition mode: frames directory to video file
            
            # Validate input directory
            if not os.path.isdir(args.input):
                print(f"Error: Input frames directory '{args.input}' does not exist")
                sys.exit(1)
            
            # Validate FPS
            if args.fps <= 0:
                print("Error: FPS must be > 0")
                sys.exit(1)
            
            if not args.quiet:
                print(f"Composing frames from '{args.input}' into video '{args.output}'...")
                print("=" * 60)
            
            frame_count = frames_to_video(
                frames_dir=args.input,
                output_video=args.output,
                fps=args.fps,
                frame_pattern=args.pattern,
                codec=args.codec
            )
            
            if not args.quiet:
                print("=" * 60)
                print(f"✅ Successfully composed {frame_count} frames into video!")
        
        else:
            # Extraction mode: video file to frames directory
            
            # Validate input file
            if not os.path.isfile(args.input):
                print(f"Error: Input video file '{args.input}' does not exist")
                sys.exit(1)
            
            # Validate step
            if args.step < 1:
                print("Error: Step must be >= 1")
                sys.exit(1)
            
            # Validate video file first
            if not args.quiet:
                print(f"Validating video file '{args.input}'...")
            
            is_valid, validation_msg = validate_video_file(args.input)
            
            if not is_valid:
                print(f"❌ Video validation failed: {validation_msg}")
                print("\n💡 Common issues with ESP32 AVI files:")
                print("   • Recording was interrupted before completion")
                print("   • File corruption during SD card operations")
                print("   • Power loss during recording")
                print("   • SD card errors or insufficient space")
                sys.exit(1)
            
            if not args.quiet:
                print(f"✅ {validation_msg}")
                print(f"Converting '{args.input}' to frame images...")
                print("=" * 60)
            
            frame_count = extract_frames(
                video_path=args.input,
                output_dir=args.output,
                image_format=args.format,
                start_frame=args.start,
                end_frame=args.end,
                step=args.step
            )
            
            if not args.quiet:
                print("=" * 60)
                print(f"✅ Successfully extracted {frame_count} frames!")
        
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main() 