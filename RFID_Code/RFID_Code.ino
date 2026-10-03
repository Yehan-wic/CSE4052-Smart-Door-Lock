#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_task_wdt.h"
#include "esp_err.h"

#define RFID_SS_PIN     10
#define RFID_SCK_PIN    12
#define RFID_MOSI_PIN   11
#define RFID_MISO_PIN   13
#define RFID_RST_PIN    9
#define RFID_IRQ_PIN    14

#define RELAY_PIN       4
#define BUTTON_PIN      5

#define CAM_RX_PIN      16
#define CAM_TX_PIN      17
#define CAM_BAUD        115200

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
HardwareSerial camSerial(1);

WebServer webServer(80);
WebSocketsServer webSocket(81);

Preferences preferences;

const char *CONFIG_AP_SSID = "SmartLock-Setup";
const char *CONFIG_AP_PASSWORD = "SmartLock123";

String deviceName = "SmartLock-01";
String configuredWiFiSSID = "";
String configuredWiFiPassword = "";

unsigned long unlockTimeMs = 5000;

bool rfidEnabled = true;
bool faceEnabled = true;

bool configMode = false;
bool startNormalRequested = false;
bool normalModeStarted = false;

byte authorizedUID[] = {
  0xF1,
  0x87,
  0x8C,
  0x02
};

const byte AUTHORIZED_UID_LENGTH =
  sizeof(authorizedUID);

const bool RELAY_ACTIVE_LOW = true;

const unsigned long LED_DISPLAY_TIME = 2000;
const unsigned long FACE_UNLOCK_COOLDOWN = 7000;
const unsigned long BUTTON_DEBOUNCE_TIME = 100;
const unsigned long RFID_COOLDOWN = 800;

const uint32_t WATCHDOG_TIMEOUT_MS = 5000;

const bool WATCHDOG_TEST_MODE = false;

const unsigned long WATCHDOG_TEST_START_MS = 15000;
const unsigned long WATCHDOG_TEST_STALL_MS = 10000;

enum AccessEventType
{
  ACCESS_RFID_GRANTED,
  ACCESS_RFID_DENIED,
  ACCESS_FACE_GRANTED,
  ACCESS_FACE_DENIED,
  ACCESS_BUTTON_GRANTED
};

struct AccessEvent
{
  AccessEventType type;
  char source[16];
  float confidence;
};

enum DoorCommandType
{
  DOOR_UNLOCK_COMMAND
};

struct DoorCommand
{
  DoorCommandType command;
  char source[16];
};

enum LedCommand
{
  LED_GREEN,
  LED_RED
};

QueueHandle_t accessEventQueue = nullptr;
QueueHandle_t doorCommandQueue = nullptr;
QueueHandle_t ledCommandQueue = nullptr;

SemaphoreHandle_t camTxMutex = nullptr;

TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t rfidTaskHandle = nullptr;

hw_timer_t *rfidTimer = nullptr;

unsigned long lastFaceUnlockTime = 0;

const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width, initial-scale=1.0">

<title>
Smart Door Lock Configuration
</title>

<style>

body {
    font-family: Arial, sans-serif;
    background: #f2f4f8;
    margin: 0;
    padding: 20px;
}

.container {
    max-width: 520px;
    margin: auto;
    background: white;
    border-radius: 14px;
    padding: 25px;
    box-shadow: 0 4px 16px rgba(0,0,0,0.12);
}

h1 {
    text-align: center;
    margin-bottom: 5px;
}

.subtitle {
    text-align: center;
    color: #666;
    margin-bottom: 25px;
}

label {
    display: block;
    font-weight: bold;
    margin-top: 16px;
    margin-bottom: 6px;
}

input[type=text],
input[type=password],
input[type=number] {
    width: 100%;
    box-sizing: border-box;
    padding: 11px;
    border: 1px solid #bbb;
    border-radius: 7px;
    font-size: 16px;
}

.switchRow {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-top: 18px;
    padding: 12px;
    background: #f7f7f7;
    border-radius: 7px;
}

button {
    width: 100%;
    padding: 13px;
    border: none;
    border-radius: 7px;
    margin-top: 18px;
    font-size: 16px;
    cursor: pointer;
}

.save {
    background: #1e88e5;
    color: white;
}

.start {
    background: #2e7d32;
    color: white;
}

.status {
    margin-top: 20px;
    padding: 12px;
    text-align: center;
    border-radius: 7px;
    background: #eeeeee;
}

.connected {
    color: #2e7d32;
}

.disconnected {
    color: #c62828;
}

.note {
    margin-top: 18px;
    font-size: 13px;
    color: #777;
}

</style>

</head>

<body>

<div class="container">

<h1>
Smart Door Lock
</h1>

<div class="subtitle">
Configuration Portal
</div>

<label>
Device Name
</label>

<input
    id="deviceName"
    type="text"
    value="SmartLock-01"
>

<label>
Unlock Duration (milliseconds)
</label>

<input
    id="unlockTime"
    type="number"
    min="1000"
    max="30000"
    value="5000"
>

<div class="switchRow">

<span>
RFID Authentication
</span>

<input
    id="rfidEnabled"
    type="checkbox"
    checked
>

</div>

<div class="switchRow">

<span>
Face Recognition
</span>

<input
    id="faceEnabled"
    type="checkbox"
    checked
>

</div>

<label>
Wi-Fi SSID
</label>

<input
    id="wifiSSID"
    type="text"
    placeholder="Home Wi-Fi SSID"
>

<label>
Wi-Fi Password
</label>

<input
    id="wifiPassword"
    type="password"
    placeholder="Enter new password only if changing"
>

<button
    class="save"
    onclick="saveConfiguration()"
>
Save Configuration
</button>

<button
    class="start"
    onclick="startNormalMode()"
>
Start Smart Lock
</button>

<div
    id="connectionStatus"
    class="status disconnected"
>
WebSocket: Connecting...
</div>

<div
    id="messageStatus"
    class="status"
>
Waiting for configuration...
</div>

<div class="note">
Configuration is stored permanently in ESP32 NVS flash.
</div>

</div>

<script>

let socket;

function connectWebSocket() {

    socket = new WebSocket(
        "ws://" +
        window.location.hostname +
        ":81/"
    );

    socket.onopen = function() {

        document.getElementById(
            "connectionStatus"
        ).innerHTML =
            "WebSocket: CONNECTED";

        document.getElementById(
            "connectionStatus"
        ).className =
            "status connected";

        socket.send(
            "GET_CONFIG"
        );
    };

    socket.onclose = function() {

        document.getElementById(
            "connectionStatus"
        ).innerHTML =
            "WebSocket: DISCONNECTED";

        document.getElementById(
            "connectionStatus"
        ).className =
            "status disconnected";

        setTimeout(
            connectWebSocket,
            2000
        );
    };

    socket.onmessage = function(event) {

        let message = event.data;

        if (
            message.startsWith(
                "CONFIG_DEVICE_NAME:"
            )
        ) {
            document.getElementById(
                "deviceName"
            ).value =
                message.substring(
                    "CONFIG_DEVICE_NAME:".length
                );
        }

        else if (
            message.startsWith(
                "CONFIG_UNLOCK_TIME:"
            )
        ) {
            document.getElementById(
                "unlockTime"
            ).value =
                message.substring(
                    "CONFIG_UNLOCK_TIME:".length
                );
        }

        else if (
            message.startsWith(
                "CONFIG_RFID:"
            )
        ) {
            document.getElementById(
                "rfidEnabled"
            ).checked =
                message.substring(
                    "CONFIG_RFID:".length
                ) === "1";
        }

        else if (
            message.startsWith(
                "CONFIG_FACE:"
            )
        ) {
            document.getElementById(
                "faceEnabled"
            ).checked =
                message.substring(
                    "CONFIG_FACE:".length
                ) === "1";
        }

        else if (
            message.startsWith(
                "CONFIG_WIFI_SSID:"
            )
        ) {
            document.getElementById(
                "wifiSSID"
            ).value =
                message.substring(
                    "CONFIG_WIFI_SSID:".length
                );
        }

        else if (
            message.startsWith(
                "STATUS:"
            )
        ) {
            document.getElementById(
                "messageStatus"
            ).innerHTML =
                message.substring(
                    "STATUS:".length
                );
        }
    };
}

function send(message) {

    if (
        socket &&
        socket.readyState ===
        WebSocket.OPEN
    ) {
        socket.send(message);
    }
}

function saveConfiguration() {

    let deviceName =
        document.getElementById(
            "deviceName"
        ).value;

    let unlockTime =
        document.getElementById(
            "unlockTime"
        ).value;

    let rfid =
        document.getElementById(
            "rfidEnabled"
        ).checked ? "1" : "0";

    let face =
        document.getElementById(
            "faceEnabled"
        ).checked ? "1" : "0";

    let ssid =
        document.getElementById(
            "wifiSSID"
        ).value;

    let password =
        document.getElementById(
            "wifiPassword"
        ).value;

    send(
        "SET_DEVICE_NAME:" +
        deviceName
    );

    send(
        "SET_UNLOCK_TIME:" +
        unlockTime
    );

    send(
        "SET_RFID:" +
        rfid
    );

    send(
        "SET_FACE:" +
        face
    );

    send(
        "SET_WIFI_SSID:" +
        ssid
    );

    if (
        password.length > 0
    ) {
        send(
            "SET_WIFI_PASSWORD:" +
            password
        );
    }

    setTimeout(
        function() {
            send("SAVE");
        },
        150
    );
}

function startNormalMode() {

    document.getElementById(
        "messageStatus"
    ).innerHTML =
        "Starting Smart Lock...";

    send(
        "START_NORMAL"
    );
}

connectWebSocket();

</script>

</body>

</html>
)rawliteral";

void RFIDTask(void *parameter);
void CameraCommTask(void *parameter);
void AccessControlTask(void *parameter);
void DoorControlTask(void *parameter);
void LEDTask(void *parameter);
void ButtonTask(void *parameter);
void SystemMonitorTask(void *parameter);

void startConfigurationMode();
void startNormalMode();

void loadConfiguration();
void saveConfiguration();

void initializeWatchdog();

void webSocketEvent(
  uint8_t num,
  WStype_t type,
  uint8_t *payload,
  size_t length
);

void sendConfiguration(uint8_t num);

void setRelay(bool enabled);
void sendToCamera(const char *message);

bool isAuthorizedCard();
void printUID();

void IRAM_ATTR buttonISR()
{
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  if (
    buttonTaskHandle != nullptr
  )
  {
    vTaskNotifyGiveFromISR(
      buttonTaskHandle,
      &higherPriorityTaskWoken
    );
  }

  portYIELD_FROM_ISR(
    higherPriorityTaskWoken
  );
}

void ARDUINO_ISR_ATTR rfidTimerISR()
{
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  if (
    rfidTaskHandle != nullptr
  )
  {
    vTaskNotifyGiveFromISR(
      rfidTaskHandle,
      &higherPriorityTaskWoken
    );
  }

  portYIELD_FROM_ISR(
    higherPriorityTaskWoken
  );
}

void initializeWatchdog()
{
  esp_task_wdt_config_t watchdogConfig;

  watchdogConfig.timeout_ms =
    WATCHDOG_TIMEOUT_MS;

  watchdogConfig.idle_core_mask =
    (1 << portNUM_PROCESSORS) - 1;

  watchdogConfig.trigger_panic =
    true;

  esp_err_t result =
    esp_task_wdt_init(
      &watchdogConfig
    );

  if (
    result ==
    ESP_ERR_INVALID_STATE
  )
  {
    result =
      esp_task_wdt_reconfigure(
        &watchdogConfig
      );
  }

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " TASK WATCHDOG"
  );

  Serial.println(
    "========================================"
  );

  if (
    result == ESP_OK
  )
  {
    Serial.println(
      "Task Watchdog configured."
    );

    Serial.print(
      "Timeout: "
    );

    Serial.print(
      WATCHDOG_TIMEOUT_MS
    );

    Serial.println(
      " ms"
    );

    Serial.println(
      "Monitoring:"
    );

    Serial.println(
      "- CameraCommTask"
    );

    Serial.println(
      "- AccessControlTask"
    );

    Serial.println(
      "- DoorControlTask"
    );
  }
  else
  {
    Serial.print(
      "Watchdog configuration error: "
    );

    Serial.println(
      esp_err_to_name(
        result
      )
    );
  }

  Serial.print(
    "Watchdog test mode: "
  );

  Serial.println(
    WATCHDOG_TEST_MODE
      ? "ENABLED"
      : "DISABLED"
  );

  Serial.println(
    "========================================"
  );
}

void loadConfiguration()
{
  preferences.begin(
    "smartlock",
    true
  );

  deviceName =
    preferences.getString(
      "device",
      "SmartLock-01"
    );

  unlockTimeMs =
    preferences.getULong(
      "unlock",
      5000
    );

  rfidEnabled =
    preferences.getBool(
      "rfid",
      true
    );

  faceEnabled =
    preferences.getBool(
      "face",
      true
    );

  configuredWiFiSSID =
    preferences.getString(
      "wifi_ssid",
      ""
    );

  configuredWiFiPassword =
    preferences.getString(
      "wifi_pass",
      ""
    );

  preferences.end();

  if (
    deviceName.length() == 0
  )
  {
    deviceName =
      "SmartLock-01";
  }

  if (
    unlockTimeMs < 1000 ||
    unlockTimeMs > 30000
  )
  {
    unlockTimeMs =
      5000;
  }

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " CONFIGURATION LOADED FROM NVS"
  );

  Serial.println(
    "========================================"
  );

  Serial.print(
    "Device Name: "
  );

  Serial.println(
    deviceName
  );

  Serial.print(
    "Unlock Time: "
  );

  Serial.print(
    unlockTimeMs
  );

  Serial.println(
    " ms"
  );

  Serial.print(
    "RFID: "
  );

  Serial.println(
    rfidEnabled
      ? "ENABLED"
      : "DISABLED"
  );

  Serial.print(
    "Face Recognition: "
  );

  Serial.println(
    faceEnabled
      ? "ENABLED"
      : "DISABLED"
  );

  Serial.print(
    "Wi-Fi SSID: "
  );

  if (
    configuredWiFiSSID.length() > 0
  )
  {
    Serial.println(
      configuredWiFiSSID
    );
  }
  else
  {
    Serial.println(
      "[NOT CONFIGURED]"
    );
  }

  Serial.println(
    "Wi-Fi Password: [HIDDEN]"
  );

  Serial.println(
    "========================================"
  );
}

void saveConfiguration()
{
  preferences.begin(
    "smartlock",
    false
  );

  preferences.putString(
    "device",
    deviceName
  );

  preferences.putULong(
    "unlock",
    unlockTimeMs
  );

  preferences.putBool(
    "rfid",
    rfidEnabled
  );

  preferences.putBool(
    "face",
    faceEnabled
  );

  preferences.putString(
    "wifi_ssid",
    configuredWiFiSSID
  );

  preferences.putString(
    "wifi_pass",
    configuredWiFiPassword
  );

  preferences.end();

  Serial.println(
    "Configuration saved to NVS."
  );
}

void setRelay(
  bool enabled
)
{
  if (
    RELAY_ACTIVE_LOW
  )
  {
    digitalWrite(
      RELAY_PIN,
      enabled ? LOW : HIGH
    );
  }
  else
  {
    digitalWrite(
      RELAY_PIN,
      enabled ? HIGH : LOW
    );
  }
}

void sendToCamera(
  const char *message
)
{
  if (
    camTxMutex == nullptr
  )
  {
    return;
  }

  if (
    xSemaphoreTake(
      camTxMutex,
      pdMS_TO_TICKS(100)
    ) == pdTRUE
  )
  {
    camSerial.println(
      message
    );

    xSemaphoreGive(
      camTxMutex
    );
  }
}

void setup()
{
  Serial.begin(
    115200
  );

  delay(
    500
  );

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " SMART DOOR LOCK"
  );

  Serial.println(
    "========================================"
  );

  loadConfiguration();

  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  setRelay(
    false
  );

#ifdef RGB_BUILTIN

  rgbLedWrite(
    RGB_BUILTIN,
    0,
    0,
    0
  );

#endif

  delay(
    300
  );

  if (
    digitalRead(
      BUTTON_PIN
    ) == LOW
  )
  {
    startConfigurationMode();
  }
  else
  {
    startNormalMode();
  }
}

void loop()
{
  if (
    configMode
  )
  {
    webServer.handleClient();

    webSocket.loop();

    if (
      startNormalRequested
    )
    {
      startNormalRequested =
        false;

      Serial.println();

      Serial.println(
        "Leaving configuration mode..."
      );

      WiFi.softAPdisconnect(
        true
      );

      delay(
        200
      );

      WiFi.mode(
        WIFI_OFF
      );

      configMode =
        false;

      startNormalMode();
    }

    delay(
      2
    );
  }
  else
  {
    vTaskDelay(
      pdMS_TO_TICKS(1000)
    );
  }
}

void startConfigurationMode()
{
  configMode =
    true;

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " CONFIGURATION MODE"
  );

  Serial.println(
    "========================================"
  );

  WiFi.mode(
    WIFI_AP
  );

  bool result =
    WiFi.softAP(
      CONFIG_AP_SSID,
      CONFIG_AP_PASSWORD
    );

  if (
    !result
  )
  {
    Serial.println(
      "ERROR: Failed to start SoftAP."
    );

    return;
  }

  IPAddress ip =
    WiFi.softAPIP();

  webServer.on(
    "/",
    HTTP_GET,
    []()
    {
      webServer.send_P(
        200,
        "text/html",
        CONFIG_HTML
      );
    }
  );

  webServer.onNotFound(
    []()
    {
      webServer.send_P(
        200,
        "text/html",
        CONFIG_HTML
      );
    }
  );

  webServer.begin();

  webSocket.begin();

  webSocket.onEvent(
    webSocketEvent
  );

  Serial.println();

  Serial.print(
    "SSID: "
  );

  Serial.println(
    CONFIG_AP_SSID
  );

  Serial.print(
    "Password: "
  );

  Serial.println(
    CONFIG_AP_PASSWORD
  );

  Serial.print(
    "IP Address: "
  );

  Serial.println(
    ip
  );

  Serial.println(
    "HTTP Server: Port 80"
  );

  Serial.println(
    "WebSocket Server: Port 81"
  );

  Serial.println();

  Serial.print(
    "Open: http://"
  );

  Serial.println(
    ip
  );

  Serial.println(
    "========================================"
  );
}

void webSocketEvent(
  uint8_t num,
  WStype_t type,
  uint8_t *payload,
  size_t length
)
{
  if (
    type == WStype_CONNECTED
  )
  {
    Serial.print(
      "[WebSocket] Client connected: "
    );

    Serial.println(
      webSocket.remoteIP(num)
    );

    sendConfiguration(
      num
    );

    return;
  }

  if (
    type == WStype_DISCONNECTED
  )
  {
    Serial.println(
      "[WebSocket] Client disconnected."
    );

    return;
  }

  if (
    type != WStype_TEXT
  )
  {
    return;
  }

  String message;

  for (
    size_t i = 0;
    i < length;
    i++
  )
  {
    message +=
      (char)payload[i];
  }

  Serial.print(
    "[WebSocket] RX: "
  );

  if (
    message.startsWith(
      "SET_WIFI_PASSWORD:"
    )
  )
  {
    Serial.println(
      "SET_WIFI_PASSWORD:********"
    );
  }
  else
  {
    Serial.println(
      message
    );
  }

  if (
    message == "GET_CONFIG"
  )
  {
    sendConfiguration(
      num
    );

    return;
  }

  if (
    message.startsWith(
      "SET_DEVICE_NAME:"
    )
  )
  {
    String value =
      message.substring(
        strlen(
          "SET_DEVICE_NAME:"
        )
      );

    value.trim();

    if (
      value.length() > 31
    )
    {
      value =
        value.substring(
          0,
          31
        );
    }

    if (
      value.length() > 0
    )
    {
      deviceName =
        value;
    }

    return;
  }

  if (
    message.startsWith(
      "SET_UNLOCK_TIME:"
    )
  )
  {
    unsigned long value =
      message
        .substring(
          strlen(
            "SET_UNLOCK_TIME:"
          )
        )
        .toInt();

    if (
      value < 1000
    )
    {
      value =
        1000;
    }

    if (
      value > 30000
    )
    {
      value =
        30000;
    }

    unlockTimeMs =
      value;

    return;
  }

  if (
    message.startsWith(
      "SET_RFID:"
    )
  )
  {
    String value =
      message.substring(
        strlen(
          "SET_RFID:"
        )
      );

    rfidEnabled =
      value == "1";

    return;
  }

  if (
    message.startsWith(
      "SET_FACE:"
    )
  )
  {
    String value =
      message.substring(
        strlen(
          "SET_FACE:"
        )
      );

    faceEnabled =
      value == "1";

    return;
  }

  if (
    message.startsWith(
      "SET_WIFI_SSID:"
    )
  )
  {
    String value =
      message.substring(
        strlen(
          "SET_WIFI_SSID:"
        )
      );

    if (
      value.length() > 32
    )
    {
      value =
        value.substring(
          0,
          32
        );
    }

    configuredWiFiSSID =
      value;

    return;
  }

  if (
    message.startsWith(
      "SET_WIFI_PASSWORD:"
    )
  )
  {
    String value =
      message.substring(
        strlen(
          "SET_WIFI_PASSWORD:"
        )
      );

    if (
      value.length() > 63
    )
    {
      value =
        value.substring(
          0,
          63
        );
    }

    configuredWiFiPassword =
      value;

    return;
  }

  if (
    message == "SAVE"
  )
  {
    saveConfiguration();

    Serial.println();

    Serial.println(
      "========================================"
    );

    Serial.println(
      " CONFIGURATION SAVED"
    );

    Serial.println(
      "========================================"
    );

    Serial.print(
      "Device Name: "
    );

    Serial.println(
      deviceName
    );

    Serial.print(
      "Unlock Time: "
    );

    Serial.print(
      unlockTimeMs
    );

    Serial.println(
      " ms"
    );

    Serial.print(
      "RFID: "
    );

    Serial.println(
      rfidEnabled
        ? "ENABLED"
        : "DISABLED"
    );

    Serial.print(
      "Face Recognition: "
    );

    Serial.println(
      faceEnabled
        ? "ENABLED"
        : "DISABLED"
    );

    Serial.print(
      "Wi-Fi SSID: "
    );

    Serial.println(
      configuredWiFiSSID
    );

    Serial.println(
      "Wi-Fi Password: [HIDDEN]"
    );

    Serial.println(
      "========================================"
    );

    webSocket.sendTXT(
      num,
      "STATUS:Configuration saved permanently to NVS"
    );

    sendConfiguration(
      num
    );

    return;
  }

  if (
    message == "START_NORMAL"
  )
  {
    webSocket.sendTXT(
      num,
      "STATUS:Starting Smart Lock..."
    );

    startNormalRequested =
      true;

    return;
  }
}

void sendConfiguration(
  uint8_t num
)
{
  webSocket.sendTXT(
    num,
    String(
      "CONFIG_DEVICE_NAME:"
    ) +
    deviceName
  );

  webSocket.sendTXT(
    num,
    String(
      "CONFIG_UNLOCK_TIME:"
    ) +
    String(
      unlockTimeMs
    )
  );

  webSocket.sendTXT(
    num,
    String(
      "CONFIG_RFID:"
    ) +
    (
      rfidEnabled
        ? "1"
        : "0"
    )
  );

  webSocket.sendTXT(
    num,
    String(
      "CONFIG_FACE:"
    ) +
    (
      faceEnabled
        ? "1"
        : "0"
    )
  );

  webSocket.sendTXT(
    num,
    String(
      "CONFIG_WIFI_SSID:"
    ) +
    configuredWiFiSSID
  );
}

void startNormalMode()
{
  if (
    normalModeStarted
  )
  {
    return;
  }

  normalModeStarted =
    true;

  configMode =
    false;

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " NORMAL SMART LOCK MODE"
  );

  Serial.println(
    "========================================"
  );

  Serial.print(
    "Device: "
  );

  Serial.println(
    deviceName
  );

  Serial.print(
    "Unlock duration: "
  );

  Serial.print(
    unlockTimeMs
  );

  Serial.println(
    " ms"
  );

  Serial.print(
    "RFID: "
  );

  Serial.println(
    rfidEnabled
      ? "ENABLED"
      : "DISABLED"
  );

  Serial.print(
    "Face Recognition: "
  );

  Serial.println(
    faceEnabled
      ? "ENABLED"
      : "DISABLED"
  );

  setRelay(
    false
  );

  SPI.begin(
    RFID_SCK_PIN,
    RFID_MISO_PIN,
    RFID_MOSI_PIN,
    RFID_SS_PIN
  );

  rfid.PCD_Init();

  pinMode(
    RFID_IRQ_PIN,
    INPUT_PULLUP
  );

  Serial.println(
    "RC522 initialized."
  );

  camSerial.begin(
    CAM_BAUD,
    SERIAL_8N1,
    CAM_RX_PIN,
    CAM_TX_PIN
  );

  camSerial.setTimeout(
    30
  );

  Serial.println(
    "ESP32-CAM UART initialized."
  );

  initializeWatchdog();

  accessEventQueue =
    xQueueCreate(
      10,
      sizeof(AccessEvent)
    );

  doorCommandQueue =
    xQueueCreate(
      5,
      sizeof(DoorCommand)
    );

  ledCommandQueue =
    xQueueCreate(
      5,
      sizeof(LedCommand)
    );

  camTxMutex =
    xSemaphoreCreateMutex();

  if (
    accessEventQueue == nullptr ||
    doorCommandQueue == nullptr ||
    ledCommandQueue == nullptr ||
    camTxMutex == nullptr
  )
  {
    Serial.println(
      "ERROR: FreeRTOS object creation failed."
    );

    while (
      true
    )
    {
      delay(
        1000
      );
    }
  }

  xTaskCreate(
    RFIDTask,
    "RFIDTask",
    4096,
    nullptr,
    2,
    &rfidTaskHandle
  );

  xTaskCreate(
    CameraCommTask,
    "CameraCommTask",
    4096,
    nullptr,
    3,
    nullptr
  );

  xTaskCreate(
    AccessControlTask,
    "AccessControlTask",
    4096,
    nullptr,
    4,
    nullptr
  );

  xTaskCreate(
    DoorControlTask,
    "DoorControlTask",
    3072,
    nullptr,
    4,
    nullptr
  );

  xTaskCreate(
    LEDTask,
    "LEDTask",
    3072,
    nullptr,
    1,
    nullptr
  );

  xTaskCreate(
    ButtonTask,
    "ButtonTask",
    3072,
    nullptr,
    3,
    &buttonTaskHandle
  );

  xTaskCreate(
    SystemMonitorTask,
    "SystemMonitorTask",
    3072,
    nullptr,
    1,
    nullptr
  );

  attachInterrupt(
    digitalPinToInterrupt(
      BUTTON_PIN
    ),
    buttonISR,
    FALLING
  );

  rfidTimer =
    timerBegin(
      1000000
    );

  timerAttachInterrupt(
    rfidTimer,
    &rfidTimerISR
  );

  timerAlarm(
    rfidTimer,
    100000,
    true,
    0
  );

  sendToCamera(
    "S3_READY"
  );

  Serial.println();

  Serial.println(
    "FreeRTOS tasks started."
  );

  Serial.println(
    "Button interrupt enabled."
  );

  Serial.println(
    "RFID timer interrupt enabled."
  );

  Serial.println();

  Serial.println(
    "System ready."
  );

  Serial.println(
    "========================================"
  );
}

void RFIDTask(
  void *parameter
)
{
  Serial.println(
    "[RFIDTask] Started"
  );

  unsigned long lastProcessedTime =
    0;

  while (
    true
  )
  {
    ulTaskNotifyTake(
      pdTRUE,
      portMAX_DELAY
    );

    if (
      !rfidEnabled
    )
    {
      continue;
    }

    if (
      millis() -
      lastProcessedTime <
      RFID_COOLDOWN
    )
    {
      continue;
    }

    if (
      !rfid.PICC_IsNewCardPresent()
    )
    {
      continue;
    }

    if (
      !rfid.PICC_ReadCardSerial()
    )
    {
      continue;
    }

    lastProcessedTime =
      millis();

    Serial.println();

    Serial.println(
      "[RFIDTask] Card detected"
    );

    Serial.print(
      "[RFIDTask] UID: "
    );

    printUID();

    Serial.println();

    AccessEvent event;

    memset(
      &event,
      0,
      sizeof(event)
    );

    strcpy(
      event.source,
      "RFID"
    );

    event.confidence =
      0.0;

    if (
      isAuthorizedCard()
    )
    {
      event.type =
        ACCESS_RFID_GRANTED;

      Serial.println(
        "[RFIDTask] AUTHORIZED"
      );
    }
    else
    {
      event.type =
        ACCESS_RFID_DENIED;

      Serial.println(
        "[RFIDTask] UNAUTHORIZED"
      );
    }

    xQueueSend(
      accessEventQueue,
      &event,
      pdMS_TO_TICKS(100)
    );

    rfid.PICC_HaltA();

    rfid.PCD_StopCrypto1();
  }
}

void CameraCommTask(
  void *parameter
)
{
  esp_err_t watchdogResult =
    esp_task_wdt_add(
      NULL
    );

  Serial.print(
    "[CameraCommTask] Watchdog registration: "
  );

  Serial.println(
    esp_err_to_name(
      watchdogResult
    )
  );

  Serial.println(
    "[CameraCommTask] Started"
  );

  while (
    true
  )
  {
    if (
      camSerial.available()
    )
    {
      String message =
        camSerial.readStringUntil(
          '\n'
        );

      message.trim();

      if (
        message.length() > 0
      )
      {
        Serial.print(
          "[CameraCommTask] RX: "
        );

        Serial.println(
          message
        );

        sendToCamera(
          "S3_ACK"
        );

        if (
          message.startsWith(
            "FACE_AUTHORIZED"
          )
        )
        {
          if (
            !faceEnabled
          )
          {
            Serial.println(
              "[CameraCommTask] Face recognition disabled."
            );
          }
          else
          {
            AccessEvent event;

            memset(
              &event,
              0,
              sizeof(event)
            );

            event.type =
              ACCESS_FACE_GRANTED;

            strcpy(
              event.source,
              "FACE"
            );

            int lastComma =
              message.lastIndexOf(
                ','
              );

            if (
              lastComma >= 0
            )
            {
              event.confidence =
                message
                  .substring(
                    lastComma + 1
                  )
                  .toFloat();
            }
            else
            {
              event.confidence =
                0.0;
            }

            xQueueSend(
              accessEventQueue,
              &event,
              pdMS_TO_TICKS(100)
            );
          }
        }

        else if (
          message.startsWith(
            "FACE_UNKNOWN"
          )
        )
        {
          if (
            faceEnabled
          )
          {
            AccessEvent event;

            memset(
              &event,
              0,
              sizeof(event)
            );

            event.type =
              ACCESS_FACE_DENIED;

            strcpy(
              event.source,
              "FACE"
            );

            event.confidence =
              0.0;

            xQueueSend(
              accessEventQueue,
              &event,
              pdMS_TO_TICKS(100)
            );
          }
        }

        else if (
          message.startsWith(
            "NO_FACE"
          )
        )
        {
          Serial.println(
            "[CameraCommTask] No face detected."
          );
        }
      }
    }

    esp_task_wdt_reset();

    vTaskDelay(
      pdMS_TO_TICKS(5)
    );
  }
}

void ButtonTask(
  void *parameter
)
{
  Serial.println(
    "[ButtonTask] Started"
  );

  while (
    true
  )
  {
    ulTaskNotifyTake(
      pdTRUE,
      portMAX_DELAY
    );

    vTaskDelay(
      pdMS_TO_TICKS(
        BUTTON_DEBOUNCE_TIME
      )
    );

    if (
      digitalRead(
        BUTTON_PIN
      ) == LOW
    )
    {
      Serial.println(
        "[ButtonTask] Manual unlock requested."
      );

      AccessEvent event;

      memset(
        &event,
        0,
        sizeof(event)
      );

      event.type =
        ACCESS_BUTTON_GRANTED;

      strcpy(
        event.source,
        "BUTTON"
      );

      event.confidence =
        0.0;

      xQueueSend(
        accessEventQueue,
        &event,
        pdMS_TO_TICKS(100)
      );
    }
  }
}

void AccessControlTask(
  void *parameter
)
{
  esp_err_t watchdogResult =
    esp_task_wdt_add(
      NULL
    );

  Serial.print(
    "[AccessControlTask] Watchdog registration: "
  );

  Serial.println(
    esp_err_to_name(
      watchdogResult
    )
  );

  Serial.println(
    "[AccessControlTask] Started"
  );

  AccessEvent event;

  while (
    true
  )
  {
    BaseType_t received =
      xQueueReceive(
        accessEventQueue,
        &event,
        pdMS_TO_TICKS(500)
      );

    if (
      received == pdTRUE
    )
    {
      LedCommand ledCommand;
      DoorCommand doorCommand;

      if (
        event.type ==
        ACCESS_RFID_GRANTED
      )
      {
        Serial.println();

        Serial.println(
          "[AccessControlTask] RFID ACCESS GRANTED"
        );

        ledCommand =
          LED_GREEN;

        xQueueSend(
          ledCommandQueue,
          &ledCommand,
          0
        );

        memset(
          &doorCommand,
          0,
          sizeof(doorCommand)
        );

        doorCommand.command =
          DOOR_UNLOCK_COMMAND;

        strcpy(
          doorCommand.source,
          "RFID"
        );

        xQueueSend(
          doorCommandQueue,
          &doorCommand,
          pdMS_TO_TICKS(100)
        );

        sendToCamera(
          "RFID_ACCESS_GRANTED"
        );
      }

      else if (
        event.type ==
        ACCESS_RFID_DENIED
      )
      {
        Serial.println(
          "[AccessControlTask] RFID ACCESS DENIED"
        );

        ledCommand =
          LED_RED;

        xQueueSend(
          ledCommandQueue,
          &ledCommand,
          0
        );

        sendToCamera(
          "RFID_ACCESS_DENIED"
        );
      }

      else if (
        event.type ==
        ACCESS_FACE_GRANTED
      )
      {
        Serial.print(
          "[AccessControlTask] FACE AUTHORIZED"
        );

        Serial.print(
          " | Similarity: "
        );

        Serial.println(
          event.confidence,
          3
        );

        if (
          lastFaceUnlockTime == 0 ||
          millis() -
          lastFaceUnlockTime >=
          FACE_UNLOCK_COOLDOWN
        )
        {
          lastFaceUnlockTime =
            millis();

          ledCommand =
            LED_GREEN;

          xQueueSend(
            ledCommandQueue,
            &ledCommand,
            0
          );

          memset(
            &doorCommand,
            0,
            sizeof(doorCommand)
          );

          doorCommand.command =
            DOOR_UNLOCK_COMMAND;

          strcpy(
            doorCommand.source,
            "FACE"
          );

          xQueueSend(
            doorCommandQueue,
            &doorCommand,
            pdMS_TO_TICKS(100)
          );
        }
        else
        {
          Serial.println(
            "[AccessControlTask] Face cooldown active."
          );
        }
      }

      else if (
        event.type ==
        ACCESS_FACE_DENIED
      )
      {
        Serial.println(
          "[AccessControlTask] UNKNOWN FACE"
        );

        ledCommand =
          LED_RED;

        xQueueSend(
          ledCommandQueue,
          &ledCommand,
          0
        );
      }

      else if (
        event.type ==
        ACCESS_BUTTON_GRANTED
      )
      {
        Serial.println(
          "[AccessControlTask] MANUAL ACCESS"
        );

        memset(
          &doorCommand,
          0,
          sizeof(doorCommand)
        );

        doorCommand.command =
          DOOR_UNLOCK_COMMAND;

        strcpy(
          doorCommand.source,
          "BUTTON"
        );

        xQueueSend(
          doorCommandQueue,
          &doorCommand,
          pdMS_TO_TICKS(100)
        );

        sendToCamera(
          "MANUAL_UNLOCK"
        );
      }
    }

    esp_task_wdt_reset();
  }
}

void DoorControlTask(
  void *parameter
)
{
  esp_err_t watchdogResult =
    esp_task_wdt_add(
      NULL
    );

  Serial.print(
    "[DoorControlTask] Watchdog registration: "
  );

  Serial.println(
    esp_err_to_name(
      watchdogResult
    )
  );

  Serial.println(
    "[DoorControlTask] Started"
  );

  bool doorUnlocked =
    false;

  unsigned long unlockedAt =
    0;

  DoorCommand command;

  bool watchdogTestTriggered =
    false;

  while (
    true
  )
  {
    if (
      WATCHDOG_TEST_MODE &&
      !watchdogTestTriggered &&
      millis() >=
      WATCHDOG_TEST_START_MS
    )
    {
      watchdogTestTriggered =
        true;

      Serial.println();

      Serial.println(
        "========================================"
      );

      Serial.println(
        " WATCHDOG FAILURE TEST"
      );

      Serial.println(
        "========================================"
      );

      Serial.println(
        "DoorControlTask will stop feeding WDT."
      );

      Serial.println(
        "Watchdog should detect the failure."
      );

      Serial.println(
        "ESP32 should restart automatically."
      );

      Serial.println(
        "========================================"
      );

      Serial.flush();

      vTaskDelay(
        pdMS_TO_TICKS(
          WATCHDOG_TEST_STALL_MS
        )
      );
    }

    if (
      xQueueReceive(
        doorCommandQueue,
        &command,
        pdMS_TO_TICKS(50)
      ) == pdTRUE
    )
    {
      if (
        command.command ==
        DOOR_UNLOCK_COMMAND
      )
      {
        Serial.println();

        Serial.print(
          "[DoorControlTask] Unlock source: "
        );

        Serial.println(
          command.source
        );

        if (
          !doorUnlocked
        )
        {
          setRelay(
            true
          );

          doorUnlocked =
            true;

          Serial.println(
            "[DoorControlTask] Relay: ON"
          );

          Serial.println(
            "[DoorControlTask] Door: UNLOCKED"
          );

          sendToCamera(
            "DOOR_UNLOCKED"
          );
        }
        else
        {
          Serial.println(
            "[DoorControlTask] Door already unlocked."
          );
        }

        unlockedAt =
          millis();
      }
    }

    if (
      doorUnlocked &&
      millis() -
      unlockedAt >=
      unlockTimeMs
    )
    {
      setRelay(
        false
      );

      doorUnlocked =
        false;

      Serial.println();

      Serial.println(
        "[DoorControlTask] Relay: OFF"
      );

      Serial.println(
        "[DoorControlTask] Door: LOCKED"
      );

      sendToCamera(
        "DOOR_LOCKED"
      );
    }

    esp_task_wdt_reset();
  }
}

void LEDTask(
  void *parameter
)
{
  Serial.println(
    "[LEDTask] Started"
  );

  LedCommand command;

  bool ledOn =
    false;

  unsigned long ledStarted =
    0;

  while (
    true
  )
  {
    if (
      xQueueReceive(
        ledCommandQueue,
        &command,
        pdMS_TO_TICKS(50)
      ) == pdTRUE
    )
    {
#ifdef RGB_BUILTIN

      if (
        command ==
        LED_GREEN
      )
      {
        rgbLedWrite(
          RGB_BUILTIN,
          0,
          50,
          0
        );
      }

      else if (
        command ==
        LED_RED
      )
      {
        rgbLedWrite(
          RGB_BUILTIN,
          50,
          0,
          0
        );
      }

#endif

      ledOn =
        true;

      ledStarted =
        millis();
    }

    if (
      ledOn &&
      millis() -
      ledStarted >=
      LED_DISPLAY_TIME
    )
    {
#ifdef RGB_BUILTIN

      rgbLedWrite(
        RGB_BUILTIN,
        0,
        0,
        0
      );

#endif

      ledOn =
        false;
    }
  }
}

void SystemMonitorTask(
  void *parameter
)
{
  Serial.println(
    "[SystemMonitorTask] Started"
  );

  while (
    true
  )
  {
    Serial.print(
      "[System] Free heap: "
    );

    Serial.print(
      ESP.getFreeHeap()
    );

    Serial.println(
      " bytes"
    );

    vTaskDelay(
      pdMS_TO_TICKS(
        10000
      )
    );
  }
}

void printUID()
{
  for (
    byte i = 0;
    i < rfid.uid.size;
    i++
  )
  {
    if (
      rfid.uid.uidByte[i] <
      0x10
    )
    {
      Serial.print(
        "0"
      );
    }

    Serial.print(
      rfid.uid.uidByte[i],
      HEX
    );

    if (
      i <
      rfid.uid.size - 1
    )
    {
      Serial.print(
        ":"
      );
    }
  }
}

bool isAuthorizedCard()
{
  if (
    rfid.uid.size !=
    AUTHORIZED_UID_LENGTH
  )
  {
    return false;
  }

  for (
    byte i = 0;
    i <
    AUTHORIZED_UID_LENGTH;
    i++
  )
  {
    if (
      rfid.uid.uidByte[i] !=
      authorizedUID[i]
    )
    {
      return false;
    }
  }

  return true;
}