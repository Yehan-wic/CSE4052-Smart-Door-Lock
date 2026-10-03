import cv2
import time

STREAM_URL = "http://192.168.1.6:81/stream"

# Built-in OpenCV face detector
face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)

print("====================================")
print(" Smart Door Lock - Face Detection")
print("====================================")
print(f"Connecting to: {STREAM_URL}")

cap = cv2.VideoCapture(STREAM_URL)

if not cap.isOpened():
    print("ERROR: Could not connect to ESP32-CAM")
    exit()

print("ESP32-CAM connected successfully.")

previous_time = time.time()

while True:

    ret, frame = cap.read()

    if not ret:
        print("Failed to receive frame.")
        continue

    # --------------------------------------
    # Prepare image for face detection
    # --------------------------------------

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # Helps slightly with uneven lighting
    gray = cv2.equalizeHist(gray)

    # --------------------------------------
    # Detect faces
    # --------------------------------------

    faces = face_cascade.detectMultiScale(
        gray,
        scaleFactor=1.1,
        minNeighbors=5,
        minSize=(50, 50)
    )

    # --------------------------------------
    # Draw detected faces
    # --------------------------------------

    for (x, y, w, h) in faces:

        cv2.rectangle(
            frame,
            (x, y),
            (x + w, y + h),
            (0, 255, 0),
            2
        )

        cv2.putText(
            frame,
            "FACE",
            (x, y - 10),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (0, 255, 0),
            2
        )

    # --------------------------------------
    # FPS
    # --------------------------------------

    current_time = time.time()
    difference = current_time - previous_time

    fps = 1 / difference if difference > 0 else 0

    previous_time = current_time

    # --------------------------------------
    # Information
    # --------------------------------------

    height, width = frame.shape[:2]

    cv2.putText(
        frame,
        f"Resolution: {width}x{height}",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        2
    )

    cv2.putText(
        frame,
        f"FPS: {fps:.1f}",
        (10, 50),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        2
    )

    cv2.putText(
        frame,
        f"Faces: {len(faces)}",
        (10, 75),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        2
    )

    # --------------------------------------
    # Display
    # --------------------------------------

    cv2.imshow(
        "Smart Door Lock - Face Detection",
        frame
    )

    # Q = quit
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break


cap.release()
cv2.destroyAllWindows()

print("Face detection stopped.")