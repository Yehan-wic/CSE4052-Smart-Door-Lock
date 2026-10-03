import cv2
import numpy as np
from pathlib import Path

# ==========================================================
# SETTINGS
# ==========================================================

STREAM_URL = "http://192.168.1.6:81/stream"

DETECTOR_MODEL = Path("models/face_detection_yunet_2023mar.onnx")
RECOGNIZER_MODEL = Path("models/face_recognition_sface_2021dec.onnx")

SAVE_FOLDER = Path("authorized_faces")
SAVE_FOLDER.mkdir(exist_ok=True)

USER_NAME = "user01"

REQUIRED_SAMPLES = 20

# ==========================================================
# CHECK MODELS
# ==========================================================

if not DETECTOR_MODEL.exists():
    raise FileNotFoundError(f"Missing model: {DETECTOR_MODEL}")

if not RECOGNIZER_MODEL.exists():
    raise FileNotFoundError(f"Missing model: {RECOGNIZER_MODEL}")

# ==========================================================
# LOAD MODELS
# ==========================================================

detector = cv2.FaceDetectorYN.create(
    str(DETECTOR_MODEL),
    "",
    (320, 320),
    0.9,   # score threshold
    0.3,   # NMS threshold
    5000
)

recognizer = cv2.FaceRecognizerSF.create(
    str(RECOGNIZER_MODEL),
    ""
)

# ==========================================================
# CONNECT CAMERA
# ==========================================================

print("Connecting to ESP32-CAM...")

cap = cv2.VideoCapture(STREAM_URL)

if not cap.isOpened():
    print("ERROR: Could not connect to camera.")
    exit()

print("ESP32-CAM connected.")
print()
print("INSTRUCTIONS")
print("--------------------------------")
print("C = capture face sample")
print("S = save enrollment")
print("Q = quit")
print()
print(f"Collect at least {REQUIRED_SAMPLES} samples.")
print("Move your head slightly between samples.")
print("--------------------------------")

features = []
last_face_crop = None

# ==========================================================
# MAIN LOOP
# ==========================================================

while True:

    ret, frame = cap.read()

    if not ret:
        continue

    height, width = frame.shape[:2]

    detector.setInputSize((width, height))

    _, faces = detector.detect(frame)

    selected_face = None

    # ------------------------------------------------------
    # Find largest detected face
    # ------------------------------------------------------

    if faces is not None and len(faces) > 0:

        selected_face = max(
            faces,
            key=lambda f: f[2] * f[3]
        )

        x, y, w, h = selected_face[:4].astype(int)

        # Draw bounding box
        cv2.rectangle(
            frame,
            (x, y),
            (x + w, y + h),
            (0, 255, 0),
            2
        )

        cv2.putText(
            frame,
            "FACE DETECTED",
            (x, max(y - 10, 20)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (0, 255, 0),
            2
        )

    # ------------------------------------------------------
    # Information
    # ------------------------------------------------------

    cv2.putText(
        frame,
        f"Samples: {len(features)}/{REQUIRED_SAMPLES}",
        (10, 30),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (255, 255, 255),
        2
    )

    cv2.putText(
        frame,
        "C: Capture   S: Save   Q: Quit",
        (10, height - 15),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        2
    )

    cv2.imshow("Face Enrollment", frame)

    key = cv2.waitKey(1) & 0xFF

    # ======================================================
    # CAPTURE SAMPLE
    # ======================================================

    if key == ord("c"):

        if selected_face is None:

            print("No face detected. Try again.")

            continue

        try:

            # Align face using YuNet landmarks
            aligned_face = recognizer.alignCrop(
                frame,
                selected_face
            )

            # Generate embedding
            feature = recognizer.feature(
                aligned_face
            )

            feature = feature.flatten().astype(np.float32)

            # Normalize embedding
            norm = np.linalg.norm(feature)

            if norm > 0:
                feature = feature / norm

            features.append(feature)

            last_face_crop = aligned_face.copy()

            print(
                f"Captured sample "
                f"{len(features)}/{REQUIRED_SAMPLES}"
            )

        except Exception as e:

            print("Failed to capture sample:", e)

    # ======================================================
    # SAVE
    # ======================================================

    elif key == ord("s"):

        if len(features) < REQUIRED_SAMPLES:

            print(
                f"Need at least {REQUIRED_SAMPLES} samples. "
                f"Current: {len(features)}"
            )

            continue

        # Average all face embeddings
        average_embedding = np.mean(
            np.vstack(features),
            axis=0
        )

        # Normalize again
        average_embedding = (
            average_embedding /
            np.linalg.norm(average_embedding)
        )

        embedding_path = (
            SAVE_FOLDER /
            f"{USER_NAME}.npy"
        )

        np.save(
            embedding_path,
            average_embedding
        )

        print()
        print("================================")
        print("FACE ENROLLMENT COMPLETE")
        print("================================")
        print(f"User: {USER_NAME}")
        print(f"Samples: {len(features)}")
        print(f"Saved: {embedding_path}")

        # Save reference face image
        if last_face_crop is not None:

            image_path = (
                SAVE_FOLDER /
                f"{USER_NAME}.jpg"
            )

            cv2.imwrite(
                str(image_path),
                last_face_crop
            )

            print(f"Reference image: {image_path}")

        break

    # ======================================================
    # QUIT
    # ======================================================

    elif key == ord("q"):

        print("Enrollment cancelled.")
        break

# ==========================================================
# CLEANUP
# ==========================================================

cap.release()
cv2.destroyAllWindows()