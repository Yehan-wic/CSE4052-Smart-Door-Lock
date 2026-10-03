import cv2
import numpy as np
from pathlib import Path
import time
import socket

# ==========================================================
# SETTINGS
# ==========================================================

STREAM_URL = "http://192.168.1.6:81/stream"

ESP32_CAM_IP = "192.168.1.6"
ESP32_CAM_PORT = 5000

DETECTOR_MODEL = Path(
    "models/face_detection_yunet_2023mar.onnx"
)

RECOGNIZER_MODEL = Path(
    "models/face_recognition_sface_2021dec.onnx"
)

AUTHORIZED_FACE = Path(
    "authorized_faces/user01.npy"
)

AUTHORIZED_NAME = "USER01"

# Your score is currently around 0.68,
# so 0.45 is a reasonable initial threshold.
MATCH_THRESHOLD = 0.45

# Require several consecutive matches
REQUIRED_MATCHES = 3

# Do not repeatedly unlock for the same face
AUTH_COOLDOWN = 8.0


# ==========================================================
# TCP FUNCTION
# ==========================================================

def send_to_esp32_cam(message):

    try:

        print()
        print(
            f"Sending to ESP32-CAM: {message}"
        )

        with socket.create_connection(
            (ESP32_CAM_IP, ESP32_CAM_PORT),
            timeout=3
        ) as sock:

            sock.sendall(
                (message + "\n").encode()
            )

            response = sock.recv(1024)

            response_text = (
                response.decode().strip()
            )

            print(
                "ESP32-CAM response:",
                response_text
            )

            return response_text == "OK"

    except Exception as e:

        print(
            "ESP32-CAM communication error:",
            e
        )

        return False


# ==========================================================
# CHECK FILES
# ==========================================================

if not DETECTOR_MODEL.exists():
    raise FileNotFoundError(DETECTOR_MODEL)

if not RECOGNIZER_MODEL.exists():
    raise FileNotFoundError(RECOGNIZER_MODEL)

if not AUTHORIZED_FACE.exists():
    raise FileNotFoundError(AUTHORIZED_FACE)


# ==========================================================
# LOAD AUTHORIZED FACE
# ==========================================================

reference_embedding = np.load(
    AUTHORIZED_FACE
).astype(np.float32)

reference_embedding = (
    reference_embedding.flatten()
)

norm = np.linalg.norm(
    reference_embedding
)

if norm > 0:

    reference_embedding /= norm


print(
    "Authorized face loaded:"
)

print(
    AUTHORIZED_FACE
)


# ==========================================================
# LOAD MODELS
# ==========================================================

detector = cv2.FaceDetectorYN.create(
    str(DETECTOR_MODEL),
    "",
    (320, 320),
    0.8,
    0.3,
    5000
)

recognizer = cv2.FaceRecognizerSF.create(
    str(RECOGNIZER_MODEL),
    ""
)


# ==========================================================
# CAMERA CONNECTION
# ==========================================================

print()
print(
    "======================================"
)

print(
    " SMART DOOR LOCK - FACE RECOGNITION"
)

print(
    "======================================"
)

print(
    f"Connecting to: {STREAM_URL}"
)

cap = cv2.VideoCapture(
    STREAM_URL
)

if not cap.isOpened():

    print(
        "ERROR: Cannot connect to ESP32-CAM."
    )

    exit()


print(
    "ESP32-CAM connected successfully."
)

print(
    "Press Q to quit."
)

print()


# ==========================================================
# VARIABLES
# ==========================================================

consecutive_matches = 0

authorization_sent = False

last_authorized_time = 0

previous_time = time.time()

last_similarity = 0.0


# ==========================================================
# MAIN LOOP
# ==========================================================

while True:

    ret, frame = cap.read()

    if not ret:

        print(
            "Failed to receive frame."
        )

        continue


    height, width = frame.shape[:2]


    detector.setInputSize(
        (width, height)
    )


    # ======================================================
    # FACE DETECTION
    # ======================================================

    _, faces = detector.detect(
        frame
    )


    face_recognized_this_frame = False

    best_similarity = -1


    if faces is not None:

        for face in faces:

            x, y, w, h = (
                face[:4].astype(int)
            )

            try:

                # ==========================================
                # FACE ALIGNMENT
                # ==========================================

                aligned_face = (
                    recognizer.alignCrop(
                        frame,
                        face
                    )
                )


                # ==========================================
                # FACE EMBEDDING
                # ==========================================

                feature = (
                    recognizer.feature(
                        aligned_face
                    )
                )


                feature = (
                    feature
                    .flatten()
                    .astype(np.float32)
                )


                feature_norm = (
                    np.linalg.norm(
                        feature
                    )
                )


                if feature_norm > 0:

                    feature /= feature_norm


                # ==========================================
                # COSINE SIMILARITY
                # ==========================================

                similarity = float(
                    np.dot(
                        reference_embedding,
                        feature
                    )
                )


                if similarity > best_similarity:

                    best_similarity = similarity


                # ==========================================
                # AUTHORIZED
                # ==========================================

                if (
                    similarity >=
                    MATCH_THRESHOLD
                ):

                    face_recognized_this_frame = True

                    color = (
                        0,
                        255,
                        0
                    )

                    label = (
                        f"{AUTHORIZED_NAME} "
                        f"{similarity:.2f}"
                    )


                # ==========================================
                # UNKNOWN
                # ==========================================

                else:

                    color = (
                        0,
                        0,
                        255
                    )

                    label = (
                        f"UNKNOWN "
                        f"{similarity:.2f}"
                    )


                # ==========================================
                # DRAW BOX
                # ==========================================

                cv2.rectangle(
                    frame,
                    (x, y),
                    (x + w, y + h),
                    color,
                    2
                )


                cv2.putText(
                    frame,
                    label,
                    (
                        x,
                        max(
                            y - 10,
                            20
                        )
                    ),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    color,
                    2
                )


            except Exception as e:

                print(
                    "Recognition error:",
                    e
                )


    # ======================================================
    # CONSECUTIVE MATCH LOGIC
    # ======================================================

    if face_recognized_this_frame:

        consecutive_matches = min(
            consecutive_matches + 1,
            REQUIRED_MATCHES
        )

    else:

        consecutive_matches = 0

        # Allow authorization again when
        # person leaves / becomes unknown.
        authorization_sent = False


    # ======================================================
    # AUTHORIZATION
    # ======================================================

    current_time = time.time()


    if (
        consecutive_matches >=
        REQUIRED_MATCHES
    ):

        system_status = "AUTHORIZED"

        status_color = (
            0,
            255,
            0
        )


        # Only send once per recognition event
        # and respect cooldown.

        if (
            not authorization_sent
            and
            current_time -
            last_authorized_time
            >= AUTH_COOLDOWN
        ):

            message = (
                f"FACE_AUTHORIZED,"
                f"{AUTHORIZED_NAME},"
                f"{best_similarity:.3f}"
            )


            if send_to_esp32_cam(
                message
            ):

                print()
                print(
                    "=============================="
                )

                print(
                    " FACE AUTHORIZATION SENT"
                )

                print(
                    f" User: {AUTHORIZED_NAME}"
                )

                print(
                    f" Similarity: "
                    f"{best_similarity:.3f}"
                )

                print(
                    "=============================="
                )


                authorization_sent = True

                last_authorized_time = (
                    current_time
                )


    else:

        system_status = (
            "WAITING / UNKNOWN"
        )

        status_color = (
            0,
            0,
            255
        )


    # ======================================================
    # FPS
    # ======================================================

    elapsed = (
        current_time -
        previous_time
    )


    fps = (
        1 / elapsed
        if elapsed > 0
        else 0
    )


    previous_time = current_time


    # ======================================================
    # DISPLAY INFORMATION
    # ======================================================

    cv2.putText(
        frame,
        f"FPS: {fps:.1f}",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (255, 255, 255),
        2
    )


    cv2.putText(
        frame,
        (
            f"Matches: "
            f"{consecutive_matches}/"
            f"{REQUIRED_MATCHES}"
        ),
        (10, 50),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        (255, 255, 255),
        2
    )


    cv2.putText(
        frame,
        system_status,
        (
            10,
            height - 20
        ),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        status_color,
        2
    )


    # ======================================================
    # SHOW WINDOW
    # ======================================================

    cv2.imshow(
        "Smart Door Lock - Face Recognition",
        frame
    )


    if (
        cv2.waitKey(1) &
        0xFF
        ==
        ord("q")
    ):

        break


# ==========================================================
# CLEANUP
# ==========================================================

cap.release()

cv2.destroyAllWindows()

print(
    "Face recognition stopped."
)