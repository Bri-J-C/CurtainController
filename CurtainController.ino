// ============================================================================
// CURTAIN CONTROLLER v4.4 - A4988 Edition
// ============================================================================
// Target: ESP32-C3 Super Mini
// Driver: A4988 with hardware MS1/MS2/MS3 microstepping pins
// ============================================================================

#define FW_VERSION "4.4"

#include <esp_netif.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <ArduinoOTA.h>
#include "mdns.h"
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
const int DIR_PIN = 6;           // Direction control
const int SLEEP_PIN = 7;         // Sleep mode (LOW = sleep, HIGH = awake)
const int STATUS_LED = 8;        // Onboard blue LED (inverted logic)
const int RESET_BUTTON_PIN = 9;  // Onboard BOOT button
const int STEP_PIN = 10;         // Step pulse

// A4988 microstepping modes: {MS1, MS2, MS3}
// Mode 0=Full, 1=Half, 2=Quarter, 3=Eighth, 4=Sixteenth
const bool MICROSTEP_TABLE[5][3] = {
  {LOW,  LOW,  LOW},   // Full step
  {HIGH, LOW,  LOW},   // Half step
  {LOW,  HIGH, LOW},   // Quarter step
  {HIGH, HIGH, LOW},   // Eighth step
  {HIGH, HIGH, HIGH},  // Sixteenth step
};
int step_mode = 3;  // Default: 1/8 step

// Motor control
int current_position = 0;
int target_position = 0;
bool is_moving = false;
unsigned long last_step_time = 0;
unsigned long movement_start_time = 0;
const unsigned long MOVEMENT_TIMEOUT = 120000;  // 2 minute timeout
int steps_since_last_save = 0;
const int STEPS_BETWEEN_SAVES = 500;
unsigned long last_position_report = 0;
const unsigned long POSITION_REPORT_INTERVAL = 500;

// Motor state
int step_delay_us = 2000;
bool motor_sleeping = true;
unsigned long motor_sleep_timeout = 30000;
unsigned long last_motor_activity = 0;

// Direction invert
bool invert_direction = false;

// Reset button with debouncing
unsigned long button_press_start = 0;
unsigned long last_button_change = 0;
bool button_state = HIGH;
bool last_stable_state = HIGH;
const unsigned long BUTTON_DEBOUNCE_MS = 50;
const unsigned long AP_HOLD_MIN = 3000;
const unsigned long AP_HOLD_MAX = 5000;
const unsigned long RESET_HOLD_MIN = 10000;
const unsigned long RESET_HOLD_MAX = 13000;

// LED control
bool led_manual_control = false;

// WiFiManager AP credentials
const char* AP_SSID = "CurtainSetup";
const char* AP_PASS = "12345678";

// mDNS periodic re-announcement
unsigned long last_mdns_announce = 0;
const unsigned long MDNS_REANNOUNCE_INTERVAL = 60000;

// MQTT reconnection with exponential backoff
unsigned long last_mqtt_attempt = 0;
int mqtt_retry_delay = 2000;
const int MAX_MQTT_RETRY_DELAY = 60000;
unsigned long mqtt_subscribe_time = 0;
const unsigned long MQTT_IGNORE_RETAINED_MS = 2000;

// WiFi reconnection state machine
enum WiFiState { WIFI_IDLE, WIFI_LOST, WIFI_RECONNECTING, WIFI_BACKOFF };
WiFiState wifi_state = WIFI_IDLE;
unsigned long wifi_reconnect_start = 0;
const unsigned long WIFI_RECONNECT_TIMEOUT = 15000;  // 15s per attempt
int wifi_reconnect_attempts = 0;
const int WIFI_MAX_RECONNECT_ATTEMPTS = 8;  // 8 attempts before reboot (~3+ minutes total)
unsigned long wifi_backoff_until = 0;

volatile bool ws_command_pending = false;
SemaphoreHandle_t ws_mutex = NULL;
const char* last_reset_reason = "Unknown";

// Deferred restart (allows async response to send before rebooting)
bool pending_restart = false;
unsigned long restart_requested_at = 0;
const unsigned long RESTART_DELAY_MS = 1500;

// CSRF token for /save endpoint
String csrf_token;

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
String mqtt_speed_set_topic;
String mqtt_speed_state_topic;
String mqtt_invert_set_topic;
String mqtt_invert_state_topic;
String mqtt_stepmode_set_topic;
String mqtt_stepmode_state_topic;
String mqtt_totalsteps_set_topic;
String mqtt_totalsteps_state_topic;
String ws_pending_command;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void boot_wifi_connect();
void init_watchdog();
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
void stop_movement(const char* reason);
void save_position();
void publish_status(const char* status);
void publish_position();
void publish_ha_discovery(bool force = false);
void publish_settings_state();
void process_command(const String& command);
void stop_motor();
void wake_motor();
void sleep_motor();
void step_motor();
void start_movement(int target);
void apply_step_mode(int mode);

// ============================================================================
// LOGGING SYSTEM
// ============================================================================

enum LogLevel { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG };
LogLevel current_log_level = LOG_INFO;

static const char* log_level_name(LogLevel level) {
  switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN ";
    case LOG_INFO:  return "INFO ";
    case LOG_DEBUG: return "DEBUG";
    default:        return "?????";
  }
}

void log_msg(LogLevel level, const char* subsystem, const char* fmt, ...) {
  if (level > current_log_level) return;

  // Single buffer for prefix + message (saves ~256 bytes stack vs two buffers)
  char line[300];
  int prefix_len = snprintf(line, sizeof(line), "[%s] [%s] ", log_level_name(level), subsystem);
  va_list args;
  va_start(args, fmt);
  vsnprintf(line + prefix_len, sizeof(line) - prefix_len, fmt, args);
  va_end(args);

  Serial.println(line);
  if (WebSerial.getConnectionCount() > 0)
    WebSerial.println(line);
}

// For structured command output (status, config, help, etc.) — no prefix
void output(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  if (WebSerial.getConnectionCount() > 0)
    WebSerial.print(buf);
}

// Send a complete String as one WebSocket message (instant display, no line-by-line)
void ws_send_bulk(const String& text) {
  Serial.print(text);
  if (WebSerial.getConnectionCount() > 0) {
    auto* buf = WebSerial.makeBuffer(text.length());
    if (buf) {
      memcpy(buf->get(), text.c_str(), text.length());
      WebSerial.send(buf);
    }
  }
}

// Append formatted text to a String buffer (for building bulk output)
void buf_printf(String& out, const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  out += buf;
}

// ============================================================================
// HELPERS
// ============================================================================

// Shared step mode name table (#14: single source of truth)
static const char* const STEPMODE_NAMES[] = {"Full", "Half", "Quarter", "Eighth", "Sixteenth"};
static const char* const STEPMODE_MQTT[] = {"full", "half", "quarter", "eighth", "sixteenth"};

// Check if a string is all digits (for validating toInt input)
bool is_all_digits(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s.charAt(i))) return false;
  }
  return true;
}

// ============================================================================
// A4988 MOTOR CONTROL
// ============================================================================

void apply_step_mode(int mode) {
  mode = constrain(mode, 0, 4);
  step_mode = mode;
  digitalWrite(MS1_PIN, MICROSTEP_TABLE[mode][0]);
  digitalWrite(MS2_PIN, MICROSTEP_TABLE[mode][1]);
  digitalWrite(MS3_PIN, MICROSTEP_TABLE[mode][2]);
}

void stop_motor() {
  // Disable driver via ENABLE pin (HIGH = disabled on A4988)
  // NOTE: Do NOT set motor_sleeping here — that flag is only for
  // actual SLEEP pin state. sleep_motor() handles the full sequence.
  digitalWrite(ENABLE_PIN, HIGH);
}

void wake_motor() {
  if (motor_sleeping) {
    // Pull SLEEP high to wake the A4988
    digitalWrite(SLEEP_PIN, HIGH);
    motor_sleeping = false;
    // A4988 requires 1ms wake-up time after SLEEP goes high
    delay(1);
  }
  // Always enable the driver (may have been disabled by stop_motor)
  digitalWrite(ENABLE_PIN, LOW);
  last_motor_activity = millis();
}

void sleep_motor() {
  if (!motor_sleeping && !is_moving) {
    stop_motor();
    // Pull SLEEP low to enter low-power mode
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

void setup_a4988() {
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(MS1_PIN, OUTPUT);
  pinMode(MS2_PIN, OUTPUT);
  pinMode(MS3_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  // Start disabled and sleeping
  digitalWrite(ENABLE_PIN, HIGH);
  digitalWrite(SLEEP_PIN, LOW);
  digitalWrite(RESET_PIN, HIGH);  // Keep out of reset
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);

  apply_step_mode(step_mode);

  log_msg(LOG_INFO, "MOTOR", "A4988 initialized, mode=%d (1/%d step)",
          step_mode, 1 << step_mode);
}

void save_position() {
  preferences.putInt("position", current_position);
}

void start_movement(int target) {
  if (is_moving) return;

  target_position = constrain(target, 0, steps_per_revolution);
  if (current_position == target_position) {
    log_msg(LOG_INFO, "MOTOR", "Already at position %d", current_position);
    return;
  }

  wake_motor();

  // Apply direction inversion: swap open/close DIR_PIN logic when inverted
  bool dir_open  = invert_direction ? LOW  : HIGH;
  bool dir_close = invert_direction ? HIGH : LOW;

  if (target_position > current_position) {
    digitalWrite(DIR_PIN, dir_open);
    log_msg(LOG_INFO, "MOTOR", "Opening: %d -> %d (%d%%)",
            current_position, target_position,
            (target_position * 100) / steps_per_revolution);
    publish_status("opening");
  } else {
    digitalWrite(DIR_PIN, dir_close);
    log_msg(LOG_INFO, "MOTOR", "Closing: %d -> %d (%d%%)",
            current_position, target_position,
            (target_position * 100) / steps_per_revolution);
    publish_status("closing");
  }

  delayMicroseconds(10);

  is_moving = true;
  last_step_time = micros();
  movement_start_time = millis();
  steps_since_last_save = 0;
  last_position_report = 0;
  publish_position();
}

void stop_movement(const char* reason) {
  is_moving = false;
  stop_motor();
  save_position();
  publish_position();

  const char* status;
  if (strcmp(reason, "Complete") == 0) {
    if (current_position <= 0) status = "closed";
    else if (current_position >= steps_per_revolution) status = "open";
    else status = "stopped";  // Partial position
  } else {
    status = "stopped";
  }
  publish_status(status);
}

void handle_movement() {
  if (!is_moving) return;

  // Periodic yield for WebSocket responsiveness
  static unsigned long last_move_yield = 0;
  if (millis() - last_move_yield >= 50) {
    yield();
    last_move_yield = millis();
  }

  if (millis() - movement_start_time > MOVEMENT_TIMEOUT) {
    log_msg(LOG_ERROR, "MOTOR", "Movement timeout after %lums", MOVEMENT_TIMEOUT);
    publish_status("error_timeout");
    stop_movement("Timeout");
    return;
  }

  unsigned long now = micros();
  if (now - last_step_time >= (unsigned long)step_delay_us) {
    last_step_time = now;

    step_motor();

    if (current_position < target_position) {
      current_position++;
    } else {
      current_position--;
    }

    current_position = constrain(current_position, 0, steps_per_revolution);

    if (++steps_since_last_save >= STEPS_BETWEEN_SAVES) {
      save_position();
      steps_since_last_save = 0;
    }

    if (current_position == target_position) {
      log_msg(LOG_INFO, "MOTOR", "Movement complete, position %d (%d%%)",
              current_position, (current_position * 100) / steps_per_revolution);
      stop_movement("Complete");
    }
  }

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

  // When forced, clear all existing discovery topics first so HA rebuilds entities
  if (force) {
    String prefix;
    prefix = "homeassistant/cover/" + device_hostname + "/config";
    client.publish(prefix.c_str(), "", true);
    prefix = "homeassistant/number/" + device_hostname + "_speed/config";
    client.publish(prefix.c_str(), "", true);
    prefix = "homeassistant/switch/" + device_hostname + "_invert/config";
    client.publish(prefix.c_str(), "", true);
    prefix = "homeassistant/select/" + device_hostname + "_stepmode/config";
    client.publish(prefix.c_str(), "", true);
    delay(100);  // Let HA process the removals
  }

  // Helper: add shared device block to any discovery document
  String mac_id = "curtain_" + WiFi.macAddress();
  auto add_device = [&](JsonDocument& d) {
    d["availability_topic"] = mqtt_availability_topic;
    d["payload_available"] = "online";
    d["payload_not_available"] = "offline";
    JsonObject dev = d.createNestedObject("device");
    JsonArray ids = dev.createNestedArray("identifiers");
    ids.add(mac_id);
    dev["name"] = device_hostname;
    dev["model"] = "CurtainController-A4988";
    dev["manufacturer"] = "DIY";
    dev["sw_version"] = FW_VERSION;
    dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/setup";
  };

  // Helper: serialize and publish, validating against MQTT buffer size
  auto publish_discovery = [&](const char* topic, JsonDocument& d) -> bool {
    String json;
    serializeJson(d, json);
    if ((int)json.length() > client.getBufferSize()) {
      log_msg(LOG_ERROR, "MQTT", "Discovery payload too large: %d > %d", json.length(), client.getBufferSize());
      return false;
    }
    return client.publish(topic, json.c_str(), true);
  };

  // Cover entity
  {
    String topic = "homeassistant/cover/" + device_hostname + "/config";
    DynamicJsonDocument doc(1024);
    doc["name"] = nullptr;
    doc["unique_id"] = "curtain_" + device_hostname;
    doc["object_id"] = device_hostname;
    doc["command_topic"] = mqtt_command_topic;
    doc["state_topic"] = mqtt_stat_topic;
    doc["position_topic"] = mqtt_position_topic;
    doc["set_position_topic"] = mqtt_command_topic;
    doc["payload_open"] = "open";
    doc["payload_close"] = "close";
    doc["payload_stop"] = "stop";
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
    add_device(doc);
    if (publish_discovery(topic.c_str(), doc)) {
      preferences.putBool("ha_disc_done", true);
      log_msg(LOG_INFO, "MQTT", "HA Cover discovery published");
    } else {
      log_msg(LOG_ERROR, "MQTT", "HA Cover discovery FAILED");
    }
  }

  // Speed number entity
  {
    String topic = "homeassistant/number/" + device_hostname + "_speed/config";
    DynamicJsonDocument doc(512);
    doc["name"] = "Speed";
    doc["unique_id"] = "curtain_" + device_hostname + "_speed";
    doc["object_id"] = device_hostname + "_speed";
    doc["command_topic"] = mqtt_speed_set_topic;
    doc["state_topic"] = mqtt_speed_state_topic;
    doc["min"] = 100;
    doc["max"] = 10000;
    doc["step"] = 100;
    doc["unit_of_measurement"] = "us";
    doc["icon"] = "mdi:speedometer";
    add_device(doc);
    publish_discovery(topic.c_str(), doc);
  }

  // Invert direction switch entity
  {
    String topic = "homeassistant/switch/" + device_hostname + "_invert/config";
    DynamicJsonDocument doc(512);
    doc["name"] = "Invert Direction";
    doc["unique_id"] = "curtain_" + device_hostname + "_invert";
    doc["object_id"] = device_hostname + "_invert";
    doc["command_topic"] = mqtt_invert_set_topic;
    doc["state_topic"] = mqtt_invert_state_topic;
    doc["icon"] = "mdi:swap-horizontal";
    add_device(doc);
    publish_discovery(topic.c_str(), doc);
  }

  // Step mode select entity
  {
    String topic = "homeassistant/select/" + device_hostname + "_stepmode/config";
    DynamicJsonDocument doc(512);
    doc["name"] = "Step Mode";
    doc["unique_id"] = "curtain_" + device_hostname + "_stepmode";
    doc["object_id"] = device_hostname + "_stepmode";
    doc["command_topic"] = mqtt_stepmode_set_topic;
    doc["state_topic"] = mqtt_stepmode_state_topic;
    JsonArray options = doc.createNestedArray("options");
    for (int i = 0; i < 5; i++) options.add(STEPMODE_MQTT[i]);
    doc["icon"] = "mdi:stairs";
    add_device(doc);
    publish_discovery(topic.c_str(), doc);
  }

  // Total steps number entity
  {
    String topic = "homeassistant/number/" + device_hostname + "_totalsteps/config";
    DynamicJsonDocument doc(512);
    doc["name"] = "Total Steps";
    doc["unique_id"] = "curtain_" + device_hostname + "_totalsteps";
    doc["object_id"] = device_hostname + "_totalsteps";
    doc["command_topic"] = mqtt_totalsteps_set_topic;
    doc["state_topic"] = mqtt_totalsteps_state_topic;
    doc["min"] = 1;
    doc["max"] = 500000;
    doc["step"] = 1;
    doc["icon"] = "mdi:counter";
    add_device(doc);
    publish_discovery(topic.c_str(), doc);
  }

  log_msg(LOG_INFO, "MQTT", "HA discovery entities published");
}

static const char* stepmode_name(int mode) {
  if (mode < 0 || mode > 4) mode = 3;
  return STEPMODE_MQTT[mode];
}

void publish_settings_state() {
  if (!client.connected()) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", step_delay_us);
  client.publish(mqtt_speed_state_topic.c_str(), buf, true);
  client.publish(mqtt_invert_state_topic.c_str(), invert_direction ? "ON" : "OFF", true);
  client.publish(mqtt_stepmode_state_topic.c_str(), stepmode_name(step_mode), true);
  snprintf(buf, sizeof(buf), "%d", steps_per_revolution);
  client.publish(mqtt_totalsteps_state_topic.c_str(), buf, true);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg((char*)payload, length);
  msg.trim();
  log_msg(LOG_DEBUG, "MQTT", "Received on %s: %s", topic, msg.c_str());

  // Settings topics — handle before lowercasing so numeric values are preserved
  if (strcmp(topic, mqtt_totalsteps_set_topic.c_str()) == 0) {
    int value = msg.toInt();
    if (value >= 1 && value <= 500000) {
      steps_per_revolution = value;
      preferences.putInt("steps_per_rev", steps_per_revolution);
      current_position = constrain(current_position, 0, steps_per_revolution);
      preferences.putInt("position", current_position);
      log_msg(LOG_INFO, "MQTT", "Total steps set to %d", steps_per_revolution);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", steps_per_revolution);
      client.publish(mqtt_totalsteps_state_topic.c_str(), buf, true);
    }
    return;
  }

  if (strcmp(topic, mqtt_speed_set_topic.c_str()) == 0) {
    int value = msg.toInt();
    if (value >= 100 && value <= 10000) {
      step_delay_us = value;
      preferences.putInt("step_delay", step_delay_us);
      log_msg(LOG_INFO, "MQTT", "Speed set to %d us", step_delay_us);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", step_delay_us);
      client.publish(mqtt_speed_state_topic.c_str(), buf, true);
    }
    return;
  }

  if (strcmp(topic, mqtt_invert_set_topic.c_str()) == 0) {
    String lower = msg;
    lower.toLowerCase();
    bool value = (lower == "on" || lower == "true" || lower == "1");
    invert_direction = value;
    preferences.putBool("invert_dir", invert_direction);
    log_msg(LOG_INFO, "MQTT", "Direction invert set to %s", invert_direction ? "ON" : "OFF");
    client.publish(mqtt_invert_state_topic.c_str(), invert_direction ? "ON" : "OFF", true);
    return;
  }

  if (strcmp(topic, mqtt_stepmode_set_topic.c_str()) == 0) {
    String lower = msg;
    lower.toLowerCase();
    int value = -1;
    if (lower == "full") value = 0;
    else if (lower == "half") value = 1;
    else if (lower == "quarter") value = 2;
    else if (lower == "eighth") value = 3;
    else if (lower == "sixteenth") value = 4;
    if (value >= 0) {
      apply_step_mode(value);
      preferences.putInt("step_mode", step_mode);
      log_msg(LOG_INFO, "MQTT", "Step mode set to %s", stepmode_name(step_mode));
      client.publish(mqtt_stepmode_state_topic.c_str(), stepmode_name(step_mode), true);
    }
    return;
  }

  // Command topic — lowercase for case-insensitive matching
  msg.toLowerCase();

  // Ignore retained messages delivered right after subscribing
  if (millis() - mqtt_subscribe_time < MQTT_IGNORE_RETAINED_MS) {
    log_msg(LOG_DEBUG, "MQTT", "Ignoring retained command: %s", msg.c_str());
    return;
  }

  if (msg == "open" || msg == "close" || msg == "stop") {
    process_command(msg);
    return;
  }

  // Allow bare percentage numbers (HA position control)
  if (is_all_digits(msg)) {
    process_command(msg);
    return;
  }

  log_msg(LOG_WARN, "MQTT", "Ignored unknown command: %s", msg.c_str());
}

void connect_mqtt() {
  if (client.connected()) return;

  if (millis() - last_mqtt_attempt < mqtt_retry_delay) return;

  last_mqtt_attempt = millis();

  char client_id[80];
  {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    snprintf(client_id, sizeof(client_id), "%s_%s", device_hostname.c_str(), mac.c_str());
  }

  bool connected;
  if (mqtt_user.length() > 0) {
    connected = client.connect(client_id, mqtt_user.c_str(), mqtt_password.c_str(),
                               mqtt_availability_topic.c_str(), 1, true, "offline");
  } else {
    connected = client.connect(client_id, mqtt_availability_topic.c_str(), 1, true, "offline");
  }

  if (connected) {
    log_msg(LOG_INFO, "MQTT", "Connected to %s:%d", mqtt_server.c_str(), mqtt_port);
    mqtt_subscribe_time = millis();
    client.subscribe(mqtt_command_topic.c_str());
    client.subscribe(mqtt_speed_set_topic.c_str());
    client.subscribe(mqtt_invert_set_topic.c_str());
    client.subscribe(mqtt_stepmode_set_topic.c_str());
    client.subscribe(mqtt_totalsteps_set_topic.c_str());
    client.publish(mqtt_availability_topic.c_str(), "online", true);
    publish_position();
    publish_status(current_position >= steps_per_revolution ? "open" :
                   current_position <= 0 ? "closed" : "stopped");
    publish_ha_discovery();
    publish_settings_state();
    mqtt_retry_delay = 2000;
  } else {
    log_msg(LOG_WARN, "MQTT", "Connection failed (rc=%d), retry in %dms", client.state(), mqtt_retry_delay);
    mqtt_retry_delay = min(mqtt_retry_delay * 2, MAX_MQTT_RETRY_DELAY);
  }
}

void setup_mqtt() {
  mqtt_server = preferences.getString("mqtt_server", "");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "");
  mqtt_password = preferences.getString("mqtt_pass", "");

  String mqtt_root_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");
  mqtt_command_topic = mqtt_root_topic + "/cmd";
  mqtt_stat_topic = mqtt_root_topic + "/status";
  mqtt_position_topic = mqtt_root_topic + "/position";
  mqtt_availability_topic = mqtt_root_topic + "/availability";
  mqtt_speed_set_topic = mqtt_root_topic + "/speed/set";
  mqtt_speed_state_topic = mqtt_root_topic + "/speed/state";
  mqtt_invert_set_topic = mqtt_root_topic + "/invert/set";
  mqtt_invert_state_topic = mqtt_root_topic + "/invert/state";
  mqtt_stepmode_set_topic = mqtt_root_topic + "/stepmode/set";
  mqtt_stepmode_state_topic = mqtt_root_topic + "/stepmode/state";
  mqtt_totalsteps_set_topic = mqtt_root_topic + "/totalsteps/set";
  mqtt_totalsteps_state_topic = mqtt_root_topic + "/totalsteps/state";

  client.setBufferSize(2048);
  client.setServer(mqtt_server.c_str(), mqtt_port);
  client.setCallback(mqtt_callback);

  connect_mqtt();
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void cmd_open(const String& param) {
  log_msg(LOG_INFO, "CMD", "open");
  start_movement(steps_per_revolution);
}

void cmd_close(const String& param) {
  log_msg(LOG_INFO, "CMD", "close");
  start_movement(0);
}

void cmd_stop(const String& param) {
  log_msg(LOG_INFO, "CMD", "stop");
  stop_movement("User command");
}

void cmd_speed(const String& param) {
  if (!is_all_digits(param)) { log_msg(LOG_ERROR, "CMD", "speed: not a number"); return; }
  int value = param.toInt();
  if (value >= 100 && value <= 10000) {
    step_delay_us = value;
    preferences.putInt("step_delay", step_delay_us);
    log_msg(LOG_INFO, "NVS", "Speed set to %d us/step", step_delay_us);
    if (client.connected()) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", step_delay_us);
      client.publish(mqtt_speed_state_topic.c_str(), buf, true);
    }
  } else {
    log_msg(LOG_ERROR, "CMD", "speed: value must be 100-10000 us (got %d)", value);
  }
}

void cmd_mode(const String& param) {
  if (!is_all_digits(param)) { log_msg(LOG_ERROR, "CMD", "mode: not a number"); return; }
  int value = param.toInt();
  if (value >= 0 && value <= 4) {
    apply_step_mode(value);
    preferences.putInt("step_mode", step_mode);
    log_msg(LOG_INFO, "NVS", "Step mode set to %d (%s step)", step_mode, STEPMODE_NAMES[step_mode]);
    if (client.connected())
      client.publish(mqtt_stepmode_state_topic.c_str(), stepmode_name(step_mode), true);
  } else {
    log_msg(LOG_ERROR, "CMD", "mode: must be 0-4 (0=Full, 1=Half, 2=Quarter, 3=Eighth, 4=Sixteenth)");
  }
}

void cmd_steps(const String& param) {
  if (!is_all_digits(param)) { log_msg(LOG_ERROR, "CMD", "steps: not a number"); return; }
  int value = param.toInt();
  if (value > 0 && value <= 500000) {
    steps_per_revolution = value;
    preferences.putInt("steps_per_rev", steps_per_revolution);
    log_msg(LOG_INFO, "NVS", "Total travel steps set to %d", steps_per_revolution);
  } else {
    log_msg(LOG_ERROR, "CMD", "steps: must be 1-500000 (got %d)", value);
  }
}

void cmd_setposition(const String& param) {
  if (!is_all_digits(param)) { log_msg(LOG_ERROR, "CMD", "setposition: not a number"); return; }
  int value = param.toInt();
  if (value >= 0 && value <= steps_per_revolution) {
    current_position = value;
    save_position();
    publish_position();
    log_msg(LOG_INFO, "NVS", "Position reset to %d", current_position);
  } else {
    log_msg(LOG_ERROR, "CMD", "setposition: must be 0-%d (got %d)", steps_per_revolution, value);
  }
}

void cmd_sleep(const String& param) {
  if (!is_all_digits(param)) { log_msg(LOG_ERROR, "CMD", "sleep: not a number"); return; }
  int value = param.toInt();
  if (value >= 0 && value <= 300000) {
    motor_sleep_timeout = value;
    preferences.putULong("sleep_timeout", motor_sleep_timeout);
    log_msg(LOG_INFO, "NVS", "Sleep timeout set to %lu ms", motor_sleep_timeout);
  } else {
    log_msg(LOG_ERROR, "CMD", "sleep: must be 0-300000 ms (got %d)", value);
  }
}

void cmd_resetdriver(const String& param) {
  log_msg(LOG_INFO, "MOTOR", "Resetting A4988 via RESET pin...");
  digitalWrite(RESET_PIN, LOW);
  delay(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(10);
  apply_step_mode(step_mode);  // Restore microstepping after reset clears registers
  log_msg(LOG_INFO, "MOTOR", "A4988 reset complete");
}

void cmd_invert(const String& param) {
  invert_direction = !invert_direction;
  preferences.putBool("invert_dir", invert_direction);
  log_msg(LOG_INFO, "CMD", "Direction invert: %s", invert_direction ? "ON" : "OFF");
  if (client.connected())
    client.publish(mqtt_invert_state_topic.c_str(), invert_direction ? "ON" : "OFF", true);
}

void cmd_position(const String& param) {
  int percentage = (current_position * 100) / steps_per_revolution;
  output("Position: %d (%d%%)\n", current_position, percentage);
}

void cmd_hadiscovery(const String& param) {
  log_msg(LOG_INFO, "CMD", "hadiscovery — forcing republish");
  preferences.putBool("ha_disc_done", false);
  publish_ha_discovery(true);
}

void cmd_restart(const String& param) {
  log_msg(LOG_INFO, "SYS", "Restarting by command...");
  delay(1000);
  ESP.restart();
}

void cmd_loglevel(const String& param) {
  LogLevel new_level;
  if (param == "error") {
    new_level = LOG_ERROR;
  } else if (param == "warn") {
    new_level = LOG_WARN;
  } else if (param == "info") {
    new_level = LOG_INFO;
  } else if (param == "debug") {
    new_level = LOG_DEBUG;
  } else {
    log_msg(LOG_ERROR, "CMD", "loglevel: unknown level '%s' (use error|warn|info|debug)", param.c_str());
    return;
  }
  current_log_level = new_level;
  preferences.putUChar("log_level", (uint8_t)new_level);
  log_msg(LOG_INFO, "NVS", "Log level set to %s", log_level_name(new_level));
}

void cmd_ledon(const String& param) {
  led_manual_control = true;
  digitalWrite(STATUS_LED, LOW);
  log_msg(LOG_INFO, "CMD", "LED ON (manual)");
}

void cmd_ledoff(const String& param) {
  led_manual_control = true;
  digitalWrite(STATUS_LED, HIGH);
  log_msg(LOG_INFO, "CMD", "LED OFF (manual)");
}

void cmd_ledauto(const String& param) {
  led_manual_control = false;
  digitalWrite(STATUS_LED, HIGH);
  log_msg(LOG_INFO, "CMD", "LED auto mode restored");
}

void cmd_help(const String& param) {
  static const char help_text[] PROGMEM =
    "\n=== Movement ===\n"
    "open              Open curtain\n"
    "close             Close curtain\n"
    "stop              Stop movement\n"
    "<0-100>           Move to percentage\n"
    "position          Show current position\n"
    "\n=== Settings ===\n"
    "speed <us>        Step delay (100-10000, lower=faster)\n"
    "mode <0-4>        Microstepping (0=Full, 1=Half, 2=Quarter, 3=Eighth, 4=Sixteenth)\n"
    "steps <n>         Total travel range in steps\n"
    "invert            Toggle open/close direction\n"
    "sleep <ms>        Motor sleep timeout (0=never)\n"
    "\n=== Diagnostics ===\n"
    "status            Position, motor, MQTT status\n"
    "config            Full configuration dump\n"
    "loglevel <level>  Set log level (error|warn|info|debug)\n"
    "ledon/ledoff/ledauto  LED control\n"
    "\n=== System ===\n"
    "setposition <n>   Override position counter (use with care)\n"
    "resetdriver       Hardware reset A4988 via RESET pin\n"
    "hadiscovery       Republish HA discovery\n"
    "restart           Reboot device\n";
  ws_send_bulk(String(help_text));
}

void cmd_status(const String& param) {
  String out;
  out.reserve(500);

  buf_printf(out, "\n=== Status ===\n");
  buf_printf(out, "Position: %d (%d%%)\n", current_position,
             (current_position * 100) / steps_per_revolution);
  buf_printf(out, "Moving: %s\n", is_moving ? "Yes" : "No");
  if (is_moving) {
    buf_printf(out, "Target: %d\n", target_position);
  }
  buf_printf(out, "Motor: %s\n", motor_sleeping ? "Sleeping" : "Awake");
  buf_printf(out, "Direction: %s\n", invert_direction ? "Inverted" : "Normal");
  buf_printf(out, "MQTT: %s\n", client.connected() ? "Connected" : "Disconnected");
  buf_printf(out, "-- System --\n");
  buf_printf(out, "Heap: %u bytes\n", ESP.getFreeHeap());
  unsigned long uptime = millis() / 1000;
  buf_printf(out, "Uptime: %lud %luh %lum %lus\n",
             uptime / 86400, (uptime % 86400) / 3600, (uptime % 3600) / 60, uptime % 60);
  buf_printf(out, "Last reset: %s\n", last_reset_reason);
  buf_printf(out, "Log level: %s\n", log_level_name(current_log_level));
  buf_printf(out, "==============\n");
  ws_send_bulk(out);
}

void cmd_config(const String& param) {
  String mqtt_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");
  String out;
  out.reserve(700);

  buf_printf(out, "\n=== Configuration ===\n");
  buf_printf(out, "Hostname: %s\n", device_hostname.c_str());
  buf_printf(out, "IP: %s\n", WiFi.localIP().toString().c_str());
  buf_printf(out, "SSID: %s\n", WiFi.SSID().c_str());
  buf_printf(out, "RSSI: %d dBm\n", WiFi.RSSI());
  buf_printf(out, "MAC: %s\n", WiFi.macAddress().c_str());
  buf_printf(out, "MQTT: %s:%d\n", mqtt_server.c_str(), mqtt_port);
  buf_printf(out, "MQTT User: %s\n", mqtt_user.length() > 0 ? mqtt_user.c_str() : "(none)");
  buf_printf(out, "MQTT Topic: %s\n", mqtt_topic.c_str());
  buf_printf(out, "Speed: %d us/step (lower=faster)\n", step_delay_us);
  buf_printf(out, "Step Mode: %d (%s step)\n", step_mode, STEPMODE_NAMES[step_mode]);
  buf_printf(out, "Travel Steps: %d\n", steps_per_revolution);
  buf_printf(out, "Invert Direction: %s\n", invert_direction ? "YES" : "NO");
  buf_printf(out, "Sleep Timeout: %lu ms\n", motor_sleep_timeout);
  buf_printf(out, "Log level: %s\n", log_level_name(current_log_level));
  buf_printf(out, "Setup: http://%s/setup\n", WiFi.localIP().toString().c_str());
  buf_printf(out, "====================\n");
  ws_send_bulk(out);
}

struct Command {
  const char* name;
  void (*handler)(const String& param);
};

const Command commands[] = {
  {"open",          cmd_open},
  {"close",         cmd_close},
  {"stop",          cmd_stop},
  {"speed ",        cmd_speed},
  {"mode ",         cmd_mode},
  {"steps ",        cmd_steps},
  {"position",      cmd_position},
  {"setposition ",  cmd_setposition},
  {"invert",        cmd_invert},
  {"sleep ",        cmd_sleep},
  {"resetdriver",   cmd_resetdriver},
  {"hadiscovery",   cmd_hadiscovery},
  {"config",        cmd_config},
  {"status",        cmd_status},
  {"loglevel ",     cmd_loglevel},
  {"restart",       cmd_restart},
  {"help",          cmd_help},
  {"ledon",         cmd_ledon},
  {"ledoff",        cmd_ledoff},
  {"ledauto",       cmd_ledauto},
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
      if (input_len >= name_len && strncmp(input, name, name_len) == 0) {
        commands[i].handler(command.substring(name_len));
        return;
      }
    } else {
      if (input_len == name_len && strcmp(input, name) == 0) {
        commands[i].handler("");
        return;
      }
    }
  }

  // Check if it's a plain number (HA sends percentage directly)
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
      int target_steps = (percentage * steps_per_revolution) / 100;
      log_msg(LOG_INFO, "CMD", "HA position: %d%% -> step %d", percentage, target_steps);
      start_movement(target_steps);
      return;
    }
  }

  log_msg(LOG_ERROR, "CMD", "Unknown command: %s", command.c_str());
}

// ============================================================================
// WEBSERIAL & WEB SERVER
// ============================================================================

const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no">
  <title>Curtain Controller</title>
  <style>
    *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
    :root{--cyan:#00D4FF;--purple:#6366F1;--gradient:linear-gradient(135deg,var(--cyan),var(--purple));--bg:#0a0a1a;--card:rgba(255,255,255,0.03);--border:rgba(255,255,255,0.08);--text:#fff;--dim:rgba(255,255,255,0.5);--success:#10B981}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;margin:0;padding:20px;background:var(--bg);background-image:radial-gradient(ellipse at top left,rgba(0,212,255,0.1) 0%,transparent 50%),radial-gradient(ellipse at bottom right,rgba(99,102,241,0.1) 0%,transparent 50%);color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center}
    .container{max-width:420px;width:100%}
    .header{display:flex;align-items:center;justify-content:center;gap:14px;margin-bottom:8px}
    .header svg{width:48px;height:48px;filter:drop-shadow(0 4px 12px rgba(0,212,255,0.3))}
    h1{font-size:26px;font-weight:700;margin:0;background:var(--gradient);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
    .subtitle{text-align:center;color:var(--dim);font-size:13px;margin-bottom:24px}
    .card{background:var(--card);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border:1px solid var(--border);border-radius:16px;padding:20px;margin-bottom:16px}
    .card h2{font-size:15px;font-weight:600;margin:0 0 14px 0;background:var(--gradient);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
    label{display:block;color:var(--dim);font-size:13px;font-weight:500;margin-bottom:4px;margin-top:12px}
    label:first-of-type{margin-top:0}
    input,select{width:100%;padding:10px 14px;border-radius:10px;border:1px solid var(--border);background:var(--card);color:var(--text);font-size:14px;font-weight:500;transition:border-color 0.2s}
    input:focus,select:focus{outline:none;border-color:var(--cyan);box-shadow:0 0 0 3px rgba(0,212,255,0.1)}
    .hint{color:var(--dim);font-size:11px;margin-top:2px}
    .btn-row{display:flex;gap:12px;margin-top:20px}
    .btn{flex:1;padding:14px;font-size:15px;border-radius:12px;border:none;cursor:pointer;font-weight:600;transition:all 0.2s;text-align:center;text-decoration:none}
    .btn-primary{background:var(--gradient);color:#fff;box-shadow:0 4px 16px rgba(0,212,255,0.3)}
    .btn-primary:hover{transform:translateY(-1px);box-shadow:0 6px 20px rgba(0,212,255,0.4)}
    .btn-secondary{background:var(--card);color:var(--dim);border:1px solid var(--border)}
    .btn-secondary:hover{border-color:var(--cyan);color:var(--text)}
    .version{text-align:center;color:rgba(255,255,255,0.15);font-size:10px;margin-top:16px}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <svg viewBox="0 0 512 512" fill="none">
        <defs><linearGradient id="g" x1="0%" y1="0%" x2="100%" y2="100%"><stop offset="0%" stop-color="#00D4FF"/><stop offset="100%" stop-color="#6366F1"/></linearGradient></defs>
        <rect x="108" y="100" width="296" height="28" rx="4" fill="url(#g)"/>
        <rect x="108" y="86" width="32" height="56" rx="4" fill="url(#g)"/>
        <rect x="372" y="86" width="32" height="56" rx="4" fill="url(#g)"/>
        <rect x="140" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
        <rect x="176" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
        <rect x="212" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
        <rect x="274" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
        <rect x="310" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
        <rect x="346" y="140" width="26" height="300" rx="3" fill="url(#g)"/>
      </svg>
      <h1>Curtain Controller</h1>
    </div>
    <div class="subtitle">A4988 Edition &middot; %HOSTNAME%</div>

    <form action="/save" method="POST">
      <input type="hidden" name="csrf" value="%CSRF%">
      <div class="card">
        <h2>Network</h2>
        <label>Hostname</label>
        <input name="hostname" value="%HOSTNAME%">
        <label>MQTT Server</label>
        <input name="mqtt_server" value="%MQTT_SERVER%">
        <label>MQTT Port</label>
        <input name="mqtt_port" type="number" value="%MQTT_PORT%">
        <label>MQTT Username</label>
        <input name="mqtt_user" value="%MQTT_USER%">
        <label>MQTT Password</label>
        <input name="mqtt_pass" type="password" placeholder="(unchanged)" value="">
        <label>MQTT Root Topic</label>
        <input name="mqtt_topic" value="%MQTT_TOPIC%">
        <div class="hint">Creates: /cmd, /status, /position, /availability</div>
      </div>

      <div class="card">
        <h2>Motor</h2>
        <label>Travel Steps</label>
        <input name="steps" type="number" value="%STEPS%">
        <label>Microstepping Mode</label>
        <select name="stepmode">
          <option value="0" %MODE0%>Full Step (1/1)</option>
          <option value="1" %MODE1%>Half Step (1/2)</option>
          <option value="2" %MODE2%>Quarter Step (1/4)</option>
          <option value="3" %MODE3%>Eighth Step (1/8)</option>
          <option value="4" %MODE4%>Sixteenth Step (1/16)</option>
        </select>
      </div>

      <div class="card">
        <h2>System</h2>
        <label>OTA Password</label>
        <input name="ota_pass" type="password" placeholder="Leave blank to keep current">
        <div class="hint">Device will reboot after saving.</div>
      </div>

      <div class="btn-row">
        <button type="submit" class="btn btn-primary">Save &amp; Reboot</button>
        <a href="/webserial" class="btn btn-secondary">Console</a>
      </div>
    </form>

    <div class="version">v%VERSION%</div>
  </div>
</body>
</html>
)rawliteral";

String html_escape(const String& raw) {
  String out;
  out.reserve(raw.length() + 16);
  for (unsigned int i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

void setup_webserial() {
  // Root redirects to setup
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });

  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Generate CSRF token
    csrf_token = String(esp_random(), HEX) + String(esp_random(), HEX);

    String html = FPSTR(SETUP_HTML);
    String hostname = html_escape(preferences.getString("hostname", "CurtainController"));
    html.replace("%HOSTNAME%", hostname);
    html.replace("%VERSION%", FW_VERSION);
    html.replace("%CSRF%", csrf_token);
    html.replace("%MQTT_SERVER%", html_escape(preferences.getString("mqtt_server", "192.168.1.100")));
    html.replace("%MQTT_PORT%", String(preferences.getInt("mqtt_port", 1883)));
    html.replace("%MQTT_USER%", html_escape(preferences.getString("mqtt_user", "")));
    // Don't echo MQTT password into HTML — credential leak risk
    html.replace("%MQTT_TOPIC%", html_escape(preferences.getString("mqtt_root_topic", "home/room/curtains")));
    html.replace("%STEPS%", String(preferences.getInt("steps_per_rev", 2000)));

    // Microstepping mode dropdown selected state
    int sm = preferences.getInt("step_mode", 3);
    html.replace("%MODE0%", sm == 0 ? "selected" : "");
    html.replace("%MODE1%", sm == 1 ? "selected" : "");
    html.replace("%MODE2%", sm == 2 ? "selected" : "");
    html.replace("%MODE3%", sm == 3 ? "selected" : "");
    html.replace("%MODE4%", sm == 4 ? "selected" : "");

    request->send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Validate CSRF token
    if (!request->hasParam("csrf", true) ||
        csrf_token.length() == 0 ||
        request->getParam("csrf", true)->value() != csrf_token) {
      request->send(403, "text/plain", "Invalid or missing CSRF token. Go back and try again.");
      return;
    }
    csrf_token = "";  // Invalidate after use

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
      String pass = request->getParam("mqtt_pass", true)->value();
      if (pass.length() > 0) preferences.putString("mqtt_pass", pass);
    }
    if (request->hasParam("mqtt_topic", true)) {
      preferences.putString("mqtt_root_topic", request->getParam("mqtt_topic", true)->value());
    }
    if (request->hasParam("steps", true)) {
      int steps = request->getParam("steps", true)->value().toInt();
      if (steps > 0) preferences.putInt("steps_per_rev", steps);
    }
    if (request->hasParam("stepmode", true)) {
      int val = request->getParam("stepmode", true)->value().toInt();
      if (val >= 0 && val <= 4) preferences.putInt("step_mode", val);
    }
    if (request->hasParam("ota_pass", true)) {
      String ota = request->getParam("ota_pass", true)->value();
      if (ota.length() > 0) preferences.putString("ota_pass", ota);
    }
    preferences.putBool("ha_disc_done", false);

    request->send(200, "text/html",
      "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:-apple-system,sans-serif;background:#0a0a1a;color:#fff;"
      "display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}"
      "div{text-align:center}h1{background:linear-gradient(135deg,#00D4FF,#6366F1);"
      "-webkit-background-clip:text;-webkit-text-fill-color:transparent}</style></head>"
      "<body><div><h1>Saved!</h1><p style='color:rgba(255,255,255,0.5)'>Rebooting...</p>"
      "</div></body></html>");

    // Deferred restart — let the async response send before rebooting
    pending_restart = true;
    restart_requested_at = millis();
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

    output("> %s\n", command.c_str());

    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      ws_pending_command = command;
      ws_command_pending = true;
      xSemaphoreGive(ws_mutex);
    }
  });

  WebSerial.begin(&server);
  WebSerial.setBuffer(256);
  server.begin();
}

// ============================================================================
// WIFI & NETWORK
// ============================================================================

bool check_button_hold_at_boot(unsigned long hold_time_ms) {
  unsigned long start = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (millis() - start >= hold_time_ms) {
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
  WiFiManagerParameter p_pass("password", "MQTT Password", "", 40);
  WiFiManagerParameter p_topic("mqtt_root_topic", "MQTT Root Topic", mqtt_topic.c_str(), 80);
  WiFiManagerParameter p_ota("ota_pass", "OTA Password", "", 40);
  WiFiManagerParameter p_steps("steps_per_rev", "Steps per Revolution", String(steps_per_revolution).c_str(), 8);

  wm.addParameter(&p_hostname);
  wm.addParameter(&p_server);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.addParameter(&p_topic);
  wm.addParameter(&p_ota);
  wm.addParameter(&p_steps);

  log_msg(LOG_INFO, "WIFI", "Starting config portal AP: CurtainSetup");
  if (!wm.autoConnect(AP_SSID, AP_PASS)) {
    log_msg(LOG_ERROR, "WIFI", "Config portal timeout, restarting...");
    delay(1000);
    ESP.restart();
  }
  log_msg(LOG_INFO, "WIFI", "WiFi connected via config portal");

  if (shouldSave) {
    preferences.putString("hostname", p_hostname.getValue());
    preferences.putString("mqtt_server", p_server.getValue());
    preferences.putInt("mqtt_port", atoi(p_port.getValue()));
    preferences.putString("mqtt_user", p_user.getValue());
    if (strlen(p_pass.getValue()) > 0) preferences.putString("mqtt_pass", p_pass.getValue());
    preferences.putString("mqtt_root_topic", p_topic.getValue());
    int steps_val = atoi(p_steps.getValue());
    if (steps_val > 0) preferences.putInt("steps_per_rev", steps_val);
    if (strlen(p_ota.getValue()) > 0) preferences.putString("ota_pass", p_ota.getValue());
    preferences.putBool("ha_disc_done", false);
    delay(1000);
    ESP.restart();
  }
}

void boot_wifi_connect() {
  device_hostname = preferences.getString("hostname", "CurtainController");
  mqtt_server = preferences.getString("mqtt_server", "");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "");
  mqtt_password = preferences.getString("mqtt_pass", "");

  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(200);
  WiFi.setHostname(device_hostname.c_str());
  WiFi.setAutoReconnect(false);

  if (check_button_hold_at_boot(3000)) {
    log_msg(LOG_INFO, "BOOT", "Button held — launching config portal");
    start_config_portal();
    return;
  }

  WiFi.begin();

  log_msg(LOG_INFO, "BOOT", "Connecting to WiFi (15s timeout)");
  unsigned long connect_start = millis();
  int blink = 0;
  while (millis() - connect_start < 15000) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      break;
    }
    delay(250);
    digitalWrite(STATUS_LED, (blink++ % 2) ? LOW : HIGH);
  }

  digitalWrite(STATUS_LED, HIGH);

  if (WiFi.status() != WL_CONNECTED) {
    WiFiManager wm_check;
    if (wm_check.getWiFiIsSaved()) {
      log_msg(LOG_WARN, "BOOT", "WiFi timeout — credentials saved, rebooting to retry");
      delay(100);
      ESP.restart();
    } else {
      log_msg(LOG_INFO, "BOOT", "WiFi timeout — no credentials, launching portal");
      start_config_portal();
      return;
    }
  }

  log_msg(LOG_INFO, "WIFI", "WiFi connected: %s", WiFi.localIP().toString().c_str());

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif) {
    esp_netif_set_hostname(netif, device_hostname.c_str());
  }

  wifi_state = WIFI_IDLE;
}

void handle_wifi_reconnection() {
  switch (wifi_state) {
    case WIFI_IDLE:
      if (WiFi.status() != WL_CONNECTED) {
        log_msg(LOG_WARN, "WIFI", "Connection lost");
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
          mdns_netif_action(netif, MDNS_EVENT_DISABLE_IP4);
        }
        wifi_state = WIFI_LOST;
      }
      break;

    case WIFI_LOST:
      WiFi.disconnect(false);
      WiFi.setHostname(device_hostname.c_str());
      WiFi.begin();
      wifi_reconnect_start = millis();
      wifi_reconnect_attempts++;
      log_msg(LOG_WARN, "WIFI", "Reconnect attempt %d/%d", wifi_reconnect_attempts, WIFI_MAX_RECONNECT_ATTEMPTS);
      wifi_state = WIFI_RECONNECTING;
      break;

    case WIFI_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        wifi_reconnect_attempts = 0;
        mqtt_retry_delay = 2000;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
          esp_netif_set_hostname(netif, device_hostname.c_str());
          mdns_netif_action(netif, MDNS_EVENT_ENABLE_IP4);
          mdns_netif_action(netif, MDNS_EVENT_ANNOUNCE_IP4);
          last_mdns_announce = millis();
        }
        log_msg(LOG_INFO, "WIFI", "Reconnected: %s", WiFi.localIP().toString().c_str());
        wifi_state = WIFI_IDLE;
      } else if (millis() - wifi_reconnect_start > WIFI_RECONNECT_TIMEOUT) {
        if (wifi_reconnect_attempts >= WIFI_MAX_RECONNECT_ATTEMPTS) {
          log_msg(LOG_ERROR, "WIFI", "Max reconnect attempts reached — rebooting");
          if (is_moving) stop_movement("WiFi timeout");
          save_position();
          ESP.restart();
        }
        unsigned long backoff_ms = min((unsigned long)wifi_reconnect_attempts * 2000UL, 30000UL);
        wifi_backoff_until = millis() + backoff_ms;
        log_msg(LOG_WARN, "WIFI", "Backoff %lums before next attempt", backoff_ms);
        wifi_state = WIFI_BACKOFF;
      }
      break;

    case WIFI_BACKOFF:
      if (millis() >= wifi_backoff_until) {
        wifi_state = WIFI_LOST;
      }
      break;
  }
}

void setup_mdns() {
  esp_err_t err = mdns_init();
  bool already_init = false;
  if (err != ESP_OK && !already_init) {
    log_msg(LOG_ERROR, "MDNS", "Init failed: %s", esp_err_to_name(err));
    return;
  }

  err = mdns_hostname_set(device_hostname.c_str());
  if (err != ESP_OK) {
    log_msg(LOG_ERROR, "MDNS", "Hostname set failed: %s", esp_err_to_name(err));
    return;
  }

  String instance = "Curtain - " + device_hostname;
  mdns_instance_name_set(instance.c_str());

  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  mdns_service_add(NULL, "_arduino", "_tcp", 3232, NULL, 0);

  log_msg(LOG_INFO, "MDNS", "Started: %s.local", device_hostname.c_str());
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
    log_msg(LOG_INFO, "OTA", "OTA update starting — stopping motor and disconnecting");
    if (is_moving) stop_movement("OTA");
    sleep_motor();
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
        // Short press during movement = emergency stop
        stop_movement("User command");
        log_msg(LOG_INFO, "BTN", "Movement stopped by button press");
        button_press_start = millis();
        last_stable_state = new_stable_state;
        return;
      }
      button_press_start = millis();
    }
    else if (new_stable_state == LOW && last_stable_state == LOW) {
      unsigned long hold_time = millis() - button_press_start;

      if (!led_manual_control) {
        if (hold_time >= AP_HOLD_MIN && hold_time < AP_HOLD_MAX) {
          digitalWrite(STATUS_LED, LOW);
        } else if (hold_time >= RESET_HOLD_MIN && hold_time < RESET_HOLD_MAX) {
          digitalWrite(STATUS_LED, (millis() / 100) % 2);
        } else {
          digitalWrite(STATUS_LED, HIGH);
        }
      }
    }
    else if (new_stable_state == HIGH && last_stable_state == LOW) {
      unsigned long hold_time = millis() - button_press_start;

      if (hold_time >= AP_HOLD_MIN && hold_time < AP_HOLD_MAX) {
        log_msg(LOG_INFO, "BTN", "Config portal triggered by button hold (%lums)", hold_time);
        if (client.connected()) client.disconnect();

        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_AP);
        delay(100);

        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) mdns_netif_action(netif, MDNS_EVENT_DISABLE_IP4);

        esp_task_wdt_delete(NULL);

        String mqtt_topic = preferences.getString("mqtt_root_topic", "home/room/curtains");
        String ota_pass = preferences.getString("ota_pass", "");

        WiFiManager wm;
        wm.setConfigPortalTimeout(300);
        wm.setBreakAfterConfig(true);

        bool shouldSave = false;
        wm.setSaveConfigCallback([&shouldSave]() { shouldSave = true; });

        WiFiManagerParameter p_hostname("hostname", "Device Hostname", device_hostname.c_str(), 40);
        WiFiManagerParameter p_server("server", "MQTT Server IP", mqtt_server.c_str(), 40);
        WiFiManagerParameter p_port("port", "MQTT Port", String(mqtt_port).c_str(), 6);
        WiFiManagerParameter p_user("user", "MQTT Username", mqtt_user.c_str(), 40);
        WiFiManagerParameter p_pass("password", "MQTT Password", "", 40);
        WiFiManagerParameter p_topic("mqtt_root_topic", "MQTT Root Topic", mqtt_topic.c_str(), 80);
        WiFiManagerParameter p_ota("ota_pass", "OTA Password", "", 40);
        WiFiManagerParameter p_steps("steps_per_rev", "Steps per Revolution", String(steps_per_revolution).c_str(), 8);

        wm.addParameter(&p_hostname);
        wm.addParameter(&p_server);
        wm.addParameter(&p_port);
        wm.addParameter(&p_user);
        wm.addParameter(&p_pass);
        wm.addParameter(&p_topic);
        wm.addParameter(&p_ota);
        wm.addParameter(&p_steps);

        wm.startConfigPortal(AP_SSID, AP_PASS);

        if (shouldSave) {
          preferences.putString("hostname", p_hostname.getValue());
          preferences.putString("mqtt_server", p_server.getValue());
          preferences.putInt("mqtt_port", atoi(p_port.getValue()));
          preferences.putString("mqtt_user", p_user.getValue());
          if (strlen(p_pass.getValue()) > 0) preferences.putString("mqtt_pass", p_pass.getValue());
          preferences.putString("mqtt_root_topic", p_topic.getValue());
          int steps_val = atoi(p_steps.getValue());
          if (steps_val > 0) preferences.putInt("steps_per_rev", steps_val);
          if (strlen(p_ota.getValue()) > 0) preferences.putString("ota_pass", p_ota.getValue());
          preferences.putBool("ha_disc_done", false);
        }

        delay(1000);
        ESP.restart();
      }

      if (hold_time >= RESET_HOLD_MIN && hold_time < RESET_HOLD_MAX) {
        log_msg(LOG_INFO, "BTN", "Factory reset triggered by button hold (%lums)", hold_time);
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

void init_watchdog() {
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };

  esp_err_t err = esp_task_wdt_init(&wdt_config);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_reconfigure(&wdt_config);
  }
  if (err != ESP_OK) {
    log_msg(LOG_ERROR, "WDT", "Init failed: %s", esp_err_to_name(err));
    return;
  }

  err = esp_task_wdt_add(NULL);
  if (err == ESP_ERR_INVALID_ARG) {
    // Task already subscribed after soft reset
  } else if (err != ESP_OK) {
    log_msg(LOG_ERROR, "WDT", "Failed to add task: %s", esp_err_to_name(err));
  }

  log_msg(LOG_INFO, "WDT", "Initialized, timeout=%ds", WDT_TIMEOUT);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.printf("\n=== Curtain Controller v%s (A4988) ===\n", FW_VERSION);

  ws_mutex = xSemaphoreCreateMutex();

  esp_reset_reason_t reason = esp_reset_reason();
  const char* reason_str;
  switch (reason) {
    case ESP_RST_POWERON:  reason_str = "Power-on"; break;
    case ESP_RST_SW:       reason_str = "Software restart"; break;
    case ESP_RST_PANIC:    reason_str = "Crash (panic)"; break;
    case ESP_RST_INT_WDT:  reason_str = "Interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: reason_str = "Task watchdog"; break;
    case ESP_RST_WDT:      reason_str = "Other watchdog"; break;
    case ESP_RST_BROWNOUT: reason_str = "Brownout"; break;
    default:               reason_str = "Unknown"; break;
  }
  last_reset_reason = reason_str;
  Serial.printf("Reset reason: %s\n", reason_str);

  preferences.begin("curtains", false);

  // Load and validate all preferences
  steps_per_revolution = constrain(preferences.getInt("steps_per_rev", 2000), 1, 500000);
  current_position = constrain(preferences.getInt("position", 0), 0, steps_per_revolution);
  step_delay_us = constrain(preferences.getInt("step_delay", 2000), 100, 10000);
  motor_sleep_timeout = preferences.getULong("sleep_timeout", 30000);
  if (motor_sleep_timeout > 300000) motor_sleep_timeout = 30000;
  step_mode = constrain(preferences.getInt("step_mode", 3), 0, 4);
  invert_direction = preferences.getBool("invert_dir", false);
  int loaded_log = preferences.getUChar("log_level", (uint8_t)LOG_INFO);
  current_log_level = (loaded_log <= LOG_DEBUG) ? (LogLevel)loaded_log : LOG_INFO;

  log_msg(LOG_INFO, "BOOT", "Reset reason: %s", reason_str);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(STATUS_LED, HIGH);

  setup_a4988();
  boot_wifi_connect();
  setup_mdns();
  setup_webserial();
  setup_mqtt();
  setup_ota();
  init_watchdog();

  log_msg(LOG_INFO, "BOOT", "Ready! IP:%s Mode:%d Invert:%s LogLevel:%s",
          WiFi.localIP().toString().c_str(),
          step_mode,
          invert_direction ? "YES" : "NO",
          log_level_name(current_log_level));
  log_msg(LOG_INFO, "BOOT", "WebSerial: http://%s/webserial", WiFi.localIP().toString().c_str());
  log_msg(LOG_INFO, "BOOT", "Setup: http://%s/setup", WiFi.localIP().toString().c_str());
}

void loop() {
  esp_task_wdt_reset();

  // Deferred restart (from /save or other sources)
  if (pending_restart && millis() - restart_requested_at > RESTART_DELAY_MS) {
    ESP.restart();
  }

  ArduinoOTA.handle();
  check_reset_button();
  handle_movement();

  if (wifi_state == WIFI_IDLE) {
    if (!client.connected()) {
      connect_mqtt();
    }
    client.loop();
  }

  if (ws_command_pending) {
    String cmd_copy;
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      cmd_copy = ws_pending_command;
      ws_command_pending = false;
      xSemaphoreGive(ws_mutex);
    }
    if (cmd_copy.length() > 0) {
      process_command(cmd_copy);
    }
  }

  // Auto-sleep motor after inactivity (0 = never sleep)
  if (motor_sleep_timeout > 0 && !is_moving && !motor_sleeping) {
    if (millis() - last_motor_activity > motor_sleep_timeout) {
      sleep_motor();
    }
  }

  handle_wifi_reconnection();

  // Periodic mDNS re-announcement (safety net for missed multicast)
  if (wifi_state == WIFI_IDLE && millis() - last_mdns_announce > MDNS_REANNOUNCE_INTERVAL) {
    last_mdns_announce = millis();
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
      mdns_netif_action(netif, MDNS_EVENT_ANNOUNCE_IP4);
    }
  }

  yield();  // Let WiFi/WebSocket process
}
