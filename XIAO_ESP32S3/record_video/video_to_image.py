import cv2
import time

# Path to your video file
video_path = 'your_video.mp4'
video_path = 'video9.avi'

# Create a video capture object
cap = cv2.VideoCapture(video_path)

# Check if the video file was successfully opened
if not cap.isOpened():
    print("Error: Could not open video file.")
    exit()

# Read and display video frames
while cap.isOpened():
    ret, frame = cap.read()
    
    if not ret:
        break  # Exit loop if frames are not returned (end of video)
    
    # Display the frame (optional)
    cv2.imshow('Video', frame)

    time.sleep(0.1)

    # Exit when 'q' is pressed
    if cv2.waitKey(25) & 0xFF == ord('q'):
        break

# Release resources
cap.release()
cv2.destroyAllWindows()

