// Version 1.3, 05.08.2024, coderpussy
const char* APP_VERSION = "1.3";

// Correction for compass direction of pcb mount
#define Az_PCB_Correction 90

// Wifi: Select AP or Client
#define WiFiMode_AP_STA 0                               // Defines WiFi Mode 0 -> AP (with IP:192.168.4.1 and  1 -> Station (client with IP: via DHCP)

// Enter your AP SSID and PASSWORD
const char* ssid_ap = "satfinder";                      // Set AP SSID name
const char* password_ap = "xxxxxxxx";                   // Set AP password

// Enter your STA SSID and PASSWORD
const char* ssid_sta = "My-WLAN";                       // Set STA SSID name
const char* password_sta = "xxxxxxxx";                  // Set STA password

// Astra 19.2 position and dish specific offsets
float Astra_Az = 173.34,
      Astra_El = 29.40,
      El_Offset = -18.00,
      Az_Offset = -10.00;

// Actuator speed
int motorSpeed = 700;                                   // Change (lower) this if dish is begining to swing

// Define config file on LittleFS
const char* filename = "/settings.json";
