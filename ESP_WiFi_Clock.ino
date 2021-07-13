#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <EEPROM.h>

// — Settings —
const int MODE_PIN = 0; //Pin Used as trigger a mode change

// — System Varibles — System will change the variables as it is running
byte currentMode = 0; //TODO: Set to 0 in production
unsigned long turnOffLightTime = 0;
unsigned long modeChangePinHoldTime = 0;
unsigned long currentTime = 0;
unsigned long oldTime = 0;
unsigned long blinkInterval = 0;
unsigned long lastBlink = 0;
boolean isReady = false; //If the system is ready to run main loop
boolean triedConnectingToWifi = false;
boolean wifiPortalRunning = false;
boolean clientIsConnected = true;

// — Global Parameters —

// — Network Parameters —
ESP8266WebServer server(80); //Create instance of WebServer on port 80
WiFiManager wifiManager; //Create isntance of WiFiManager
WiFiUDP ntpUDP; //Opens a UDP Port
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000); //Starts a NTPClient to get the time. //TODO: replace number and add/subtract based on timezone & DST
char* ESPWifiSSID;
String esp_ssid;

// — System Modes —
const short MODE_NORMAL     = 0;
const short MODE_SETTINGS   = 1;
const short MODE_WIFI_SETUP = 2;

// — Indexs of EEPROM Data —
const short EEPROM_Mode = 0;

void setup() {
  Serial.begin(115200); //Opens serial connection for debugging
  delay(1000);
  Serial.println();

  // — Set SSID —
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  esp_ssid = ("ESP Clock - " + mac.substring(6, 12));

  ESPWifiSSID = new char[esp_ssid.length() + 1];
  strcpy(ESPWifiSSID, esp_ssid.c_str());

  // — Setup EEPROM —
  EEPROM.begin(512);
  currentMode = EEPROM.read(EEPROM_Mode);

  // — Setup Pins —
  pinMode(MODE_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  wifiManager.setHostname(ESPWifiSSID); //TODO: may not be needed

  /*Serial.println("WL_CONNECTED" + String(WL_CONNECTED));
    Serial.println("WL_NO_SHIELD" + String(WL_NO_SHIELD));
    Serial.println("WL_IDLE_STATUS" + String(WL_IDLE_STATUS));
    Serial.println("WL_NO_SSID_AVAIL" + String(WL_NO_SSID_AVAIL));
    Serial.println("WL_SCAN_COMPLETED" + String(WL_SCAN_COMPLETED));
    Serial.println("WL_CONNECT_FAILED" + String(WL_CONNECT_FAILED));
    Serial.println("WL_CONNECTION_LOST" + String(WL_CONNECTION_LOST));
    Serial.println("WL_DISCONNECTED" + String(WL_DISCONNECTED));
  */

  //wifiManager.autoConnect(ESPWifiSSID);

  /*WiFi.begin(wifiManager.getWiFiSSID(), wifiManager.getWiFiPass());
    Serial.println("Connecting to WIFI: " + wifiManager.getWiFiSSID() + "\nWith password: " + wifiManager.getWiFiPass());
    while (true){
    Serial.println(WiFi.status());
    delay(100);
    }*/
}

void loop() {
  if (blinkInterval != 0 && (millis() - lastBlink >= blinkInterval)) {
    LEDToggle();
    lastBlink = millis();
  }

  // — Delta Time — Time since the last loop
  oldTime = currentTime;
  currentTime = millis();
  const unsigned long deltaTime = currentTime - oldTime;

  if (turnOffLightTime != 0 && millis() > turnOffLightTime) {
    LEDOff();
  }

  if (digitalRead(MODE_PIN) == LOW) {
    modeChangePinHoldTime += deltaTime;
    if (modeChangePinHoldTime > 5000) {
      blinkInterval = 250;
    }
  }
  else {
    if (modeChangePinHoldTime > 5000) {
      modeChangePinHoldTime = 0;
      blinkInterval = 0;
      wifiManager.resetSettings();
      Serial.println("WiFi Settings have been reset!");

      if (currentMode != MODE_WIFI_SETUP) {
        EEPROM.write(EEPROM_Mode, MODE_WIFI_SETUP);
        EEPROM.commit();
      }

      Serial.println("Restarting!");
      Serial.println("Restarting!");
      Serial.println("Restarting!");
      ESP.restart();
    }
    else if (modeChangePinHoldTime > 1000) {
      modeChangePinHoldTime = 0;
      Serial.println("Set new EEPROM!");
      EEPROM.write(EEPROM_Mode, 2);
      EEPROM.commit();
    }
    else if (modeChangePinHoldTime > 200) {
      modeChangePinHoldTime = 0;
      Serial.print("Current Mode in Code: "); Serial.println(currentMode);
      Serial.print("Current Mode in EEPROM: "); Serial.println(EEPROM.read(EEPROM_Mode));
    }
  }

  if (!isReady) {
    if (currentMode == MODE_NORMAL) {
      //Serial.println(">>> Starting in mode: NORMAL");

      if (wifiManager.getWiFiSSID() == "") {
        Serial.println("Network SSID is INVALID. Switching to setup mode!");
        delay(2000); //TODO: Remove this
        EEPROM.write(EEPROM_Mode, MODE_WIFI_SETUP);
        EEPROM.commit();
        ESP.restart();
      }
      else if (WiFi.status() == WL_IDLE_STATUS) {
        if (triedConnectingToWifi) {
          Serial.println("I guess the network was not found????");
          delay(1000); //TODO: Remove this
        }
        else {
          Serial.println("Starting to connect to network: " + wifiManager.getWiFiSSID());
          WiFi.begin(wifiManager.getWiFiSSID(), wifiManager.getWiFiPass());
          triedConnectingToWifi = true;
          delay(500); //Wait a moment to allow WiFi Module to start connecting
        }
      }
      else if (WiFi.status() == WL_CONNECTED) {
        LEDOn();
        LEDOffIn(5000);
        Serial.println();
        Serial.print("Got IP: ");  Serial.println(WiFi.localIP()); //Print IP that router gave client
        timeClient.begin(); //Start keeping track of time
        Serial.println("Time Client Started!");
        isReady = true;
      }
      else if (WiFi.status() == WL_DISCONNECTED) {
        Serial.println("Failed to connect???");
      }
    }
    else if (currentMode == MODE_SETTINGS) {
      //Serial.println(">>> Starting in mode: SETTINGS");
      Serial.println("Starting HTTP Server");
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ESPWifiSSID); //Starts hosted directly on ESP
      delay(500); //A small pause to allow WiFi chip to fully start
      WiFi.softAPConfig(IPAddress(192,168,1,1), IPAddress(192,168,1,1), IPAddress(255,255,255,0)); //Not being set for some reason...

      delay(500); //A small pause to allow WiFi chip to fully start

      server.on("/", handle_OnConnect);
      server.onNotFound(handle_NotFound);

      server.begin(); //Run host and allow clients to view website
      Serial.print("HTTP Server Started on: "); Serial.println(WiFi.softAPIP());

      if (MDNS.begin("clock")) { //Start the mDNS responder for clock.local
        Serial.println("mDNS responder started");
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.print("Error setting up MDNS responder! Connect using: ");
        Serial.println(WiFi.softAPIP());
      }

      isReady = true;
    } //else WiFi.begin(ssid, password); //Shows website on connected network //MOST LIKELY NOT NEEDED!!
    else if (currentMode == MODE_WIFI_SETUP) {
      if (wifiPortalRunning) {
        MDNS.update();
        if (wifiManager.process()) { //True when saved
          Serial.println("WiFi Config Finished");

          EEPROM.write(EEPROM_Mode, MODE_SETTINGS);
          EEPROM.commit();

          Serial.println("Clock Mode Saved!");

          wifiManager.stopConfigPortal(); //TODO: Is this a crash or a shutdown???
        }
      }

      if (!wifiPortalRunning) {
        WiFi.mode(WIFI_STA); //Sets WiFi Mode to STA+AP
        Serial.println("Starting WIFI Config Portal");
        wifiManager.setConfigPortalBlocking(false);
        wifiManager.startConfigPortal(ESPWifiSSID);
        wifiPortalRunning = true;

        if (MDNS.begin("clock")) { //Start the mDNS responder for clock.local
          Serial.println("mDNS responder started");
          MDNS.addService("http", "tcp", 80);
        } else {
          Serial.println("Error setting up MDNS responder! Connect using: ");
          Serial.print(WiFi.localIP());
        }
      }
    }
  }
  else {
    if (currentMode == MODE_SETTINGS) {
      MDNS.update();
      if (WiFi.softAPgetStationNum() == 0){
        if (clientIsConnected) {
          clientIsConnected = false;
          blinkInterval = 250;
        }
      }
      else {
        if (!clientIsConnected) {
          clientIsConnected = true;
          blinkInterval = 1000;
          LEDOn();
        }
      }
    }
    //Serial.println("Everything is ready!!!");
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML());
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

String SendHTML() {
  String doc = "<!DOCTYPE html> <html>\n";

  doc += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";

  doc += "<title>ESP Clock - Settings</title>\n";
  doc += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  doc += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  doc += ".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  doc += ".button-on {background-color: #1abc9c;}\n";
  doc += ".button-on:active {background-color: #16a085;}\n";
  doc += ".button-off {background-color: #34495e;}\n";
  doc += ".button-off:active {background-color: #2c3e50;}\n";
  doc += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  doc += "</style>\n";

  doc += "</head>\n";

  doc += "<body>\n";

  doc += "<h1>ESP Clock</h1>\n";
  doc += "<h3>Settings</h3>\n";

  doc += "</body>\n";
  doc += "</html>\n";
  return doc;
}

void LEDToggle() {
  digitalWrite(LED_BUILTIN,  !digitalRead(LED_BUILTIN));
}

void LEDOffIn(unsigned long ms) { //Turn off the LED in x amount of milliseconds
  turnOffLightTime = millis() + ms;
}

void LEDOn() {
  digitalWrite(LED_BUILTIN,  HIGH);
}

void LEDOff() {
  digitalWrite(LED_BUILTIN,  LOW);
}

//TODO: wifiManager.setSaveConnect(false) to disable connect on save in config portal
