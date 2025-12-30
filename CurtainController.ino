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
const int DIR_PIN = 6;           // Direction control
const int SLEEP_PIN = 7;         // Sleep mode (LOW = sleep, HIGH = awake)
const int STATUS_LED = 8;        // Onboard blue LED (inverted logic)
const int RESET_BUTTON_PIN = 9;  // Onboard BOOT button
const int STEP_PIN = 10;         // Step pulse

// Motor control
int current_position = 0;
int target_position = 0;
bool is_moving = false;
unsigned long last_step_time = 0;
unsigned long movement_start_time = 0;
const unsigned long MOVEMENT_TIMEOUT = 120000;  // 2 minute timeout
int steps_since_last_save = 0;
const int STEPS_BETWEEN_SAVES = 50;

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
const unsigned long BUTTON_DEBOUNCE_MS = 50;
const unsigned long RESET_HOLD_TIME = 5000;

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
String mqtt_config_topic;
String ws_pending_command;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void setup_a4988();
void setup_wifi_manager();
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
void publish_config();
void publish_ha_discovery();
void process_command(const String& command);
void set_microstepping_mode(int mode);
void pulse_reset();
void wake_motor();
void sleep_motor();
void step_motor();
void start_movement(int target);

// Command handlers
void cmd_open(const String& param);
void cmd_close(const String& param);
void cmd_stop(const String& param);
void cmd_position(const String& param);
void cmd_set_speed(const String& param);
void cmd_set_mode(const String& param);
void cmd_set_steps(const String& param);
void cmd_reset_position(const String& param);
void cmd_set_sleep(const String& param);
void cmd_set_hostname(const String& param);
void cmd_wifi_info(const String& param);
void cmd_mqtt_info(const String& param);
void cmd_reset_driver(const String& param);
void cmd_ha_discovery(const String& param);
void cmd_config(const String& param);
void cmd_status(const String& param);
void cmd_restart(const String& param);
void cmd_help(const String& param);
void cmd_led_on(const String& param);
void cmd_led_off(const String& param);


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
  // CRITICAL: Disable and sleep motor FIRST to prevent boot movement
  // Configure control pins first
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH);   // DISABLE motor immediately (HIGH = disabled)
  pinMode(SLEEP_PIN, OUTPUT);
  digitalWrite(SLEEP_PIN, LOW);     // SLEEP motor immediately
  
  // Small delay to ensure motor is fully disabled
  delay(10);
  
  // Now configure remaining pins safely
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(MS1_PIN, OUTPUT);
  pinMode(MS2_PIN, OUTPUT);
  pinMode(MS3_PIN, OUTPUT);
  
  // Set safe states
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(RESET_PIN, HIGH); 
  
  // Set initial microstepping mode
  set_microstepping_mode(stepping_mode);
  
  Serial.println("[A4988] Driver configured");
  Serial.printf("[A4988] Mode: %s\n", MICROSTEP_NAMES[stepping_mode]);
  Serial.println("[A4988] Motor DISABLED and SLEEPING on boot");
}

void set_microstepping_mode(int mode) {
  if (mode < 0 || mode > 4) {
    Serial.printf("[ERROR] Invalid microstepping mode: %d\n", mode);
    return;
  }
  
  digitalWrite(MS1_PIN, MICROSTEP_CONFIG[mode][0]);
  digitalWrite(MS2_PIN, MICROSTEP_CONFIG[mode][1]);
  digitalWrite(MS3_PIN, MICROSTEP_CONFIG[mode][2]);
  
  stepping_mode = mode;
  Serial.printf("[A4988] Microstepping: %s\n", MICROSTEP_NAMES[mode]);
}

void pulse_reset() {
  Serial.println("[A4988] Pulsing RESET pin");
  digitalWrite(RESET_PIN, LOW);
  delayMicroseconds(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(2);
}

void stop_motor() {
  digitalWrite(ENABLE_PIN, HIGH);  // Disable driver
}

void wake_motor() {
  if (motor_sleeping) {
    digitalWrite(SLEEP_PIN, HIGH);   // Wake from sleep
    digitalWrite(ENABLE_PIN, LOW);   // Enable driver
    delayMicroseconds(2);            // tWAKE min
    motor_sleeping = false;
    Serial.println("[Motor] Awake");
  }
  last_motor_activity = millis();
}

void sleep_motor() {
  if (!motor_sleeping && !is_moving) {
    digitalWrite(ENABLE_PIN, HIGH);  // Disable first
    delayMicroseconds(1);
    digitalWrite(SLEEP_PIN, LOW);    // Then sleep
    motor_sleeping = true;
    Serial.println("[Motor] Sleep");
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
  if (is_moving) {
    Serial.println("[Motor] Already moving");
    return;
  }
  
  target_position = constrain(target, 0, steps_per_revolution);
  
  if (current_position == target_position) {
    Serial.println("[Motor] Already at target");
    return;
  }
  
  wake_motor();
  
  // Set direction
  if (target_position > current_position) {
    digitalWrite(DIR_PIN, HIGH);
    Serial.printf("[Motor] Opening: %d → %d\n", current_position, target_position);
    publish_status("opening");
  } else {
    digitalWrite(DIR_PIN, LOW);
    Serial.printf("[Motor] Closing: %d → %d\n", current_position, target_position);
    publish_status("closing");
  }
  
  delayMicroseconds(2);  // tSETUP
  
  is_moving = true;
  last_step_time = micros();
  movement_start_time = millis();
  steps_since_last_save = 0;
}

void stop_movement(const char* reason) {
  is_moving = false;
  stop_motor();
  save_position();
  
  // Publish final position
  publish_position();
  
  // Determine and publish final status
  const char* status;
  if (current_position >= steps_per_revolution) {
    status = "open";
  } else if (current_position <= 0) {
    status = "closed";
  } else {
    status = "partial";
  }
  publish_status(status);
  
  int percentage = (current_position * 100) / steps_per_revolution;
  Serial.printf("[Motor] Stopped: %s at %d (%d%%)\n", reason, current_position, percentage);
}

void handle_movement() {
  if (!is_moving) return;

  // Movement timeout protection
  if (millis() - movement_start_time > MOVEMENT_TIMEOUT) {
    Serial.println("[ERROR] Movement timeout!");
    publish_status("error_timeout");
    stop_movement("Timeout");
    return;
  }

  // Overflow-safe timing check
  unsigned long current_micros = micros();
  unsigned long elapsed;
  if (current_micros >= last_step_time) {
    elapsed = current_micros - last_step_time;
  } else {
    // Overflow occurred
    elapsed = (0xFFFFFFFF - last_step_time) + current_micros + 1;
  }

  if (elapsed >= (unsigned long)step_delay_us) {
    last_step_time = current_micros;

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
}

// ============================================================================
// MQTT
// ============================================================================

void publish_status(const char* status) {
  if (client.connected()) {
    client.publish(mqtt_stat_topic.c_str(), status, true);
    Serial.printf("[MQTT] Status: %s\n", status);
  }
}

void publish_position() {
  if (client.connected()) {
    int percentage = (current_position * 100) / steps_per_revolution;
    char pos_str[8];
    snprintf(pos_str, sizeof(pos_str), "%d", percentage);
    client.publish(mqtt_position_topic.c_str(), pos_str, true);
    Serial.printf("[MQTT] Position: %d%%\n", percentage);
  }
}

void publish_ha_discovery() {
  if (!client.connected()) {
    Serial.println("[MQTT] Cannot publish HA discovery - not connected");
    return;
  }
  
  // Check if already published
  bool already_published = preferences.getBool("ha_disc_done", false);
  if (already_published) {
    Serial.println("[MQTT] HA discovery already published (use 'ha_discovery' to republish)");
    return;
  }
  
  String discovery_topic = "homeassistant/cover/" + device_hostname + "/config";
  
  StaticJsonDocument<1024> doc;
  
  doc["name"] = "Curtain " + device_hostname;
  doc["unique_id"] = "curtain_" + device_hostname;
  doc["object_id"] = device_hostname;
  
  doc["command_topic"] = mqtt_command_topic;
  doc["state_topic"] = mqtt_stat_topic;
  doc["position_topic"] = mqtt_position_topic;
  doc["set_position_topic"] = mqtt_command_topic;
  doc["availability_topic"] = mqtt_stat_topic;
  
  doc["payload_open"] = "open";
  doc["payload_close"] = "close";
  doc["payload_stop"] = "stop";
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  
  doc["state_open"] = "open";
  doc["state_opening"] = "opening";
  doc["state_closed"] = "closed";
  doc["state_closing"] = "closing";
  
  doc["position_open"] = 100;
  doc["position_closed"] = 0;
  
  doc["optimistic"] = false;
  doc["qos"] = 1;
  doc["retain"] = true;
  
  JsonObject device = doc.createNestedObject("device");
  JsonArray identifiers = device.createNestedArray("identifiers");
  identifiers.add(device_hostname);
  
  device["name"] = "Curtain Controller " + device_hostname;
  device["model"] = "ESP32-C3 with A4988";
  device["manufacturer"] = "DIY";
  device["sw_version"] = "4.0-Refactored";
  
  String json;
  size_t json_size = serializeJson(doc, json);
  
  Serial.printf("[MQTT] Publishing HA discovery (%d bytes)\n", json_size);
  
  bool success = client.publish(discovery_topic.c_str(), json.c_str(), true);
  
  if (success) {
    preferences.putBool("ha_disc_done", true);
    Serial.println("[MQTT] ✓ HA discovery published");
  } else {
    Serial.println("[MQTT] ✗ HA discovery failed");
  }
}

void publish_config() {
  if (!client.connected()) return;
  
  StaticJsonDocument<512> doc;
  doc["step_delay_us"] = step_delay_us;
  doc["stepping_mode"] = stepping_mode;
  doc["mode_name"] = MICROSTEP_NAMES[stepping_mode];
  doc["position"] = current_position;
  doc["sleep_timeout_ms"] = motor_sleep_timeout;
  doc["hostname"] = device_hostname;
  doc["ip"] = WiFi.localIP().toString();
  doc["steps_per_rev"] = steps_per_revolution;
  doc["version"] = "4.0-Refactored";
  
  String json;
  serializeJson(doc, json);
  client.publish((mqtt_config_topic + "/response").c_str(), json.c_str());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.printf("[MQTT] Received '%s': %s\n", topic, msg.c_str());
  process_command(msg);
}

void connect_mqtt() {
  if (client.connected()) return;
  
  if (millis() - last_mqtt_attempt < mqtt_retry_delay) {
    return;
  }
  
  last_mqtt_attempt = millis();
  
  Serial.printf("[MQTT] Connecting (retry delay: %ds)...\n", mqtt_retry_delay / 1000);
  
  String client_id = device_hostname + "_" + WiFi.macAddress();
  client_id.replace(":", "");
  
  bool connected;
  if (mqtt_user.length() > 0) {
    connected = client.connect(client_id.c_str(), mqtt_user.c_str(), mqtt_password.c_str(),
                              mqtt_stat_topic.c_str(), 1, true, "offline");
  } else {
    connected = client.connect(client_id.c_str(), mqtt_stat_topic.c_str(), 1, true, "offline");
  }
  
  if (connected) {
    Serial.println("[MQTT] ✓ Connected");
    client.subscribe(mqtt_command_topic.c_str());
    client.subscribe(mqtt_config_topic.c_str());
    Serial.printf("[MQTT] Subscribed to: %s\n", mqtt_command_topic.c_str());
    
    publish_status("online");
    publish_position();
    publish_ha_discovery();
    
    mqtt_retry_delay = 2000;
  } else {
    Serial.printf("[MQTT] ✗ Failed (state: %d)\n", client.state());
    mqtt_retry_delay = min(mqtt_retry_delay * 2, MAX_MQTT_RETRY_DELAY);
  }
}

void setup_mqtt() {
  mqtt_server = preferences.getString("mqtt_server", "10.0.0.8");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "");
  mqtt_password = preferences.getString("mqtt_pass", "");
  
  String mqtt_root_topic = preferences.getString("mqtt_root_topic", "home/firstfloor/test/curtains");
  mqtt_command_topic = mqtt_root_topic + "/cmd";
  mqtt_stat_topic = mqtt_root_topic + "/status";
  mqtt_position_topic = mqtt_root_topic + "/position";
  mqtt_config_topic = mqtt_command_topic + "/config";

  client.setBufferSize(1024);
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
  const char* help;
};

void cmd_open(const String& param) {
  start_movement(steps_per_revolution);
  ws_println("Opening curtain...");
  
}

void cmd_close(const String& param) {
  start_movement(0);
  ws_println("Closing curtain...");
  
}

void cmd_stop(const String& param) {
  stop_movement("User command");
  ws_println("Stopped");
  
}

void cmd_position(const String& param) {
  int pos = param.toInt();
  if (pos >= 0 && pos <= steps_per_revolution) {
    start_movement(pos);
    ws_printf("Moving to position %d\n", pos);
    
  } else {
    Serial.printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    ws_printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    
  }
}

void cmd_set_speed(const String& param) {
  int value = param.toInt();
  if (value >= 2 && value <= 10000) {
    step_delay_us = value;
    preferences.putInt("step_delay", step_delay_us);
    Serial.printf("[Config] Speed set to %d us/step\n", step_delay_us);
    ws_printf("Speed set to %d us/step\n", step_delay_us);
    
    publish_config();
  } else {
    Serial.println("[ERROR] Speed must be 2-10000 us");
    ws_println("[ERROR] Speed must be 2-10000 us");
    
  }
}

void cmd_set_mode(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= 4) {
    set_microstepping_mode(value);
    preferences.putInt("step_mode", stepping_mode);
    ws_printf("Mode set to %d (%s)\n", stepping_mode, MICROSTEP_NAMES[stepping_mode]);
    
    publish_config();
  } else {
    Serial.println("[ERROR] Mode must be 0-4");
    ws_println("[ERROR] Mode must be 0-4");
    
  }
}

void cmd_set_steps(const String& param) {
  int value = param.toInt();
  if (value > 0 && value <= 20000) {
    steps_per_revolution = value;
    preferences.putInt("steps_per_rev", steps_per_revolution);
    Serial.printf("[Config] Steps per revolution: %d\n", steps_per_revolution);
    ws_printf("Steps per revolution: %d\n", steps_per_revolution);
    
    publish_config();
  } else {
    Serial.println("[ERROR] Steps must be 1-20000");
    ws_println("[ERROR] Steps must be 1-20000");
    
  }
}

void cmd_reset_position(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= steps_per_revolution) {
    current_position = value;
    save_position();
    publish_position();
    Serial.printf("[Config] Position reset to %d\n", current_position);
    ws_printf("Position reset to %d\n", current_position);
    
  } else {
    Serial.printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    ws_printf("[ERROR] Position must be 0-%d\n", steps_per_revolution);
    
  }
}

void cmd_set_sleep(const String& param) {
  int value = param.toInt();
  if (value >= 0 && value <= 300000) {
    motor_sleep_timeout = value;
    preferences.putULong("sleep_timeout", motor_sleep_timeout);
    Serial.printf("[Config] Sleep timeout: %lu ms\n", motor_sleep_timeout);
    ws_printf("Sleep timeout set to %lu ms\n", motor_sleep_timeout);
    
    publish_config();
  } else {
    Serial.println("[ERROR] Sleep timeout must be 0-300000 ms");
    ws_println("[ERROR] Sleep timeout must be 0-300000 ms (0-5 minutes)");
    
  }
}

void cmd_set_hostname(const String& param) {
  String new_hostname = param;
  new_hostname.trim();
  
  if (new_hostname.length() < 1 || new_hostname.length() > 40) {
    ws_println("[ERROR] Hostname must be 1-40 characters");
    
    return;
  }
  
  // Validate hostname characters
  for (unsigned int i = 0; i < new_hostname.length(); i++) {
    char c = new_hostname.charAt(i);
    if (!isalnum(c) && c != '-' && c != '_') {
      ws_println("[ERROR] Hostname can only contain letters, numbers, hyphens, and underscores");
      
      return;
    }
  }
  
  preferences.putString("hostname", new_hostname);
  ws_println("Hostname set to: " + new_hostname);
  ws_println("Rebooting in 3 seconds to apply...");
  
  
  if (client.connected()) {
    client.publish(mqtt_stat_topic.c_str(), "rebooting", true);
  }
  
  delay(3000);
  ESP.restart();
}

void cmd_wifi_info(const String& param) {
  ws_println("\n=== WiFi Info ===");
  ws_printf("SSID: %s\n", WiFi.SSID().c_str());
  ws_printf("IP: %s\n", WiFi.localIP().toString().c_str());
  ws_printf("MAC: %s\n", WiFi.macAddress().c_str());
  ws_printf("RSSI: %d dBm\n", WiFi.RSSI());
  ws_println("=================\n");
  
}

void cmd_mqtt_info(const String& param) {
  ws_println("\n=== MQTT Info ===");
  ws_printf("Server: %s:%d\n", mqtt_server.c_str(), mqtt_port);
  ws_printf("Connected: %s\n", client.connected() ? "Yes" : "No");
  if (!client.connected()) {
    ws_printf("State: %d\n", client.state());
  }
  ws_printf("Command Topic: %s\n", mqtt_command_topic.c_str());
  ws_printf("Status Topic: %s\n", mqtt_stat_topic.c_str());
  ws_println("=================\n");
  
}

void cmd_reset_driver(const String& param) {
  ws_println("Pulsing A4988 RESET pin...");
  
  
  digitalWrite(RESET_PIN, LOW);
  delayMicroseconds(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(2);
  
  Serial.println("[Motor] Driver reset complete");
  ws_println("Driver reset complete");
  
}

void cmd_ha_discovery(const String& param) {
  Serial.println("[HA] Republishing discovery...");
  ws_println("[HA] Republishing discovery...");
  publish_ha_discovery();
    // Force immediate output
}

void cmd_config(const String& param) {
  String output = "\n=== Configuration ===\n";
  output += "Hostname: " + device_hostname + "\n";
  output += "IP: " + WiFi.localIP().toString() + "\n";
  output += "Speed: " + String(step_delay_us) + " us/step\n";
  output += "Mode: " + String(stepping_mode) + " (" + String(MICROSTEP_NAMES[stepping_mode]) + ")\n";
  output += "Steps/Rev: " + String(steps_per_revolution) + "\n";
  output += "Position: " + String(current_position) + " (" + String((current_position * 100) / steps_per_revolution) + "%)\n";
  output += "Sleep Timeout: " + String(motor_sleep_timeout) + " ms\n";
  output += "MQTT: " + mqtt_server + ":" + String(mqtt_port) + "\n";
  output += "====================\n";
  
  Serial.print(output);
  ws_print(output);
  
  publish_config();
}

void cmd_restart(const String& param) {
  Serial.println("[System] Restarting in 2s...");
  ws_println("Restarting in 2 seconds...");
  
  delay(2000);
  ESP.restart();
}

void cmd_help(const String& param) {
  String help = "\n=== Available Commands ===\n";
  help += "Movement:\n";
  help += "  open                  - Open curtain\n";
  help += "  close                 - Close curtain\n";
  help += "  stop                  - Stop movement\n";
  help += "  position:<steps>      - Move to position\n";
  help += "\nConfiguration:\n";
  help += "  set:speed:<us>        - Set speed (2-10000 us/step)\n";
  help += "  set:mode:<0-4>        - Set microstepping mode\n";
  help += "  set:steps:<n>         - Set steps per revolution\n";
  help += "  set:position:<n>      - Reset current position\n";
  help += "  set:sleep:<ms>        - Set motor sleep timeout (0-300000 ms)\n";
  help += "  set:hostname:<name>   - Set device hostname (requires reboot)\n";
  help += "\nInformation:\n";
  help += "  status                - Show current status\n";
  help += "  config                - Show configuration\n";
  help += "  wifi                  - Show WiFi info\n";
  help += "  mqtt                  - Show MQTT info\n";
  help += "  ha_discovery          - Republish HA discovery\n";
  help += "\nUtilities:\n";
  help += "  reset_driver          - Pulse A4988 RESET pin\n";
  help += "  restart               - Restart ESP32\n";
  help += "  led:on                - Turn STATUS LED on\n";
  help += "  led:off               - Turn STATUS LED off\n";
  help += "  help                  - Show this help\n";
  help += "========================\n";
  
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
  output += "Speed: " + String(step_delay_us) + " us/step\n";
  output += "Mode: " + String(MICROSTEP_NAMES[stepping_mode]) + "\n";
  output += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  output += "Uptime: " + String(millis() / 1000) + " seconds\n";
  output += "==============\n";
  
  Serial.print(output);
  ws_print(output);
  
}

void cmd_led_on(const String& param) {
  led_manual_control = true;
  led_desired_state = LOW;  // LOW = LED on (inverted logic)
  digitalWrite(STATUS_LED, led_desired_state);
  Serial.println("[LED] STATUS LED turned ON (manual control)");
  ws_println("STATUS LED turned ON");
  
}

void cmd_led_off(const String& param) {
  led_manual_control = true;
  led_desired_state = HIGH;  // HIGH = LED off (inverted logic)
  digitalWrite(STATUS_LED, led_desired_state);
  Serial.println("[LED] STATUS LED turned OFF (manual control)");
  ws_println("STATUS LED turned OFF");
  
}

const Command commands[] = {
  {"open", cmd_open, "Open curtain"},
  {"close", cmd_close, "Close curtain"},
  {"stop", cmd_stop, "Stop movement"},
  {"position:", cmd_position, "Move to position (0-steps_per_rev)"},
  {"set:speed:", cmd_set_speed, "Set speed (2-10000 us)"},
  {"set:mode:", cmd_set_mode, "Set microstepping mode (0-4)"},
  {"set:steps:", cmd_set_steps, "Set steps per revolution"},
  {"set:position:", cmd_reset_position, "Reset current position"},
  {"set:sleep:", cmd_set_sleep, "Set motor sleep timeout (0-300000 ms)"},
  {"set:hostname:", cmd_set_hostname, "Set device hostname"},
  {"wifi", cmd_wifi_info, "Show WiFi information"},
  {"mqtt", cmd_mqtt_info, "Show MQTT information"},
  {"reset_driver", cmd_reset_driver, "Pulse A4988 RESET pin"},
  {"ha_discovery", cmd_ha_discovery, "Republish HA discovery"},
  {"config", cmd_config, "Show configuration"},
  {"status", cmd_status, "Show status"},
  {"restart", cmd_restart, "Restart ESP32"},
  {"help", cmd_help, "Show available commands"},
  {"led:on", cmd_led_on, "Turn STATUS LED on"},
  {"led:off", cmd_led_off, "Turn STATUS LED off"},
  {nullptr, nullptr, nullptr}
};

void process_command(const String& cmd) {
  String command = cmd;
  command.trim();
  command.toLowerCase();
  
  if (command.length() == 0) return;
  
  // Debug: Show what we're trying to match
  Serial.printf("[Command] Processing: '%s' (length: %d)\n", command.c_str(), command.length());
  
  // Find matching command
  for (int i = 0; commands[i].name != nullptr; i++) {
    String cmd_name = String(commands[i].name);
    
    if (cmd_name.endsWith(":")) {
      // Command with parameter
      if (command.startsWith(cmd_name)) {
        String param = command.substring(cmd_name.length());
        Serial.printf("[Command] Matched: '%s' with param: '%s'\n", cmd_name.c_str(), param.c_str());
        commands[i].handler(param);
        return;
      }
    } else {
      
      if (command == cmd_name) {
        Serial.printf("[Command] Matched: '%s'\n", cmd_name.c_str());
        commands[i].handler("");
        return;
      }
    }
  }
  
  // Debug: Show all available commands for comparison
  Serial.printf("[ERROR] Unknown command: '%s'\n", command.c_str());
  Serial.println("[DEBUG] Available commands:");
  for (int i = 0; commands[i].name != nullptr; i++) {
    Serial.printf("  - '%s'\n", commands[i].name);
  }
  ws_printf("[ERROR] Unknown command: '%s' (type 'help' for commands)\n", command.c_str());
}

// ============================================================================
// WEBSERIAL
// ============================================================================

void setup_webserial() {
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

      // Store command for later processing
      ws_pending_command = command;
      ws_command_pending = true;
  });


  WebSerial.begin(&server);
  server.begin();

  Serial.println("[WebSerial] Started on port 80");
  Serial.printf("[WebSerial] URL: http://%s/webserial\n", WiFi.localIP().toString().c_str());
  Serial.println("[WebSerial] Available commands: help, open, close, stop, status, config");
  Serial.println("[WebSerial] Note: No password protection - secure your WiFi network");
}


// ============================================================================
// WIFI & NETWORK
// ============================================================================

void setup_wifi_manager() {
  WiFiManager wm;
  
  bool shouldSaveConfig = false;
  wm.setSaveConfigCallback([&shouldSaveConfig]() {
    shouldSaveConfig = true;
  });

  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(60);

  device_hostname = preferences.getString("hostname", "ESP32-Curtain");
  mqtt_server = preferences.getString("mqtt_server", "10.0.0.8");
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  mqtt_user = preferences.getString("mqtt_user", "");
  mqtt_password = preferences.getString("mqtt_pass", "");
  String mqtt_root_topic = preferences.getString("mqtt_root_topic", "home/firstfloor/test/curtains");
  String ota_password = preferences.getString("ota_pass", "");
  int steps_pref = preferences.getInt("steps_per_rev", steps_per_revolution);

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  esp_netif_set_hostname(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), device_hostname.c_str());
  WiFi.setHostname(device_hostname.c_str());

  WiFiManagerParameter hostname_param("hostname", "Device Hostname", device_hostname.c_str(), 40);
  WiFiManagerParameter mqtt_server_param("server", "MQTT Server IP", mqtt_server.c_str(), 40);
  WiFiManagerParameter mqtt_port_param("port", "MQTT Port", String(mqtt_port).c_str(), 6);
  WiFiManagerParameter mqtt_user_param("user", "MQTT Username", mqtt_user.c_str(), 40);
  WiFiManagerParameter mqtt_pass_param("password", "MQTT Password", mqtt_password.c_str(), 40);
  WiFiManagerParameter mqtt_root_topic_param("mqtt_root_topic", "MQTT Root Topic", mqtt_root_topic.c_str(), 80);
  WiFiManagerParameter ota_pass_param("ota_pass", "OTA Password", ota_password.c_str(), 40);
  WiFiManagerParameter steps_param("steps_per_rev", "Steps per Revolution", String(steps_pref).c_str(), 8);

  wm.addParameter(&hostname_param);
  wm.addParameter(&mqtt_server_param);
  wm.addParameter(&mqtt_port_param);
  wm.addParameter(&mqtt_user_param);
  wm.addParameter(&mqtt_pass_param);
  wm.addParameter(&mqtt_root_topic_param);
  wm.addParameter(&ota_pass_param);
  wm.addParameter(&steps_param);

  if (!wm.autoConnect("ESP32-Curtain-Setup", "12345678")) {
    Serial.println("[WiFi] Failed to connect - restarting");
    delay(5000);
    ESP.restart();
  }

  if (shouldSaveConfig) {
    device_hostname = String(hostname_param.getValue());
    mqtt_server = String(mqtt_server_param.getValue());
    mqtt_port = String(mqtt_port_param.getValue()).toInt();
    mqtt_user = String(mqtt_user_param.getValue());
    mqtt_password = String(mqtt_pass_param.getValue());
    String mqtt_root = String(mqtt_root_topic_param.getValue());
    String ota_pw_new = String(ota_pass_param.getValue());
    steps_per_revolution = String(steps_param.getValue()).toInt();
    if (steps_per_revolution <= 0) steps_per_revolution = 2000;

    preferences.putString("hostname", device_hostname);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putInt("mqtt_port", mqtt_port);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_pass", mqtt_password);
    preferences.putString("mqtt_root_topic", mqtt_root);
    preferences.putInt("steps_per_rev", steps_per_revolution);
    if (ota_pw_new.length() > 0) {
      preferences.putString("ota_pass", ota_pw_new);
    }

    Serial.println("[WiFi] Settings saved - rebooting");
    delay(3000);
    ESP.restart();
  }

  Serial.println("[WiFi] ✓ Connected");
  Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  wifi_state = WIFI_CONNECTED;
}

void handle_wifi_reconnection() {
  switch (wifi_state) {
    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection lost");
        wifi_state = WIFI_DISCONNECTED;
      }
      break;
      
    case WIFI_DISCONNECTED:
      Serial.println("[WiFi] Reconnecting...");
      WiFi.disconnect();
      WiFi.setHostname(device_hostname.c_str());
      WiFi.begin();
      wifi_reconnect_start = millis();
      wifi_state = WIFI_RECONNECTING;
      break;
      
    case WIFI_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] ✓ Reconnected");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        wifi_state = WIFI_CONNECTED;
        
        MDNS.end();
        setup_mdns();
      }
      else if (millis() - wifi_reconnect_start > WIFI_RECONNECT_TIMEOUT) {
        Serial.println("[WiFi] Reconnect timeout - restarting");
        ESP.restart();
      }
      break;
  }
}

void setup_mdns() {
  if (!MDNS.begin(device_hostname.c_str())) {
    Serial.println("[mDNS] Failed to start");
    return;
  }
  
  Serial.printf("[mDNS] Started: %s.local\n", device_hostname.c_str());
  
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
  } else {
    ArduinoOTA.setPasswordHash("8f7e5b2a9d4c1e3f6b8a7d9c2e5f8a1b");
  }
  
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Update starting");
    if (is_moving) {
      stop_movement("OTA update");
    }
    stop_motor();
    if (client.connected()) {
      client.disconnect();
    }
    esp_task_wdt_delete(NULL);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned long last_print = 0;
    if (millis() - last_print > 2000) {
      Serial.printf("[OTA] Progress: %u%%\n", (progress / (total / 100)));
      last_print = millis();
    }
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready on %s.local:3232\n", device_hostname.c_str());
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
        Serial.println("[Button] Cannot reset while moving");
        // Skip LED blink if manual control
        if (!led_manual_control) {
          digitalWrite(STATUS_LED, LOW);
          delay(100);
          digitalWrite(STATUS_LED, HIGH);
        }
        last_stable_state = new_stable_state;
        return;
      }
      
      button_press_start = millis();
      Serial.println("[Button] Press detected - hold 5s to reset");
    }
    else if (new_stable_state == LOW && last_stable_state == LOW) {
      unsigned long hold_time = millis() - button_press_start;
      
      // Skip blinking if manual control
      if (hold_time > 1000 && !reset_in_progress && !led_manual_control) {
        digitalWrite(STATUS_LED, (millis() / 250) % 2);
      }
      
      if (hold_time >= RESET_HOLD_TIME && !reset_in_progress) {
        reset_in_progress = true;
        Serial.println("[System] FACTORY RESET");
        
        // Skip factory reset blinks if manual control
        if (!led_manual_control) {
          for (int i = 0; i < 10; i++) {
            digitalWrite(STATUS_LED, HIGH);
            delay(100);
            digitalWrite(STATUS_LED, LOW);
            delay(100);
          }
        }
        
        if (client.connected()) client.disconnect();
        WiFi.disconnect(true);
        
        WiFiManager wm;
        wm.resetSettings();
        preferences.clear();
        
        delay(1000);
        ESP.restart();
      }
    }
    else if (new_stable_state == HIGH && last_stable_state == LOW) {
      reset_in_progress = false;
      // Skip LED off if manual control
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
  
  Serial.println("\n=====================================");
  Serial.println("ESP32-C3 Curtain Controller v4.0");
  Serial.println("Complete Refactor Edition");
  Serial.println("=====================================");

  preferences.begin("curtains", false);

  current_position = preferences.getInt("position", 0);
  step_delay_us = preferences.getInt("step_delay", 2000);
  stepping_mode = preferences.getInt("step_mode", 4);
  motor_sleep_timeout = preferences.getULong("sleep_timeout", 30000);
  steps_per_revolution = preferences.getInt("steps_per_rev", 2000);

  Serial.printf("[Config] Position: %d\n", current_position);
  Serial.printf("[Config] Speed: %d us/step\n", step_delay_us);
  Serial.printf("[Config] Mode: %s\n", MICROSTEP_NAMES[stepping_mode]);
  Serial.printf("[Config] Steps/Rev: %d\n", steps_per_revolution);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(STATUS_LED, HIGH);  // LED off (inverted)

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
  Serial.printf("[Watchdog] Enabled (%ds timeout)\n", WDT_TIMEOUT);

  Serial.println("=====================================");
  Serial.println("[System] Ready!");
  Serial.println("=====================================");
  Serial.printf("[Network] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[Network] Hostname: %s.local\n", device_hostname.c_str());
  Serial.printf("[WebSerial] http://%s/webserial\n", WiFi.localIP().toString().c_str());
  Serial.printf("[OTA] %s.local:3232\n", device_hostname.c_str());
  Serial.println("=====================================\n");
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
