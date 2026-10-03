#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>

#include "board_config.h"

// ======================================================
// WIFI SETTINGS
// ======================================================

const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// ======================================================
// LAPTOP / JETSON -> ESP32-CAM TCP SERVER
// ======================================================

#define FACE_SERVER_PORT 5000

WiFiServer faceResultServer(FACE_SERVER_PORT);


// ======================================================
// ESP32-CAM -> ESP32-S3 UART
// ======================================================

// We are NOT using the SD card, so GPIO13 and GPIO14
// can be used for UART communication.
//
// ESP32-CAM GPIO14 TX -> ESP32-S3 GPIO16 RX
// ESP32-CAM GPIO13 RX <- ESP32-S3 GPIO17 TX

#define S3_RX_PIN 13
#define S3_TX_PIN 14

#define S3_BAUD 115200

HardwareSerial s3Serial(1);


// ======================================================
// FUNCTION DECLARATIONS
// ======================================================

void startCameraServer();

void setupLedFlash();

void handleFaceRecognitionClient();

void processFaceRecognitionMessage(String message);

void sendToESP32S3(String message);

void handleESP32S3Response();


// ======================================================
// RECOGNITION STATE
// ======================================================

bool faceAuthorized = false;

bool unknownFaceDetected = false;

String recognizedUser = "";

float faceConfidence = 0.0;


// ======================================================
// SETUP
// ======================================================

void setup()
{
  // ----------------------------------------------------
  // USB SERIAL DEBUG
  // ----------------------------------------------------

  Serial.begin(115200);

  Serial.setDebugOutput(true);

  delay(1000);


  Serial.println();
  Serial.println("==========================================");
  Serial.println(" SMART DOOR LOCK - ESP32-CAM NODE");
  Serial.println("==========================================");


  // ====================================================
  // ESP32-S3 UART
  // ====================================================

  s3Serial.begin(
    S3_BAUD,
    SERIAL_8N1,
    S3_RX_PIN,
    S3_TX_PIN
  );


  Serial.println();
  Serial.println("ESP32-S3 UART initialized.");

  Serial.print("UART RX GPIO: ");
  Serial.println(S3_RX_PIN);

  Serial.print("UART TX GPIO: ");
  Serial.println(S3_TX_PIN);

  Serial.print("UART Baud: ");
  Serial.println(S3_BAUD);


  // ====================================================
  // CAMERA CONFIGURATION
  // ====================================================

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;

  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_UXGA;

  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  config.fb_location = CAMERA_FB_IN_PSRAM;

  config.jpeg_quality = 12;

  config.fb_count = 1;


  // ====================================================
  // PSRAM CONFIGURATION
  // ====================================================

  if (psramFound())
  {
    Serial.println("PSRAM detected.");

    config.jpeg_quality = 10;

    config.fb_count = 2;

    config.grab_mode = CAMERA_GRAB_LATEST;
  }
  else
  {
    Serial.println("PSRAM NOT detected.");

    config.frame_size = FRAMESIZE_SVGA;

    config.fb_location = CAMERA_FB_IN_DRAM;

    config.fb_count = 1;
  }


  // ====================================================
  // INITIALIZE CAMERA
  // ====================================================

  esp_err_t err = esp_camera_init(&config);


  if (err != ESP_OK)
  {
    Serial.printf(
      "Camera initialization failed: 0x%x\n",
      err
    );

    return;
  }


  Serial.println("Camera initialized successfully.");


  // ====================================================
  // SENSOR SETTINGS
  // ====================================================

  sensor_t *sensor = esp_camera_sensor_get();


  if (sensor != NULL)
  {
    // Start with QVGA.
    // You can change to VGA from the web interface.

    sensor->set_framesize(
      sensor,
      FRAMESIZE_QVGA
    );


    sensor->set_brightness(
      sensor,
      0
    );


    sensor->set_contrast(
      sensor,
      0
    );


    sensor->set_saturation(
      sensor,
      0
    );
  }


  // ====================================================
  // CAMERA SENSOR CORRECTIONS
  // ====================================================

  if (sensor != NULL)
  {
    if (sensor->id.PID == OV3660_PID)
    {
      sensor->set_vflip(sensor, 1);

      sensor->set_brightness(sensor, 1);

      sensor->set_saturation(sensor, -2);
    }
  }


#if defined(CAMERA_MODEL_M5STACK_WIDE) || \
    defined(CAMERA_MODEL_M5STACK_ESP32CAM)

  if (sensor != NULL)
  {
    sensor->set_vflip(sensor, 1);

    sensor->set_hmirror(sensor, 1);
  }

#endif


#if defined(CAMERA_MODEL_ESP32S3_EYE)

  if (sensor != NULL)
  {
    sensor->set_vflip(sensor, 1);
  }

#endif


  // ====================================================
  // CAMERA FLASH LED
  // ====================================================

#if defined(LED_GPIO_NUM)

  setupLedFlash();

#endif


  // ====================================================
  // CONNECT WIFI
  // ====================================================

  Serial.println();

  Serial.print("Connecting to Wi-Fi");


  WiFi.begin(
    ssid,
    password
  );


  // Reduce streaming latency
  WiFi.setSleep(false);


  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);

    Serial.print(".");
  }


  Serial.println();

  Serial.println("Wi-Fi connected.");


  // ====================================================
  // START CAMERA WEB SERVER
  // ====================================================

  startCameraServer();


  // ====================================================
  // START FACE RECOGNITION TCP SERVER
  // ====================================================

  faceResultServer.begin();


  // ====================================================
  // SYSTEM INFORMATION
  // ====================================================

  IPAddress ip = WiFi.localIP();


  Serial.println();
  Serial.println("==========================================");
  Serial.println(" ESP32-CAM READY");
  Serial.println("==========================================");


  Serial.print("IP Address: ");

  Serial.println(ip);


  Serial.print("Camera webpage: http://");

  Serial.println(ip);


  Serial.print("Video stream: http://");

  Serial.print(ip);

  Serial.println(":81/stream");


  Serial.print("Recognition server: ");

  Serial.print(ip);

  Serial.print(":");

  Serial.println(FACE_SERVER_PORT);


  Serial.println();


  Serial.println(
    "ESP32-S3 communication:"
  );


  Serial.println(
    "CAM GPIO14 TX -> S3 GPIO16 RX"
  );


  Serial.println(
    "CAM GPIO13 RX <- S3 GPIO17 TX"
  );


  Serial.println();


  Serial.println(
    "Waiting for face recognition results..."
  );


  Serial.println("==========================================");
}


// ======================================================
// MAIN LOOP
// ======================================================

void loop()
{
  // Receive AI result from Laptop / Jetson

  handleFaceRecognitionClient();


  // Receive acknowledgment/status from ESP32-S3

  handleESP32S3Response();


  delay(2);
}


// ======================================================
// LAPTOP / JETSON TCP CLIENT
// ======================================================

void handleFaceRecognitionClient()
{
  WiFiClient client =
    faceResultServer.available();


  if (!client)
  {
    return;
  }


  Serial.println();

  Serial.println("------------------------------------------");

  Serial.println(
    "Face recognition client connected."
  );


  client.setTimeout(300);


  unsigned long connectionStart =
    millis();


  while (
    client.connected() &&
    millis() - connectionStart < 1500
  )
  {
    if (client.available())
    {
      String message =
        client.readStringUntil('\n');


      message.trim();


      if (message.length() > 0)
      {
        Serial.print(
          "Received from AI processor: "
        );


        Serial.println(message);


        processFaceRecognitionMessage(
          message
        );


        // Tell laptop/Jetson message arrived successfully

        client.println("OK");
      }


      break;
    }


    delay(1);
  }


  client.stop();


  Serial.println(
    "Recognition client disconnected."
  );


  Serial.println("------------------------------------------");
}


// ======================================================
// PROCESS FACE RECOGNITION RESULT
// ======================================================

void processFaceRecognitionMessage(
  String message
)
{
  // ====================================================
  // FACE AUTHORIZED
  //
  // Expected:
  //
  // FACE_AUTHORIZED,USER01,0.770
  // ====================================================

  if (
    message.startsWith(
      "FACE_AUTHORIZED"
    )
  )
  {
    faceAuthorized = true;

    unknownFaceDetected = false;

    recognizedUser = "";

    faceConfidence = 0.0;


    // --------------------------------------------------
    // PARSE CSV
    // --------------------------------------------------

    int firstComma =
      message.indexOf(',');


    int secondComma =
      message.indexOf(
        ',',
        firstComma + 1
      );


    if (firstComma >= 0)
    {
      if (secondComma >= 0)
      {
        recognizedUser =
          message.substring(
            firstComma + 1,
            secondComma
          );


        String confidenceString =
          message.substring(
            secondComma + 1
          );


        faceConfidence =
          confidenceString.toFloat();
      }
      else
      {
        recognizedUser =
          message.substring(
            firstComma + 1
          );
      }
    }


    // --------------------------------------------------
    // DEBUG
    // --------------------------------------------------

    Serial.println();

    Serial.println(
      "******************************************"
    );


    Serial.println(
      "            FACE AUTHORIZED"
    );


    Serial.println(
      "******************************************"
    );


    Serial.print("User: ");

    Serial.println(
      recognizedUser
    );


    Serial.print("Similarity: ");

    Serial.println(
      faceConfidence,
      3
    );


    // --------------------------------------------------
    // SEND TO ESP32-S3
    // --------------------------------------------------

    String uartMessage =
      "FACE_AUTHORIZED," +
      recognizedUser +
      "," +
      String(
        faceConfidence,
        3
      );


    sendToESP32S3(
      uartMessage
    );


    Serial.println(
      "******************************************"
    );


    return;
  }


  // ====================================================
  // UNKNOWN FACE
  // ====================================================

  if (
    message.startsWith(
      "FACE_UNKNOWN"
    )
  )
  {
    faceAuthorized = false;

    unknownFaceDetected = true;

    recognizedUser = "";

    faceConfidence = 0.0;


    Serial.println();

    Serial.println(
      "******************************************"
    );


    Serial.println(
      "          UNKNOWN FACE DETECTED"
    );


    Serial.println(
      "******************************************"
    );


    Serial.println(
      "Door must remain LOCKED."
    );


    // Tell S3 about the unknown face

    sendToESP32S3(
      "FACE_UNKNOWN"
    );


    Serial.println(
      "Future feature:"
    );


    Serial.println(
      "Capture image + notify mobile application."
    );


    Serial.println(
      "******************************************"
    );


    return;
  }


  // ====================================================
  // NO FACE
  // ====================================================

  if (
    message.startsWith(
      "NO_FACE"
    )
  )
  {
    faceAuthorized = false;

    unknownFaceDetected = false;

    recognizedUser = "";

    faceConfidence = 0.0;


    Serial.println(
      "No face currently detected."
    );


    sendToESP32S3(
      "NO_FACE"
    );


    return;
  }


  // ====================================================
  // PING
  // ====================================================

  if (
    message.startsWith(
      "PING"
    )
  )
  {
    Serial.println(
      "AI processor PING received."
    );


    return;
  }


  // ====================================================
  // UNKNOWN COMMAND
  // ====================================================

  Serial.print(
    "Unknown AI command: "
  );


  Serial.println(
    message
  );
}


// ======================================================
// SEND COMMAND TO ESP32-S3
// ======================================================

void sendToESP32S3(
  String message
)
{
  s3Serial.println(
    message
  );


  Serial.print(
    "Sent to ESP32-S3: "
  );


  Serial.println(
    message
  );
}


// ======================================================
// RECEIVE ESP32-S3 RESPONSE
// ======================================================

void handleESP32S3Response()
{
  if (!s3Serial.available())
  {
    return;
  }


  String response =
    s3Serial.readStringUntil('\n');


  response.trim();


  if (response.length() == 0)
  {
    return;
  }


  Serial.print(
    "ESP32-S3 response: "
  );


  Serial.println(
    response
  );


  // ------------------------------------------
  // Example acknowledgements
  // ------------------------------------------

  if (
    response == "S3_ACK"
  )
  {
    Serial.println(
      "ESP32-S3 received command successfully."
    );
  }


  else if (
    response == "DOOR_UNLOCKED"
  )
  {
    Serial.println(
      "ESP32-S3 reports door UNLOCKED."
    );
  }


  else if (
    response == "DOOR_LOCKED"
  )
  {
    Serial.println(
      "ESP32-S3 reports door LOCKED."
    );
  }
}