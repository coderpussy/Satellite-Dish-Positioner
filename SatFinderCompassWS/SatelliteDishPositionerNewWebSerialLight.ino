/*
  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// ESP8266 Diseqc SatFinder
// Controls satellite dish direction with Diseqc rotor (azimut) and linear actuator (elevation)
// Uses an MPU6050 device for Elevation and a QMC5883L compass for Azimut control.

// History
// Version 1.1, 19.05.2021, AK-Homberger
// Version 1.2 pre1, 07.04.2024, coderpussy
// Version 1.2 pre2, 24.06.2024, coderpussy

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerialLite.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// #include <ESP8266WiFiGratuitous.h>
#include <ArduinoOTA.h>
#include <TinyMPU6050.h>
#include <QMC5883LCompass.h>

//    SDA => D2
//    SCL => D1

#define datapin D5      // Digital pin for 22 kHz signal D5
#define motor1 D6       // Linear Actuator 1
#define motor2 D0       // Linear Actuator 2
#define UP 1
#define DOWN 2

#define Az_PCB_Correction 90  // Correction for compass direction of pcb mount

// Wifi: Select AP or Client
#define WiFiMode_AP_STA 0            // Defines WiFi Mode 0 -> AP (with IP:192.168.4.1 and  1 -> Station (client with IP: via DHCP)

//Enter your AP SSID and PASSWORD
const char *ssid_ap = "satfinder";     // Set WLAN name
const char *password_ap = "levin1989";  // Set password

//Enter your STA SSID and PASSWORD
const char* ssid_sta = "Superpueppi-WLAN";
const char* password_sta = "10250562589012613526";

float Astra_Az = 173.34, Astra_El = 29.40, El_Offset = -18, Az_Offset = -10.0; // Astra 19.2 position and dish specific offsets
int motorSpeed = 700;  // Actuator speed     // Change (lower) this if dish is begining to swing

// Define config file on LittleFS
const char *filename = "/settings.json";

// variables to save values from HTML form
const char* act;
String action;

float Azimut = 0, Elevation = 0;
float sAzimut = 0, sElevation = 0;
float dAzimut = 0, dElevation = 0;
float IsRotor = 0, RotorPos = 0;

bool auto_on = false;
bool update_rotor = false;
bool rotor_changed = false;
bool rotor_off = false;

unsigned long comp_on_time = 0, motor_on_time = 0;

int motor_error = 0;
int LED_level = 0;

bool om_elevator = false;
int om_el_dir;
int om_time = 0;
int om_speed = 0;

// Timer variables
unsigned long lastTime = 0;  
unsigned long timerDelay = 1000;

QMC5883LCompass compass;
MPU6050 mpu (Wire);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Initialize LittleFS
void initFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
  } else {
    Serial.println("LittleFS mounted successfully");
  }
}

// Initialize WiFi
void initWiFi() {
  bool LED = false;
  int wifi_retry = 0;
  
  // Init WLAN AP or STA
  if (WiFiMode_AP_STA == 0) {

    WiFi.mode(WIFI_AP);                              // WiFi Mode Access Point
    delay (100);
    WiFi.softAP(ssid_ap, password_ap);                     // AP name and password
    Serial.println("Start WLAN AP");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());

  } else {

    Serial.println("Start WLAN Client DHCP");         // WiFi Mode Client with DHCP
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_sta, password_sta);

    while (WiFi.status() != WL_CONNECTED) {           // Check connection
      wifi_retry++;
      Serial.print(".");
      delay(1000);
      digitalWrite(D4, LED);
      LED = ! LED;
      if (wifi_retry > 10) {
        Serial.println("\nReboot");                   // Reboot after 10 connection tries
        ESP.restart();
      }
    }
    digitalWrite(D4, HIGH);

    // If connection successful show IP address in serial monitor
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid_sta);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());  //IP address assigned to your ESP
  }
}

// Initialze Compass
void initCompass() {
  delay(1000);
  Serial.println();
  Serial.println("Initialise Compass...");

  compass.init();
  compass.setCalibration(-1122, 1770, -1788, 1171, -1633, 1387);   // Do a calibration for the compass and put your values here!!!
  compass.setSmoothing(10, true);
}

// Initialze MPU
void initMPU() {
  delay(1000);
  Serial.println("Initialise MPU...");
  
  mpu.Initialize();
  //Serial.println("Starting calibration...");
  //mpu.Calibrate();
  //Serial.println("Calibration complete!");
}

// Message callback of WebSerial
void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
}

void notifyClients() {
  //const uint8_t size = JSON_OBJECT_SIZE(1);
  StaticJsonDocument<48> json;
  
  json["action"] = "success";
  WebSerial.print(F("success\n"));

  char data[24];
  serializeJson(json, data);
  ws.textAll(data);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    const uint8_t size = JSON_OBJECT_SIZE(6);
    StaticJsonDocument<size> json;
    DeserializationError err = deserializeJson(json, data);
    
    if (err) {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(err.c_str());
      WebSerial.print(F("deserializeJson() failed with code "));
      WebSerial.println(err.c_str());
      return;
    }

    act = json["action"];
    action = act;

    Serial.print("action: ");
    Serial.println(action);
    WebSerial.print(F("action: "));
    WebSerial.println(action);
    
    if (action == "getvalues") handleGetData();
    else if (action == "getsettings") handleGetSettings();
    else if (action == "setsettings") handleSetSettings(json);
    else if (action == "on") handleOn();
    else if (action == "off") handleOff();
    else if (action == "r_off") handleRotorOff();
    else if (action == "az_up") handleAzUp();
    else if (action == "az_down") handleAzDown();
    else if (action == "el_up") handleElUp();
    else if (action == "el_down") handleElDown();
    else if (action == "om_el_up") handleOmElUp(json);
    else if (action == "om_el_down") handleOmElDown(json);
    else if (action == "rotor_up") handleRotorUp();
    else if (action == "rotor_down") handleRotorDown();
    else if (action == "rotor_up_step") handleRotorUpStep();
    else if (action == "rotor_down_step") handleRotorDownStep();
    else if (action == "cal") handleCal();
    else if (action == "slider1") handleSlider1(json);
    else if (action == "slider2") handleSlider2(json);
    else if (action == "slider3") handleSlider3(json);
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup(void) {
  Serial.begin(115200);

  initFS();
  initWiFi();

  initWebSocket();
  
  initCompass();
  initMPU();

  pinMode(datapin, OUTPUT);
  pinMode(motor1, OUTPUT);
  pinMode(motor2, OUTPUT);
  digitalWrite(motor1, HIGH);
  digitalWrite(motor2, HIGH);
  pinMode(D4, OUTPUT);
  digitalWrite(D4, HIGH);

  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  // Attach Message Callback
  WebSerial.onMessage(recvMsg);
  
  //experimental::ESP8266WiFiGratuitous::stationKeepAliveSetIntervalMs(5000);

  // Arduino OTA config and start
  ArduinoOTA.setHostname("Satfinder");
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, handleRoot);      //This is display page
  server.onNotFound(handleNotFound);
  
  server.serveStatic("/", LittleFS, "/");
  server.serveStatic("/css/", LittleFS, "/css/");
  server.serveStatic("/js/", LittleFS, "/js/");
  
  server.begin();                  //Start server
  Serial.println("HTTP server started");
  WebSerial.print(F("HTTP server started\n"));

  loadConfiguration(filename);
  
  sAzimut = Astra_Az;
  sElevation = Astra_El;
}

void handleRoot(AsyncWebServerRequest *request) {
  request->send(LittleFS, "/index.html", "text/html"); //Send index web page
}

void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "File Not Found\n\n"); // Unknown request. Send error 404
}

void handleGetSettings() {
  String Text;

  const uint8_t size = JSON_OBJECT_SIZE(7);
  StaticJsonDocument<size> root;

  root["action"] = action;
  root["azimut"] = Astra_Az;
  root["elevation"] = Astra_El;
  root["el_offset"] = El_Offset;
  root["az_offset"] = Az_Offset;
  root["motor_speed"] = motorSpeed;

  serializeJson(root, Text);

  ws.textAll(Text); //Send values to websocket clients 
}

void handleSetSettings(const JsonDocument& json) {
  float tmp;
  String Text;

  tmp = float(json["azimut"]);
  if ((tmp >= 90) && (tmp <= 270)) Astra_Az = tmp;
  
  tmp = float(json["elevation"]);
  if ((tmp >= 10) && (tmp <= 50)) Astra_El = tmp;
  
  tmp = float(json["el_offset"]);
  if (abs(tmp) <= 90) El_Offset = tmp;
  
  tmp = float(json["az_offset"]);
  if (abs(tmp) <= 90) Az_Offset = tmp;
  
  tmp = float(json["motor_speed"]);
  if ((tmp >= 500) && (tmp < 1024)) motorSpeed = trunc(tmp);

  saveConfiguration(filename);

  serializeJson(json, Text);

  ws.textAll(Text);
}

void handleGetData() {
  String Text;

  const uint8_t size = JSON_OBJECT_SIZE(11);
  StaticJsonDocument<size> root;

  root["action"] = action;
  
  root["azimut"] = Azimut;
  root["elevation"] = Elevation;

  root["s_azimut"] = sAzimut;
  root["s_elevation"] = sElevation;

  root["d_azimut"] = dAzimut;
  root["d_elevation"] = dElevation;

  root["rotor"] = IsRotor;
  root["led_level"] = LED_level;

  if (auto_on) root["state"] = "On";
  else root["state"] = "Off";
  
  if (auto_on && rotor_off) root["state"] = "R-Off";

  serializeJson(root, Text);

  ws.textAll(Text); //Send sensors values to websocket clients
}

void handleCal() {
  Serial.println("Starting calibration...");
  WebSerial.print(F("Starting calibration...\n"));
  mpu.Calibrate();
  Serial.println("Calibration complete!");
  WebSerial.print(F("Calibration complete!\n"));
  notifyClients();
}

void handleAzDown() {
  if (dAzimut > -50) dAzimut -= 0.5;
  update_rotor = true;
  notifyClients();
}

void handleAzUp() {
  if (dAzimut < 50) dAzimut += 0.5;
  update_rotor = true;
  notifyClients();
}

void handleElDown() {
  if (dElevation > -8) dElevation -= 0.1;
  notifyClients();
}

void handleElUp() {
  if (dElevation < 8) dElevation += 0.1;
  notifyClients();
}

void handleRotorDown() {
  if (RotorPos > -70) RotorPos -= 1;
  update_rotor = true;
  notifyClients();
}

void handleRotorUp() {
  if (RotorPos < 70) RotorPos += 1;
  update_rotor = true;
  notifyClients();
}

void handleRotorDownStep() {
  noInterrupts();
  write_byte_with_parity(0xE0);
  write_byte_with_parity(0x31);
  write_byte_with_parity(0x69);
  write_byte_with_parity(0xfe);
  interrupts();

  notifyClients();
  
  comp_on_time = millis() + 1000;
  rotor_changed = true;
  IsRotor = IsRotor + 1.0 / 8.0;
}

void handleRotorUpStep() {
  noInterrupts();
  write_byte_with_parity(0xE0);
  write_byte_with_parity(0x31);
  write_byte_with_parity(0x68);
  write_byte_with_parity(0xfe);
  interrupts();

  notifyClients();
  
  comp_on_time = millis() + 1000;
  rotor_changed = true;
  IsRotor = IsRotor - 1.0 / 8.0;
}

void handleOn() {
  digitalWrite(D4, LOW);

  notifyClients();
  
  auto_on = true;
  rotor_off = false;
  motor_error = 0;
}

void handleOff() {
  auto_on = false;
  rotor_off = true;
  digitalWrite(D4, HIGH);

  notifyClients();
  
  noInterrupts();
  write_byte_with_parity(0xE0);
  write_byte_with_parity(0x31);
  write_byte_with_parity(0x60);
  interrupts();
  comp_on_time = 0;
  digitalWrite(motor2, HIGH);
  digitalWrite(motor1, HIGH);
}

void handleRotorOff() {
  rotor_off = true;

  notifyClients();

  noInterrupts();
  write_byte_with_parity(0xE0);
  write_byte_with_parity(0x31);
  write_byte_with_parity(0x60);
  interrupts();
  comp_on_time = 0;
  digitalWrite(motor2, HIGH);
  digitalWrite(motor1, HIGH);
}

void handleSlider1(const JsonDocument& json) {
  dAzimut = float(json["level"]);
  Serial.print("Azimut: ");
  Serial.println(Azimut);
  WebSerial.print(F("Azimut: "));
  WebSerial.println(String(Azimut));
  //update_rotor = true;
  
  notifyClients();
}

void handleSlider2(const JsonDocument& json) {
  dElevation = float(json["level"]);
  Serial.print("Elevation: ");
  Serial.println(Elevation);
  WebSerial.print(F("Elevation: "));
  WebSerial.println(String(Elevation));
  
  notifyClients();
}

void handleSlider3(const JsonDocument& json) {
  RotorPos = float(json["level"]);
  Serial.print("Rotor: ");
  Serial.println(RotorPos);
  WebSerial.print(F("Rotor: "));
  WebSerial.println(String(RotorPos));
  update_rotor = true;
  
  notifyClients();
}

void handleOmElUp(const JsonDocument& json) {
  auto_on = false;
  om_el_dir = UP;
  
  om_time = float(json["time"]);
  om_speed = float(json["speed"]);
  
  om_elevator = true;
  
  notifyClients();
}

void handleOmElDown(const JsonDocument& json) {
  auto_on = false;
  om_el_dir = DOWN;
  
  om_time = float(json["time"]);
  om_speed = float(json["speed"]);
  
  om_elevator = true;
  
  notifyClients();
}

void om_motor(int direction) {
  if (direction == UP) {
    unsigned long stop = millis();
    while (millis() - stop < om_time) {
      digitalWrite(motor2, LOW);
      analogWrite(motor1, om_speed);
    }
  }

  if (direction == DOWN) {
    unsigned long stop = millis();
    while (millis() - stop < om_time) {
      digitalWrite(motor1, LOW);
      analogWrite(motor2, om_speed);
    }
  }

  digitalWrite(motor1, HIGH);
  digitalWrite(motor2, HIGH);
}

// Loads the configuration from a file
void loadConfiguration(const char *filename) {
  float tmp = 0;
  
  // Open file for reading
  File file = LittleFS.open(filename, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  const uint8_t size = JSON_OBJECT_SIZE(5);
  StaticJsonDocument<size> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  
  if (error) {
    Serial.println(F("Failed to read file, using default configuration"));
    WebSerial.print(F("Failed to read file, using default configuration\n"));
  }

  // Copy values from the JsonDocument to the Config
  tmp = doc["Astra_Az"];
  if ((tmp >= 90) && (tmp <= 270)) Astra_Az = tmp;
  
  tmp = doc["Astra_El"];
  if ((tmp >= 10) && (tmp <= 50)) Astra_El = tmp;
  
  tmp = doc["El_Offset"];
  if (abs(tmp) <= 90) El_Offset = tmp;
  
  tmp = doc["Az_Offset"];
  if (abs(tmp) <= 90) Az_Offset = tmp;
  
  tmp = doc["motorSpeed"];
  if ((tmp >= 500) && (tmp < 1024)) motorSpeed = trunc(tmp);

  // Close the file
  file.close();
}

// Saves the configuration to a file
void saveConfiguration(const char *filename) {
  // Delete existing file, otherwise the configuration is appended to the file
  LittleFS.remove(filename);

  // Open file for writing
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Serial.println(F("Failed to create file"));
    WebSerial.print(F("Failed to create file\n"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  const uint8_t size = JSON_OBJECT_SIZE(5);
  StaticJsonDocument<size> doc;
  
  // Set the values in the document
  doc["Azimut"] = Astra_Az;
  doc["Elevator"] = Astra_El;
  doc["El_Offset"] = El_Offset;
  doc["Az_Offset"] = Az_Offset;
  doc["motorSpeed"] = motorSpeed;
  
  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
    WebSerial.print(F("Failed to write to file\n"));
  }

  // Close the file
  file.close();
}

void write0() {                      // write a '0' bit toneburst
  for (int i = 1; i <= 22; i++) {    // 1 ms of 22 kHz (22 cycles)
    digitalWrite(datapin, HIGH);
    delayMicroseconds(21);
    digitalWrite(datapin, LOW);
    delayMicroseconds(21);
  }
  delayMicroseconds(500);             // 0.5 ms of silence
}

void write1() {                      // write a '1' bit toneburst
  for (int i = 1; i <= 11; i++) {    // 0.5 ms of 22 kHz (11 cycles)
    digitalWrite(datapin, HIGH);
    delayMicroseconds(21);
    digitalWrite(datapin, LOW);
    delayMicroseconds(21);
  }
  delayMicroseconds(1000);            // 1 ms of silence
}

// Calculate parity of a byte
bool parity_even_bit(byte x) {
  unsigned int count = 0, i, b = 1;

  for (i = 0; i < 8; i++) {
    if ( x & (b << i) ) {
      count++;
    }
  }
  if ( (count % 2) ) {
    return 0;
  }
  return 1;
}

// write the parity of a byte (as a toneburst)
void write_parity(byte x) {
  if (parity_even_bit(x)) write0(); else write1();
}

// write out a byte (as a toneburst)
// high bit first (ie as if reading from the left)
void write_byte(byte x) {
  for (int j = 7; j >= 0; j--) {
    if (x & (1 << j)) write1(); else write0();
  }
}

// write out a byte with parity attached (as a toneburst)
void write_byte_with_parity(byte x) {
  write_byte(x);
  write_parity(x);
}

// goto position angle a in degrees, south = 0.
// (a must be in the range +/- 75 degrees)
void goto_angle(float a) {
  /*
    Note the diseqc "goto x.x" command is not well documented.
    The general decription is available at https://de.eutelsat.com/en/support/technical-support/diseqc.html
    See "Positioner Application Note.pdf".
  */

  byte n1, n2, n3, n4, d1, d2;
  int a16;
  
  // get the angle in range +/- 75 degrees.  Sit at these limits and switch
  // over at ~ midnight unless otherwise instructed.
  if (a < -75.0) {
    a = -75;
  }
  if (a > 75.0) {
    a = 75;
  }

  // set the sign nibble in n1 to E (east) or D (west).
  if (a < 0) {
    n1 = 0xE0;
  } else {
    n1 = 0xD0;
  }
  
  // shift everything up so the fraction (1/16) nibble is in the
  //integer, and round to the nearest integer:
  a16 =  (int) (16.0 * abs(a) + 0.5);
  // n2 is the top nibble of the three-nibble number a16:
  n2 = (a16 & 0xF00) >> 8;
  // the second data byte is the bottom two nibbles:
  d2 = a16 & 0xFF;
  //the first data byte is
  d1 = n1 | n2;
  
  // send the command to the positioner
  noInterrupts();
  write_byte_with_parity(0xE0);
  write_byte_with_parity(0x31);
  write_byte_with_parity(0x6E);
  write_byte_with_parity(d1);
  write_byte_with_parity(d2);
  interrupts();
}

void motor(int direction) {
  float X = 0, Y = 0;

  mpu.Execute();
  X = round(mpu.GetAngX() * 10) / 10;

  if (direction == UP) {
    digitalWrite(motor2, LOW);
    analogWrite(motor1, motorSpeed);
    delay(10);
    mpu.Execute();
    Y = round(mpu.GetAngX() * 10) / 10;
  }

  if (direction == DOWN) {
    digitalWrite(motor1, LOW);
    analogWrite(motor2, motorSpeed - 500);
    delay(10);
    mpu.Execute();
    Y = round(mpu.GetAngX() * 10) / 10;
  }

  digitalWrite(motor1, HIGH);
  digitalWrite(motor2, HIGH);

  delay(10);
  //comp_on_time = millis() + 500;
  //rotor_changed = true;
  if ( X == Y ) motor_error++; else motor_error = 0;
}

void loop(void) {
  ArduinoOTA.handle();
  mpu.Execute();
  compass.read();

  sAzimut = Astra_Az + dAzimut;
  sElevation = Astra_El + dElevation;

  dAzimut = round(dAzimut * 10) / 10;
  dElevation = round(dElevation * 10) / 10;

  sAzimut = round(sAzimut * 10) / 10;
  sElevation = round(sElevation * 10) / 10;

  if (rotor_changed && (millis() > comp_on_time )) {
    rotor_changed = false;
    Serial.println("Compass On");
    WebSerial.print(F("Compass On\n"));
  }

  if (!rotor_changed && !rotor_off)  {
    Azimut = compass.getAzimuth() - Az_PCB_Correction - Az_Offset;
    if (Azimut < 0) Azimut += 360;
    if (Azimut > 360) Azimut -= 360;
  }

  Elevation = -round((mpu.GetAngX() + El_Offset) * 10) / 10;
  if (Elevation < 0 ) Elevation = 0;

  if (auto_on) {
    RotorPos = sAzimut - Azimut;
    if (RotorPos > 70) RotorPos = 70;
    if (RotorPos < -70) RotorPos = -70;

    if (motor_error < 20) {
      if (sElevation - Elevation > 0.3) {
        motor(UP);
      }
      else if (sElevation - Elevation < -0.3) {
        motor(DOWN);
      }
    }
  }

  if (om_elevator) {
    if (om_el_dir == UP) {
      om_motor(UP);
    } else {
      om_motor(DOWN);
    }
    om_elevator = false;
  }
  
  if ((!rotor_off && abs(RotorPos - IsRotor) > 1) || update_rotor) {
    comp_on_time = millis() + (abs(RotorPos - IsRotor) * 700);
    rotor_changed = true;

    goto_angle(-RotorPos);
    Serial.print("RotorPos: ");
    Serial.println(RotorPos);
    WebSerial.print(F("RotorPos: "));
    WebSerial.println(String(RotorPos));
    IsRotor = RotorPos;
    update_rotor = false;
  }

  if ((millis() - lastTime) > timerDelay) {
    notifyClients();
    lastTime = millis();
  }
  
  ws.cleanupClients();
  
  delay(10);
}
