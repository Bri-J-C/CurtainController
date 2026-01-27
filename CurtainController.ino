#include <esp_netif.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <MycilaWebSerial.h>
#include <stdarg.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// Motor Configuration
int steps_per_revolution = 2000;

// Watchdog timeout (increased to handle slow movements)
const int WDT_TIMEOUT = 180;  // 3 minutes

// Pin definitions for A4988 stepper driver - ESP32-C3 SUPER MINI
const int ENABLE_PIN = 0;        // Enable (LOW = enabled, HIGH = disabled)
const int MS1_PIN = 1;           // Microstepping bit 1
const int MS2_PIN = 2;           // Microstepping bit 2
const int MS3_PIN = 3;           // Microstepping bit 3
const int RESET_PIN = 4;         // Reset (normally HIGH)
const int DIR_PIN = 10;          // Direction control
const int SLEEP_PIN = 7;         // Sleep mode (LOW = sleep, HIGH = awake)
const int STATUS_LED = 8;        // Onboard blue LED (inverted logic)
const int RESET_BUTTON_PIN = 9;  // Onboard BOOT button
const int STEP_PIN = 6;          // Step pulse

// Motor control
int current_position = 0;
int target_position = 0;
bool is_moving = false;
unsigned long last_step_time = 0;
unsigned long movement_start_time = 0;
const unsigned long MOVEMENT_TIMEOUT = 120000;  // 2 minute timeout
int steps_since_last_save = 0;
const int STEPS_BETWEEN_SAVES = 50;
unsigned long last_position_report = 0;
const unsigned long POSITION_REPORT_INTERVAL = 500;  // Report every 500ms

// Configurable parameters
int step_delay_us = 2000;  // (microseconds between steps)
int stepping_mode = 4;     // Default to 1/16 microstepping
bool motor_sleeping = true;
unsigned long motor_sleep_timeout = 30000;
unsigned long last_motor_activity = 0;

// A4988 Microstepping modes
const uint8_t MICROSTEP_CONFIG[5][3] = {
  {LOW,  LOW,  LOW},   // Mode 0: Full step
  {HIGH, LOW,  LOW},   // Mode 1: Half step
  {LOW,  HIGH, LOW},   // Mode 2: Quarter step
  {HIGH, HIGH, LOW},   // Mode 3: Eighth step
  {HIGH, HIGH, HIGH}   // Mode 4: Sixteenth step
};

const char* MICROSTEP_NAMES[5] = {
  "Full (1/1)",
  "Half (1/2)",
  "Quarter (1/4)",
  "Eighth (1/8)",
  "Sixteenth (1/16)"
};

// Reset button with debouncing
unsigned long button_press_start = 0;
unsigned long last_button_change = 0;
bool button_state = HIGH;
bool last_stable_state = HIGH;
bool reset_in_progress = false;
bool ap_triggered = false;
const unsigned long BUTTON_DEBOUNCE_MS = 50;
const unsigned long AP_HOLD_MIN = 3000;       // 3 seconds min for config portal
const unsigned long AP_HOLD_MAX = 5000;       // 5 seconds max for config portal
const unsigned long RESET_HOLD_MIN = 10000;   // 10 seconds min for factory reset
const unsigned long RESET_HOLD_MAX = 13000;   // 13 seconds max for factory reset

// LED control
bool led_manual_control = false;  // If true, LED commands override automatic behavior
bool led_desired_state = HIGH;    // Desired LED state (HIGH = off, LOW = on)

// MQTT reconnection with exponential backoff
unsigned long last_mqtt_attempt = 0;
int mqtt_retry_delay = 2000;
const int MAX_MQTT_RETRY_DELAY = 60000;

// WiFi reconnection state machine
enum WiFiReconnectState {
  WIFI_CONNECTED,
  WIFI_DISCONNECTED,
  WIFI_RECONNECTING
};
WiFiReconnectState wifi_state = WIFI_CONNECTED;
unsigned long wifi_reconnect_start = 0;
const unsigned long WIFI_RECONNECT_TIMEOUT = 30000;

volatile bool ws_command_pending = false;

// Network
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;
AsyncWebServer server(80);
WebSerial WebSerial;
String device_hostname;
String mqtt_server;
int mqtt_port;
String mqtt_user;
String mqtt_password;
String mqtt_command_topic;
String mqtt_stat_topic;
String mqtt_position_topic;
String mqtt_availability_topic;
String ws_pending_command;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void setup_a4988();
void setup_wifi_manager();
bool check_button_hold_at_boot(unsigned long hold_time_ms);
void start_config_portal();
void setup_mqtt();
void setup_ota();
void setup_webserial();
void setup_mdns();
void connect_mqtt();
void handle_movement();
void handle_wifi_reconnection();
void check_reset_button();
void stop_motor();
void stop_movement(const char* reason);
void save_position();
void publish_status(const char* status);
void publish_position();
void publish_ha_discovery(bool force = false);
void process_command(const String& command);
void set_microstepping_mode(int mode);
void wake_motor();
void sleep_motor();
void step_motor();
void start_movement(int target);

// Command handlers
void cmd_open(const String& param);
void cmd_close(const String& param);
void cmd_stop(const String& param);
void cmd_position(const String& param);
void cmd_speed(const String& param);
void cmd_mode(const String& param);
void cmd_steps(const String& param);
void cmd_setposition(const String& param);
void cmd_sleep(const String& param);
void cmd_resetdriver(const String& param);
void cmd_hadiscovery(const String& param);
void cmd_config(const String& param);
void cmd_status(const String& param);
void cmd_restart(const String& param);
void cmd_help(const String& param);
void cmd_ledon(const String& param);
void cmd_ledoff(const String& param);


// ============================================================================
// WEBSERIAL HELPERS (Option A)
// ============================================================================

void ws_println(const String& s) {
  WebSerial.println(s);   
  delay(1);               // small yield for async send
}

void ws_print(const String& s) {
  WebSerial.print(s);     
  delay(1);
}

void ws_printf(const char* fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  WebSerial.print(buffer); 
  delay(1);
}



// ============================================================================
// MOTOR CONTROL
// ============================================================================

void setup_a4988() {
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH);
  pinMode(SLEEP_PIN, OUTPUT);
  digitalWrite(SLEEP_PIN, LOW);

  delay(10);

  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(MS1_PIN, OUTPUT);
  pinMode(MS2_PIN, OUTPUT);
  pinMode(MS3_PIN, OUTPUT);

  digitalWrite(DIR_PIN, LOW);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(RESET_PIN, HIGH);

  set_microstepping_mode(stepping_mode);
}

void set_microstepping_mode(int mode) {
  if (mode < 0 || mode > 4) return;

  digitalWrite(MS1_PIN, MICROSTEP_CONFIG[mode][0]);
  digitalWrite(MS2_PIN, MICROSTEP_CONFIG[mode][1]);
  digitalWrite(MS3_PIN, MICROSTEP_CONFIG[mode][2]);

  stepping_mode = mode;
}

void stop_motor() {
  digitalWrite(ENABLE_PIN, HIGH);
}

void wake_motor() {
  if (motor_sleeping) {
    digitalWrite(SLEEP_PIN, HIGH);
    digitalWrite(ENABLE_PIN, LOW);
    delayMicroseconds(2);
    motor_sleeping = false;
  }
  last_motor_activity = millis();
}

void sleep_motor() {
  if (!motor_sleeping && !is_moving) {
    digitalWrite(ENABLE_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(SLEEP_PIN, LOW);
    motor_sleeping = true;
  }
}

void step_motor() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(2);
  digitalWrite(STEP_PIN, LOW);
  last_motor_activity = millis();
}

void save_position() {
  preferences.putInt("position", current_position);
}

void start_movement(int target) {
  if (is_moving) return;

  target_position = constrain(target, 0, steps_per_revolution);

  if (current_position == target_position) return;

  wake_motor();

  if (target_position > current_position) {
    digitalWrite(DIR_PIN, HIGH);
    publish_status("opening");
  } else {
    digitalWrite(DIR_PIN, LOW);
    publish_status("closing");
  }

  delayMicroseconds(2);

  is_moving = true;
  last_step_time = micros();
  movement_start_time = millis();
  steps_since_last_save = 0;
  last_position_report = 0;  // Force immediate position report
  publish_position();        // Report starting position to HA
}

void stop_movement(const char* reason) {
  is_moving = false;
  stop_motor();
  save_position();
  publish_position();

  const char* status;
  if (strcmp(reason, "Complete") == 0) {
    // Movement completed naturally - report position
    if (current_position >= steps_per_revolution) {
      status = "open";
    } else if (current_position <= 0) {
      status = "closed";
    } else {
      status = "open";  // Partial position
    }
  } else {
    // Manually stopped (User command, Timeout, OTA)
    status = "stopped";
  }
  publish_status(status);
}

void handle_movement() {
  if (!is_moving) return;

  if (millis() - movement_start_time > MOVEMENT_TIMEOUT) {
    publish_status("error_timeout");
    stop_movement("Timeout");
    return;
  }

  unsigned long now = micros();
  if (now - last_step_time >= (unsigned long)step_delay_us) {
    last_step_time = now;

    // Update position
    if (current_position < target_position) {
      current_position++;
    } else {
      current_position--;
    }

    current_position = constrain(current_position, 0, steps_per_revolution);

    step_motor();

    // Periodic save
    if (++steps_since_last_save >= STEPS_BETWEEN_SAVES) {
      save_position();
      steps_since_last_save = 0;
    }

    // Check if done
    if (current_position == target_position) {
      stop_movement("Complete");
    }
  }

  // Report position while moving (outside step timing for reliable 500ms updates)
  if (millis() - last_position_report >= POSITION_REPORT_INTERVAL) {
    publish_position();
    last_position_report = millis();
  }
}

// ============================================================================
// MQTT
// ============================================================================

void publish_status(const char* status) {
  if (client.connected()) {
    client.publish(mqtt_stat_topic.c_str(), status, true);
  }
}

void publish_position() {
  if (client.connected()) {
    int percentage = (current_position * 100) / steps_per_revolution;
    char pos_str[8];
    snprintf(pos_str, sizeof(pos_str), "%d", percentage);
    client.publish(mqtt_position_topic.c_str(), pos_str, true);
  }
}

void publish_ha_discovery(bool force) {
  if (!client.connected()) return;

  if (!force) {
    bool already_published = preferences.getBool("ha_disc_done", false);
    if (already_published) return;
  }

  String discovery_topic = "homeassistant/cover/" + device_hostname + "/config";

  StaticJsonDocument<1536> doc;

  doc["name"] = nullptr;  // Inherit name from device
  doc["unique_id"] = "curtain_" + device_hostname;
  doc["object_id"] = device_hostname;

  doc["command_topic"] = mqtt_command_topic;
  doc["state_topic"] = mqtt_stat_topic;
  doc["position_topic"] = mqtt_position_topic;
  doc["set_position_topic"] = mqtt_command_topic;
  doc["availability_topic"] = mqtt_availability_topic;

  doc["payload_open"] = "open";
  doc["payload_close"] = "close";
  doc["payload_stop"] = "stop";
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  doc["state_open"] = "open";
  doc["state_opening"] = "opening";
  doc["state_closed"] = "closed";
  doc["state_closing"] = "closing";
  doc["state_stopped"] = "stopped";

  doc["position_open"] = 100;
  doc["position_closed"] = 0;

  doc["optimistic"] = false;
  doc["qos"] = 1;
  doc["retain"] = true;
  doc["device_class"] = "curtain";

  JsonObject device = doc.createNestedObject("device");
  JsonArray identifiers = device.createNestedArray("identifiers");
  identifiers.add("curtain_" + WiFi.macAddress());

  device["name"] = device_hostname;
  device["model"] = "CurtainControllerv1";
  device["manufacturer"] = "GuyWithAComputer";
  device["sw_version"] = "4.3";
  device["configuration_url"] = "http://" + WiFi.localIP().toString() + "/setup";

  String json;
  serializeJson(doc, json);

  Serial.printf("HA Discovery payload size: %d bytes\n", json.length());
  ws_printf("HA Discovery payload size: %d bytes\n", json.length());

  if (client.publish(discovery_topic.c_str(), json.c_str(), true)) {
    preferences.putBool("ha_disc_done", true);
    Serial.println("HA Discovery published successfully");
    ws_println("HA Discovery published successfully");
  } else {
    Serial.println("HA Discovery publish FAILED");
    ws_println("HA Discovery publish FAILED");
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  process_command(msg);
}

void connect_mqtt() {
  if (client.connected()) return;

  if (millis() - last_mqtt_attempt < mqtt_retry_delay) return;

  last_mqtt_attempt = millis();

  String client_id = device_hostname + "_" + WiFi.macAddress();
  client_id.replace(":", "");

  bool connected;
  if (mqtt_user.length() > 0) {
    connected = client.connect(client_id.c_str(), mqtt_user.c_str(), mqtt_password.c_str(),
                              mqtt_availability_topic.c_str(), 1, true, "offline");
  } else {
    connected = client.connect(client_id.c_str(), mqtt_availability_topic.c_str(), 1, true, "offline");
  }

  if (connected) {
    client.subscribe(mqtt_command_topic.c_str());
    client.publish(mqtt_availability_topic.c_str(), "online", true);
    publish_position();
    publish_status(current_position >= steps_per_revolution ? "open" :
                   current_position <= 0 ? "closed" : "open");
    publish_ha_discovery();
    mqtt_retry_delay = 2000;
  } else {
    mqtt_retry_delay = min(mqtt_retry_delay * 2, MAX_MQTT_RETRY_DELAY);
  }
}

void setup_mqtt() {
  mqtt_server = preferences.getString("mqtt_server", "192.168.1.100");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "your_mqtt_user");
  mqtt_password = preferences.getString("mqtt_pass", "your_mqtt_password");

  String mqtt_root_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");
  mqtt_command_topic = mqtt_root_topic + "/cmd";
  mqtt_stat_topic = mqtt_root_topic + "/status";
  mqtt_position_topic = mqtt_root_topic + "/position";
  mqtt_availability_topic = mqtt_root_topic + "/availability";

  client.setBufferSize(1536);
  client.setServer(mqtt_server.c_str(), mqtt_port);
  client.setCallback(mqtt_callback);

  connect_mqtt();
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

struct Command {
  const char* name;
  void (*handler)(const String& param);
};

void cmd_open(const String& param) {
  start_movement(steps_per_revolution);
  String msg = "Opening curtain...";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_close(const String& param) {
  start_movement(0);
  String msg = "Closing curtain...";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_stop(const String& param) {
  stop_movement("User command");
  String msg = "Stopped";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_position(const String& param) {
  int pos = param.toInt();
  if (pos >= 0 && pos <= steps_per_revolution) {
    start_movement(pos);
    Serial.printf("Moving to position %d\n", pos);
    ws_printf("Moving to position %d\n", pos);
  } else {
    Serial.printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    ws_printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
  }
}

void cmd_speed(const String& param) {
  int value = param.toInt();
  if (value >= 2 && value <= 10000) {
    step_delay_us = value;
    preferences.putInt("step_delay", step_delay_us);
    Serial.printf("Speed: %d us/step\n", step_delay_us);
    ws_printf("Speed: %d us/step\n", step_delay_us);
  } else {
    String msg = "[ERROR] Speed must be 2-10000 us";
    Serial.println(msg);
    ws_println(msg);
  }
}

void cmd_mode(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= 4) {
    set_microstepping_mode(value);
    preferences.putInt("step_mode", stepping_mode);
    Serial.printf("Mode: %s\n", MICROSTEP_NAMES[stepping_mode]);
    ws_printf("Mode: %s\n", MICROSTEP_NAMES[stepping_mode]);
  } else {
    String msg = "[ERROR] Mode must be 0-4";
    Serial.println(msg);
    ws_println(msg);
  }
}

void cmd_steps(const String& param) {
  int value = param.toInt();
  if (value > 0 && value <= 500000) {
    steps_per_revolution = value;
    preferences.putInt("steps_per_rev", steps_per_revolution);
    Serial.printf("Steps/rev: %d\n", steps_per_revolution);
    ws_printf("Steps/rev: %d\n", steps_per_revolution);
  } else {
    String msg = "[ERROR] Steps must be 1-500000";
    Serial.println(msg);
    ws_println(msg);
  }
}

void cmd_setposition(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= steps_per_revolution) {
    current_position = value;
    save_position();
    publish_position();
    Serial.printf("Position reset to %d\n", current_position);
    ws_printf("Position reset to %d\n", current_position);
  } else {
    Serial.printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    ws_printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
  }
}

void cmd_sleep(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= 300000) {
    motor_sleep_timeout = value;
    preferences.putULong("sleep_timeout", motor_sleep_timeout);
    Serial.printf("Sleep timeout: %lu ms\n", motor_sleep_timeout);
    ws_printf("Sleep timeout: %lu ms\n", motor_sleep_timeout);
  } else {
    String msg = "[ERROR] Sleep must be 0-300000 ms";
    Serial.println(msg);
    ws_println(msg);
  }
}

void cmd_resetdriver(const String& param) {
  digitalWrite(RESET_PIN, LOW);
  delayMicroseconds(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(2);
  String msg = "Driver reset complete";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_hadiscovery(const String& param) {
  preferences.putBool("ha_disc_done", false);
  publish_ha_discovery(true);  // Force republish
  String msg = "HA discovery republished";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_config(const String& param) {
  String mqtt_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");

  String output = "\n=== Configuration ===\n";
  output += "Hostname: " + device_hostname + "\n";
  output += "IP: " + WiFi.localIP().toString() + "\n";
  output += "SSID: " + WiFi.SSID() + "\n";
  output += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
  output += "MAC: " + WiFi.macAddress() + "\n";
  output += "MQTT: " + mqtt_server + ":" + String(mqtt_port) + "\n";
  output += "MQTT User: " + (mqtt_user.length() > 0 ? mqtt_user : "(none)") + "\n";
  output += "MQTT Topic: " + mqtt_topic + "\n";
  output += "Speed: " + String(step_delay_us) + " us/step\n";
  output += "Mode: " + String(MICROSTEP_NAMES[stepping_mode]) + "\n";
  output += "Steps/Rev: " + String(steps_per_revolution) + "\n";
  output += "Sleep Timeout: " + String(motor_sleep_timeout) + " ms\n";
  output += "Setup: http://" + WiFi.localIP().toString() + "/setup\n";
  output += "====================\n";

  Serial.print(output);
  ws_print(output);
}

void cmd_restart(const String& param) {
  String msg = "Restarting...";
  Serial.println(msg);
  ws_println(msg);
  delay(1000);
  ESP.restart();
}

void cmd_help(const String& param) {
  String help = "\n=== Commands ===\n";
  help += "open          - Open curtain\n";
  help += "close         - Close curtain\n";
  help += "stop          - Stop movement\n";
  help += "position <n>  - Move to step position\n";
  help += "speed <us>    - Set step delay (2-10000)\n";
  help += "mode <0-4>    - Set microstepping mode\n";
  help += "steps <n>     - Set steps per revolution\n";
  help += "setposition <n> - Reset position counter\n";
  help += "sleep <ms>    - Set sleep timeout\n";
  help += "status        - Show current status\n";
  help += "config        - Show configuration\n";
  help += "hadiscovery   - Republish HA discovery\n";
  help += "resetdriver   - Reset A4988 driver\n";
  help += "restart       - Reboot device\n";
  help += "ledon/ledoff  - Control LED\n";
  help += "================\n";
  help += "Setup: http://" + WiFi.localIP().toString() + "/setup\n";

  Serial.print(help);
  ws_print(help);
}

void cmd_status(const String& param) {
  String output = "\n=== Status ===\n";
  output += "Position: " + String(current_position) + " (" + String((current_position * 100) / steps_per_revolution) + "%)\n";
  output += "Moving: " + String(is_moving ? "Yes" : "No") + "\n";
  if (is_moving) {
    output += "Target: " + String(target_position) + "\n";
  }
  output += "Motor: " + String(motor_sleeping ? "Sleeping" : "Awake") + "\n";
  output += "MQTT: " + String(client.connected() ? "Connected" : "Disconnected") + "\n";
  output += "Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  output += "Uptime: " + String(millis() / 1000) + "s\n";
  output += "==============\n";

  Serial.print(output);
  ws_print(output);
}

void cmd_ledon(const String& param) {
  led_manual_control = true;
  led_desired_state = LOW;
  digitalWrite(STATUS_LED, led_desired_state);
  String msg = "LED ON";
  Serial.println(msg);
  ws_println(msg);
}

void cmd_ledoff(const String& param) {
  led_manual_control = true;
  led_desired_state = HIGH;
  digitalWrite(STATUS_LED, led_desired_state);
  String msg = "LED OFF";
  Serial.println(msg);
  ws_println(msg);
}

const Command commands[] = {
  {"open", cmd_open},
  {"close", cmd_close},
  {"stop", cmd_stop},
  {"position ", cmd_position},
  {"speed ", cmd_speed},
  {"mode ", cmd_mode},
  {"steps ", cmd_steps},
  {"setposition ", cmd_setposition},
  {"sleep ", cmd_sleep},
  {"resetdriver", cmd_resetdriver},
  {"hadiscovery", cmd_hadiscovery},
  {"config", cmd_config},
  {"status", cmd_status},
  {"restart", cmd_restart},
  {"help", cmd_help},
  {"ledon", cmd_ledon},
  {"ledoff", cmd_ledoff},
  {nullptr, nullptr}
};

void process_command(const String& cmd) {
  String command = cmd;
  command.trim();
  command.toLowerCase();

  if (command.length() == 0) return;

  const char* input = command.c_str();
  size_t input_len = command.length();

  for (int i = 0; commands[i].name != nullptr; i++) {
    const char* name = commands[i].name;
    size_t name_len = strlen(name);

    if (name[name_len - 1] == ' ') {
      // Command with parameter (space-separated)
      if (input_len >= name_len && strncmp(input, name, name_len) == 0) {
        commands[i].handler(command.substring(name_len));
        return;
      }
    } else {
      // Exact match command
      if (input_len == name_len && strcmp(input, name) == 0) {
        commands[i].handler("");
        return;
      }
    }
  }

  // Check if it's a plain number (HA set_position_topic sends percentage directly)
  bool is_numeric = true;
  for (size_t i = 0; i < command.length(); i++) {
    if (!isDigit(command.charAt(i))) {
      is_numeric = false;
      break;
    }
  }

  if (is_numeric && command.length() > 0) {
    int percentage = command.toInt();
    if (percentage >= 0 && percentage <= 100) {
      // Convert percentage to steps
      int target_steps = (percentage * steps_per_revolution) / 100;
      start_movement(target_steps);
      Serial.printf("HA position: %d%% -> step %d\n", percentage, target_steps);
      ws_printf("HA position: %d%% -> step %d\n", percentage, target_steps);
      return;
    }
  }

  String msg = "[ERROR] Unknown: " + command;
  Serial.println(msg);
  ws_println(msg);
}

// ============================================================================
// WEBSERIAL
// ============================================================================

const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Curtain Controller Setup</title>
  <style>
    body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}
    h1{color:#4CAF50}
    form{max-width:400px}
    label{display:block;margin-top:10px;font-weight:bold}
    input{width:100%;padding:8px;margin-top:4px;box-sizing:border-box;border-radius:4px;border:1px solid #444;background:#333;color:#fff}
    button{margin-top:20px;padding:12px 24px;background:#4CAF50;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:16px}
    button:hover{background:#45a049}
    .info{color:#888;font-size:12px}
  </style>
</head>
<body>
  <h1>Curtain Controller Setup</h1>
  <form action="/save" method="POST">
    <label>Hostname</label>
    <input name="hostname" value="%HOSTNAME%">
    <label>MQTT Server</label>
    <input name="mqtt_server" value="%MQTT_SERVER%">
    <label>MQTT Port</label>
    <input name="mqtt_port" type="number" value="%MQTT_PORT%">
    <label>MQTT Username</label>
    <input name="mqtt_user" value="%MQTT_USER%">
    <label>MQTT Password</label>
    <input name="mqtt_pass" type="password" value="%MQTT_PASS%">
    <label>MQTT Root Topic</label>
    <input name="mqtt_topic" value="%MQTT_TOPIC%">
    <p class="info">Creates: /cmd, /status, /position</p>
    <label>Steps per Revolution</label>
    <input name="steps" type="number" value="%STEPS%">
    <label>OTA Password</label>
    <input name="ota_pass" type="password" placeholder="Leave blank to keep current">
    <p class="info">Device will reboot after saving.</p>
    <button type="submit">Save &amp; Reboot</button>
  </form>
</body>
</html>
)rawliteral";

void setup_webserial() {
  // Setup page with prefilled values
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = String(SETUP_HTML);
    html.replace("%HOSTNAME%", preferences.getString("hostname", "NewCurtains"));
    html.replace("%MQTT_SERVER%", preferences.getString("mqtt_server", "192.168.1.100"));
    html.replace("%MQTT_PORT%", String(preferences.getInt("mqtt_port", 1883)));
    html.replace("%MQTT_USER%", preferences.getString("mqtt_user", ""));
    html.replace("%MQTT_PASS%", preferences.getString("mqtt_pass", ""));
    html.replace("%MQTT_TOPIC%", preferences.getString("mqtt_root_topic", "home/room/curtains"));
    html.replace("%STEPS%", String(preferences.getInt("steps_per_rev", 2000)));
    request->send(200, "text/html", html);
  });

  // Save handler
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("hostname", true)) {
      preferences.putString("hostname", request->getParam("hostname", true)->value());
    }
    if (request->hasParam("mqtt_server", true)) {
      preferences.putString("mqtt_server", request->getParam("mqtt_server", true)->value());
    }
    if (request->hasParam("mqtt_port", true)) {
      preferences.putInt("mqtt_port", request->getParam("mqtt_port", true)->value().toInt());
    }
    if (request->hasParam("mqtt_user", true)) {
      preferences.putString("mqtt_user", request->getParam("mqtt_user", true)->value());
    }
    if (request->hasParam("mqtt_pass", true)) {
      preferences.putString("mqtt_pass", request->getParam("mqtt_pass", true)->value());
    }
    if (request->hasParam("mqtt_topic", true)) {
      preferences.putString("mqtt_root_topic", request->getParam("mqtt_topic", true)->value());
    }
    if (request->hasParam("steps", true)) {
      int steps = request->getParam("steps", true)->value().toInt();
      if (steps > 0) preferences.putInt("steps_per_rev", steps);
    }
    if (request->hasParam("ota_pass", true)) {
      String ota = request->getParam("ota_pass", true)->value();
      if (ota.length() > 0) preferences.putString("ota_pass", ota);
    }
    preferences.putBool("ha_disc_done", false);

    request->send(200, "text/html", "<html><body><h1>Saved!</h1><p>Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
  });

  WebSerial.onMessage([](uint8_t *data, size_t len) {
      String command;
      command.reserve(len + 1);

      for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c >= 32 && c <= 126) command += c;
      }

      command.trim();
      if (command.length() == 0) return;

      ws_println("> " + command);

      ws_pending_command = command;
      ws_command_pending = true;
  });

  WebSerial.begin(&server);
  server.begin();
}


// ============================================================================
// WIFI & NETWORK
// ============================================================================

bool check_button_hold_at_boot(unsigned long hold_time_ms) {
  // Check if button is held at boot for specified time
  unsigned long start = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (millis() - start >= hold_time_ms) {
      // Indicate with LED blink
      for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED, LOW);
        delay(100);
        digitalWrite(STATUS_LED, HIGH);
        delay(100);
      }
      return true;
    }
    delay(10);
  }
  return false;
}

void start_config_portal() {
  String mqtt_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");
  String ota_pass = preferences.getString("ota_pass", "");

  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(60);

  bool shouldSave = false;
  wm.setSaveConfigCallback([&shouldSave]() { shouldSave = true; });

  WiFiManagerParameter p_hostname("hostname", "Device Hostname", device_hostname.c_str(), 40);
  WiFiManagerParameter p_server("server", "MQTT Server IP", mqtt_server.c_str(), 40);
  WiFiManagerParameter p_port("port", "MQTT Port", String(mqtt_port).c_str(), 6);
  WiFiManagerParameter p_user("user", "MQTT Username", mqtt_user.c_str(), 40);
  WiFiManagerParameter p_pass("password", "MQTT Password", mqtt_password.c_str(), 40);
  WiFiManagerParameter p_topic("mqtt_root_topic", "MQTT Root Topic", mqtt_topic.c_str(), 80);
  WiFiManagerParameter p_ota("ota_pass", "OTA Password", ota_pass.c_str(), 40);
  WiFiManagerParameter p_steps("steps_per_rev", "Steps per Revolution", String(steps_per_revolution).c_str(), 8);

  wm.addParameter(&p_hostname);
  wm.addParameter(&p_server);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.addParameter(&p_topic);
  wm.addParameter(&p_ota);
  wm.addParameter(&p_steps);

  Serial.println("Starting AP: Curtain-Setup");
  if (!wm.autoConnect("Curtain-Setup", "12345678")) {
    Serial.println("Config portal timeout, restarting...");
    delay(1000);
    ESP.restart();
  }
  Serial.println("WiFi connected via portal");

  if (shouldSave) {
    preferences.putString("hostname", p_hostname.getValue());
    preferences.putString("mqtt_server", p_server.getValue());
    preferences.putInt("mqtt_port", String(p_port.getValue()).toInt());
    preferences.putString("mqtt_user", p_user.getValue());
    preferences.putString("mqtt_pass", p_pass.getValue());
    preferences.putString("mqtt_root_topic", p_topic.getValue());
    int steps_val = String(p_steps.getValue()).toInt();
    if (steps_val > 0) preferences.putInt("steps_per_rev", steps_val);
    String ota_new = String(p_ota.getValue());
    if (ota_new.length() > 0) preferences.putString("ota_pass", ota_new);
    preferences.putBool("ha_disc_done", false);
    delay(1000);
    ESP.restart();
  }
}

void setup_wifi_manager() {
  device_hostname = preferences.getString("hostname", "NewCurtain");
  mqtt_server = preferences.getString("mqtt_server", "192.168.1.100");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "your_mqtt_user");
  mqtt_password = preferences.getString("mqtt_pass", "your_mqtt_password");

  Serial.println("Starting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(device_hostname.c_str());

  // Check if button held for 3 seconds at boot to force config portal
  bool force_portal = check_button_hold_at_boot(3000);

  // Check if we have saved WiFi credentials
  bool has_wifi_config = WiFi.SSID().length() > 0;

  if (force_portal || !has_wifi_config) {
    // Start config portal
    if (force_portal) {
      Serial.println("Button held - starting config portal");
    } else {
      Serial.println("No WiFi config - starting config portal");
    }
    start_config_portal();
  } else {
    // Try to connect with saved credentials, retry indefinitely
    Serial.println("Connecting to saved WiFi...");
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      attempts++;

      // Blink LED while connecting
      digitalWrite(STATUS_LED, (attempts % 2) ? LOW : HIGH);

      // Every 30 seconds, try reconnecting
      if (attempts >= 60) {
        Serial.println("\nRetrying WiFi connection...");
        WiFi.disconnect();
        delay(1000);
        WiFi.begin();
        attempts = 0;
      }
    }
    digitalWrite(STATUS_LED, HIGH);  // LED off when connected
    Serial.println("\nWiFi connected");
  }

  wifi_state = WIFI_CONNECTED;
}

void handle_wifi_reconnection() {
  switch (wifi_state) {
    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        wifi_state = WIFI_DISCONNECTED;
      }
      break;

    case WIFI_DISCONNECTED:
      WiFi.disconnect();
      WiFi.setHostname(device_hostname.c_str());
      WiFi.begin();
      wifi_reconnect_start = millis();
      wifi_state = WIFI_RECONNECTING;
      break;

    case WIFI_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        wifi_state = WIFI_CONNECTED;
        MDNS.end();
        setup_mdns();
      } else if (millis() - wifi_reconnect_start > WIFI_RECONNECT_TIMEOUT) {
        ESP.restart();
      }
      break;
  }
}

void setup_mdns() {
  if (!MDNS.begin(device_hostname.c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("arduino", "tcp", 3232);
}

// ============================================================================
// OTA
// ============================================================================

void setup_ota() {
  ArduinoOTA.setHostname(device_hostname.c_str());
  ArduinoOTA.setPort(3232);

  String ota_password = preferences.getString("ota_pass", "");
  if (ota_password.length() > 0) {
    ArduinoOTA.setPassword(ota_password.c_str());
  }

  ArduinoOTA.onStart([]() {
    if (is_moving) stop_movement("OTA");
    stop_motor();
    if (client.connected()) client.disconnect();
    esp_task_wdt_delete(NULL);
  });

  ArduinoOTA.begin();
}

// ============================================================================
// RESET BUTTON
// ============================================================================

void check_reset_button() {
  bool current_reading = digitalRead(RESET_BUTTON_PIN);

  if (current_reading != button_state) {
    last_button_change = millis();
    button_state = current_reading;
  }

  if (millis() - last_button_change >= BUTTON_DEBOUNCE_MS) {
    bool new_stable_state = button_state;

    if (new_stable_state == LOW && last_stable_state == HIGH) {
      if (is_moving) {
        if (!led_manual_control) {
          digitalWrite(STATUS_LED, LOW);
          delay(100);
          digitalWrite(STATUS_LED, HIGH);
        }
        last_stable_state = new_stable_state;
        return;
      }
      button_press_start = millis();
    }
    else if (new_stable_state == LOW && last_stable_state == LOW) {
      unsigned long hold_time = millis() - button_press_start;

      // LED feedback while holding
      if (!led_manual_control) {
        if (hold_time >= AP_HOLD_MIN && hold_time < AP_HOLD_MAX) {
          // 3-5 seconds: Solid LED for AP window
          digitalWrite(STATUS_LED, LOW);
        } else if (hold_time >= RESET_HOLD_MIN && hold_time < RESET_HOLD_MAX) {
          // 10-13 seconds: Rapid blink for factory reset window
          digitalWrite(STATUS_LED, (millis() / 100) % 2);
        } else {
          // Outside windows: LED off
          digitalWrite(STATUS_LED, HIGH);
        }
      }
    }
    else if (new_stable_state == HIGH && last_stable_state == LOW) {
      // Button released - check how long it was held
      unsigned long hold_time = millis() - button_press_start;

      // 3-5 seconds: Start config portal
      if (hold_time >= AP_HOLD_MIN && hold_time < AP_HOLD_MAX) {
        Serial.println("Starting config portal...");
        if (client.connected()) client.disconnect();

        // Force disconnect and switch to AP mode
        WiFi.disconnect(true, true);  // disconnect and erase credentials from RAM
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_AP);
        delay(100);

        WiFiManager wm;
        wm.setConfigPortalTimeout(300);
        wm.setBreakAfterConfig(true);
        wm.startConfigPortal("ESP32-Curtain-Setup", "12345678");

        delay(1000);
        ESP.restart();
      }

      // 10-13 seconds: Factory reset
      if (hold_time >= RESET_HOLD_MIN && hold_time < RESET_HOLD_MAX) {
        Serial.println("Factory reset...");
        if (client.connected()) client.disconnect();
        WiFi.disconnect(true);

        WiFiManager wm;
        wm.resetSettings();
        preferences.clear();

        delay(1000);
        ESP.restart();
      }

      if (!led_manual_control) {
        digitalWrite(STATUS_LED, HIGH);
      }
    }

    last_stable_state = new_stable_state;
  }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n=== Curtain Controller v4.3 ===");

  preferences.begin("curtains", false);

  current_position = preferences.getInt("position", 0);
  step_delay_us = preferences.getInt("step_delay", 2000);
  stepping_mode = preferences.getInt("step_mode", 4);
  motor_sleep_timeout = preferences.getULong("sleep_timeout", 30000);
  steps_per_revolution = preferences.getInt("steps_per_rev", 2000);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(STATUS_LED, HIGH);

  setup_a4988();
  setup_wifi_manager();
  setup_mdns();
  setup_webserial();
  setup_mqtt();
  setup_ota();

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.println("Ready!");
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("WebSerial: http://%s/webserial\n", WiFi.localIP().toString().c_str());
  Serial.printf("Setup: http://%s/setup\n", WiFi.localIP().toString().c_str());
}

void loop() {
  esp_task_wdt_reset();
  
  // Enforce LED state if manual control is active
  if (led_manual_control) {
    digitalWrite(STATUS_LED, led_desired_state);
  }
  
  ArduinoOTA.handle();
  check_reset_button();
  handle_movement();
  
  if (!client.connected()) {
    connect_mqtt();
  }
  client.loop();

  if (ws_command_pending) { 
    ws_command_pending = false; 
    process_command(ws_pending_command); 
  }
  // Auto-sleep motor after inactivity
  if (!is_moving && (millis() - last_motor_activity > motor_sleep_timeout)) {
    sleep_motor();
  }


  handle_wifi_reconnection();
  
  delay(1);
}
