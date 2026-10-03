import cv2
import time

STREAM_URL = "http://192.168.1.6:81/stream"

print("====================================")
print(" ESP32-CAM Laptop Stream Test")
print("====================================")
print(f"Connecting to: {STREAM_URL}")

cap = cv2.VideoCapture(STREAM_URL)

if not cap.isOpened():
    print("ERROR: Unable to connect.")
    exit()

print("Connected successfully.")

previous_time = time.time()

while True:

    ret, frame = cap.read()

    if not ret:
        print("Frame reception failed.")
        continue

    # ---------------------------------
    # Calculate FPS
    # ---------------------------------

    current_time = time.time()

    difference = current_time - previous_time

    if difference > 0:
        fps = 1 / difference
    else:
        fps = 0

    previous_time = current_time

    # ---------------------------------
    # Display information
    # ---------------------------------

    height, width = frame.shape[:2]

    cv2.putText(
        frame,
        f"Resolution: {width}x{height}",
        (20, 30),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 255, 255),
        2
    )

    cv2.putText(
        frame,
        f"FPS: {fps:.1f}",
        (20, 60),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 255, 255),
        2
    )

    cv2.imshow(
        "Smart Door Lock - ESP32 Camera",
        frame
    )

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()