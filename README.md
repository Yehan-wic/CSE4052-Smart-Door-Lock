# CSE4052 Advanced Smart Door Lock System

Advanced Smart Door Lock System developed for CSE4052 -
Embedded Systems and Internet of Things.

## System Overview

The system extends an ESP32-based smart door lock with advanced
embedded programming techniques.

### Main Features

- RFID authentication
- AI-based face recognition
- ESP32-CAM video streaming
- ESP32-S3 main access controller
- FreeRTOS multitasking
- WebSocket configuration portal
- Wi-Fi SoftAP configuration mode
- Interrupt-driven button handling
- Hardware timer based RFID sampling
- UART communication between ESP32 boards
- NVS persistent configuration storage
- Task Watchdog Timer
- FreeRTOS queues, mutexes and task notifications
- Automatic door re-locking
- RGB access status indication

## Hardware

- ESP32-S3
- ESP32-CAM
- RC522 RFID Reader
- Relay Module
- Electric Door Lock
- Push Button
- External Power Supply

## System Architecture

![System Architecture](diagrams/system_architecture.png)

## FreeRTOS Architecture

![FreeRTOS Task Diagram](diagrams/freertos_task_diagram.png)

## Authentication Methods

1. RFID
2. AI Face Recognition
3. Manual Push Button

## Communication

- Laptop AI Processor -> ESP32-CAM: TCP
- ESP32-CAM -> ESP32-S3: UART
- RC522 -> ESP32-S3: SPI
- Browser -> ESP32-S3: WebSocket

## AI Face Recognition

Face detection: OpenCV YuNet

Face recognition: OpenCV SFace

Required model files:

- face_detection_yunet_2023mar.onnx
- face_recognition_sface_2021dec.onnx

## Python Setup

```bash
pip install -r requirements.txt
