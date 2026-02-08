#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>      
#include <Wiegand.h>          

// --- PIN SETTINGS ---
#define LED_PIN LED_BUILTIN
const int RELAY_PIN = D1;
const int PIN_D0 = D6;        // Green Wire
const int PIN_D1 = D7;        // White Wire

// --- ODOO SERVER SETTINGS ---
const char* serverUrl = "https://mugie-client-qa-27687587.dev.odoo.com/api/card_read";
const char* apiKey    = "668275cff50c1c5fd30bf146a3e13c1f547fb0b6";
const char* deviceId  = "TMCL_0001"; 

// --- AP SETTINGS ---
const char* ap_ssid = "TMCL_R_C_0001";
const char* ap_password = "controller0001";

// --- OBJECTS ---
ESP8266WebServer server(80);
Wiegand wiegand;

// --- CONFIGURATION ---
const int RELAY_ON_STATE  = HIGH;  
const int RELAY_OFF_STATE = LOW;  

// --- TIME SETTINGS ---
const unsigned long AP_HIDE_DELAY = 300000; 

// --- BLINK SETTINGS ---
const int IDLE_PERIOD     = 2000;
const int IDLE_ON_TIME    = 20;  
const int FAST_BLINK_SPEED = 50;

// --- VARIABLES ---
unsigned long lastCheckTime = 0;
bool isApActive = true;          

// --- HELPER DECLARATIONS ---
void handleRoot();
void handleTrigger();
void handleWifiSetup();
void handleWifiSave();
void triggerRelaySequence();
void handleIdleLed();
void handleConnectionMonitor();
void sendDataToServer(unsigned long cardId);
void receivedData(uint8_t* data, uint8_t bits, const char* message);

// --- WIEGAND INTERRUPTS ---
void IRAM_ATTR p0Changed() { wiegand.setPinState(0, digitalRead(PIN_D0)); }
void IRAM_ATTR p1Changed() { wiegand.setPinState(1, digitalRead(PIN_D1)); }

void setup() {
  Serial.begin(115200);
  
  digitalWrite(RELAY_PIN, RELAY_OFF_STATE);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(PIN_D0, INPUT_PULLUP);
  pinMode(PIN_D1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_D0), p0Changed, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), p1Changed, CHANGE);
  
  wiegand.onReceive(receivedData, "Card Read: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  WiFi.begin();

  server.on("/", handleRoot);          
  server.on("/click", handleTrigger);  
  server.on("/setup", handleWifiSetup);
  server.on("/save", handleWifiSave);  
  
  server.begin();
}

void loop() {
  server.handleClient();      
  handleConnectionMonitor();  
  handleIdleLed();            
  wiegand.flush();            
}

void receivedData(uint8_t* data, uint8_t bits, const char* message) {
  unsigned long cardId = 0;
  for (int i = 0; i < (bits / 8); i++) {
    cardId <<= 8;
    cardId |= data[i];
  }
  sendDataToServer(cardId);
}

void sendDataToServer(unsigned long id) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    String hardwareID = String(ESP.getChipId(), HEX);
    hardwareID.toUpperCase();

    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Key", apiKey);

    StaticJsonDocument<512> doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "call"; 
    
    JsonObject params = doc.createNestedObject("params");
    params["card_id"] = String(id);
    params["hardware_id"] = hardwareID; // CHANGED: Now using real dynamic Chip ID
    params["direction"] = "out";     

    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpCode = http.POST(requestBody);
    
    if (httpCode > 0) {
      String response = http.getString();
      if (response.indexOf("\"status\": \"authorized\"") != -1) {
        triggerRelaySequence();
      }
    }
    http.end();
  }
}

void handleConnectionMonitor() {
  if (millis() - lastCheckTime > 5000) {
    lastCheckTime = millis();
    bool isMaintenanceWindow = millis() < AP_HIDE_DELAY;
    if (WiFi.status() == WL_CONNECTED) {
      if (!isMaintenanceWindow && isApActive) { 
        WiFi.softAPdisconnect(true); 
        WiFi.mode(WIFI_STA); 
        isApActive = false; 
      }
    } else if (!isApActive) { 
      WiFi.mode(WIFI_AP_STA); 
      WiFi.softAP(ap_ssid, ap_password); 
      isApActive = true; 
    }
  }
}

void triggerRelaySequence() {
  digitalWrite(RELAY_PIN, RELAY_ON_STATE);
  unsigned long startMillis = millis();
  unsigned long prevBlink = 0;
  bool ledState = false;
  while (millis() - startMillis < 500) {
    if (millis() - prevBlink >= FAST_BLINK_SPEED) {
      prevBlink = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    }
    yield();
  }
  digitalWrite(RELAY_PIN, RELAY_OFF_STATE);
  digitalWrite(LED_PIN, HIGH);
}

void handleIdleLed() {
  if ((millis() % IDLE_PERIOD) < IDLE_ON_TIME) digitalWrite(LED_PIN, LOW);
  else digitalWrite(LED_PIN, HIGH);
}

String getHeader() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=0'>";
  html += "<style>";
  html += "body { background:#1a1a1a; color:#eee; font-family:sans-serif; text-align:center; margin:0; padding:20px; }";
  html += ".card { background:#2a2a2a; padding:25px; border-radius:15px; box-shadow:0 4px 15px rgba(0,0,0,0.5); margin-bottom:20px; }";
  html += "h1 { margin:0; font-size:28px; color:#fff; }";
  html += ".hw-id { color:#00aaff; font-size:14px; font-weight:bold; letter-spacing:1px; margin-top:5px; }";
  html += ".btn { display:block; width:100%; padding:20px; margin:15px 0; font-size:18px; font-weight:bold; border:none; border-radius:10px; cursor:pointer; text-decoration:none; box-sizing:border-box; }";
  html += ".btn-blue { background:#007bff; color:white; }";
  html += ".btn-green { background:#28a745; color:white; }";
  html += "select, input { width:100%; padding:15px; font-size:16px; margin:10px 0; border-radius:8px; border:1px solid #444; background:#333; color:white; box-sizing:border-box; }";
  html += ".timer { color:#ffc107; font-size:12px; margin-top:10px; font-style:italic; }";
  html += ".status { font-size:14px; margin-top:20px; color:#888; }";
  html += ".online { color:#28a745; font-weight:bold; }";
  html += "</style></head><body>";
  return html;
}

void handleRoot() {
  String hardwareID = String(ESP.getChipId(), HEX);
  hardwareID.toUpperCase();
  String displayId = String(deviceId);
  displayId.replace("_", " ");
  String html = getHeader();
  html += "<div class='card'><h1>" + displayId + "</h1><div class='hw-id'>HW-ID: " + hardwareID + "</div>";
  if (WiFi.status() == WL_CONNECTED && millis() < AP_HIDE_DELAY) {
      int secondsLeft = (AP_HIDE_DELAY - millis()) / 1000;
      html += "<div class='timer'>Maintenance Mode: AP hides in " + String(secondsLeft/60) + "m " + String(secondsLeft%60) + "s</div>";
  }
  html += "</div><a href='/click' class='btn btn-blue'>TEST RELAY</a><a href='/setup' class='btn btn-green'>WIFI SETTINGS</a>";
  html += "<div class='status'>";
  if (WiFi.status() == WL_CONNECTED) html += "Status: <span class='online'>ONLINE</span><br>" + WiFi.SSID();
  else html += "Status: <span style='color:#ffc107'>LOCAL MODE</span>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleWifiSetup() {
  int n = WiFi.scanNetworks();
  String html = getHeader();
  html += "<div class='card'><h1>WiFi Setup</h1><p style='font-size:12px; color:#888;'>Select strongest signal (lower dBm is better)</p></div>";
  html += "<form action='/save' method='POST'><select name='ssid'>";
  for (int i = 0; i < n; ++i) {
    // CHANGED: Added RSSI (Signal strength) to the dropdown list
    int rssi = WiFi.RSSI(i);
    html += "<option value='" + WiFi.SSID(i) + "'>";
    html += WiFi.SSID(i) + " (" + String(rssi) + " dBm)";
    html += "</option>";
  }
  html += "</select><input type='password' name='pass' placeholder='WiFi Password'><input type='submit' class='btn btn-green' value='SAVE & CONNECT'></form>";
  html += "<a href='/' style='color:#888; text-decoration:none;'>&larr; Back to Menu</a></body></html>";
  server.send(200, "text/html", html);
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  WiFi.begin(ssid.c_str(), pass.c_str());
  String html = getHeader();
  html += "<div class='card'><h1>Connecting...</h1><p>Trying to join " + ssid + "</p></div><a href='/' class='btn btn-blue'>CHECK STATUS</a></body></html>";
  server.send(200, "text/html", html);
}

void handleTrigger() {
  triggerRelaySequence();
  server.sendHeader("Location", "/");
  server.send(303);
}