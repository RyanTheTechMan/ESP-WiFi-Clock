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

// — Global Parameters —

// — Network Parameters —
ESP8266WebServer server(80); //Create instance of WebServer on port 80
WiFiManager wifiManager; //Create isntance of WiFiManager
WiFiUDP ntpUDP; //Opens a UDP Port
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000); //Starts a NTPClient to get the time. //TODO: replace number and add/subtract based on timezone & DST

// — System Modes —
const short MODE_NORMAL     = 0;
const short MODE_SETTINGS   = 1;
const short MODE_WIFI_SETUP = 2;

// — Indexs of EEPROM Data —
const short EEPROM_Mode = 0;

void setup() {
  Serial.begin(115200); //Opens serial connection for debugging
  Serial.println();
  // — Setup EEPROM —
  //EEPROM.begin(2);
  //currentMode = EEPROM.read(EEPROM_Mode);

  // — Setup Pins —
  pinMode(MODE_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);


  WiFi.mode(WIFI_STA); //Sets WiFi Mode to STA+AP

  if (currentMode == MODE_NORMAL) {
    Serial.println(">>> Starting in mode: NORMAL");
    while (!ConnectToWiFi(wifiManager.getWiFiSSID(), wifiManager.getWiFiPass())){} //Keep trying to connect to network. MOVE TO LOOP TO STOP LAG
    
    LEDOn();
    LEDOffIn(2000);
    Serial.println();
    Serial.print("Got IP: ");  Serial.println(WiFi.localIP()); //Print IP that router gave client
    timeClient.begin(); //Start keeping track of time
    Serial.println("Time Client Started!");
  }

  if (currentMode == MODE_SETTINGS) {
    Serial.println(">>> Starting in mode: SETTINGS");
    Serial.print("Starting HTTP Server");
    WiFi.softAP("ESP Clock - Settings"); //Starts hosted directly on ESP

    delay(100); //A small pause to allow WiFi chip to fully start

    server.on("/", handle_OnConnect);
    server.onNotFound(handle_NotFound);

    server.begin(); //Run host and allow clients to view website
    Serial.println("HTTP Server Started!");

    if (MDNS.begin("clock")) { //Start the mDNS responder for clock.local
      Serial.println("mDNS responder started");
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("Error setting up MDNS responder! Connect using: ");
      Serial.print(WiFi.localIP());
    }
  } //else WiFi.begin(ssid, password); //Shows website on connected network //MOST LIKELY NOT NEEDED!!

}

void loop() {
  if(blinkInterval != 0 && (millis() - oldTime >= blinkInterval)) {
    LEDToggle();
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
      
      Serial.println("Restarting!");
      Serial.println("Restarting!");
      Serial.println("Restarting!");
      ESP.restart();
    }
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

//run EEPROM.commit() to save changes

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

boolean ConnectToWiFi(String[] ssid, String[] pass) {
  Serial.println("Connecting to WIFI: " + ssid + "\nWith password: " + pass);
  WiFi.begin(ssid, pass);

  int wifiTimeout = 15000;
  boolean cnt = true; //Connected to WiFi Network
  while (WiFi.status() != WL_CONNECTED) { //Wait until connected to network
    if (wifiTimeout <= 0) {
      cnt = false;
      break;
    }
    wifiTimeout -= 500;
    Serial.print(".");
    LEDToggle();
    delay(500);
  }

  if (!cnt) { //Go into a loop and try connecting again
    WiFi.disconnect()
    return false;
  }
  return true;
}
