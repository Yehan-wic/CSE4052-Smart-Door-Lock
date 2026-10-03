import socket

ESP32_CAM_IP = "192.168.1.6"
ESP32_CAM_PORT = 5000

message = "FACE_AUTHORIZED,USER01,0.77\n"

print("Connecting to ESP32-CAM...")

try:
    with socket.create_connection(
        (ESP32_CAM_IP, ESP32_CAM_PORT),
        timeout=3
    ) as sock:

        print("Connected.")

        sock.sendall(message.encode())

        print("Sent:")
        print(message.strip())

        response = sock.recv(1024)

        print(
            "ESP32-CAM response:",
            response.decode().strip()
        )

except Exception as e:
    print("Communication failed:")
    print(e)