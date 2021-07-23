#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ezTime.h>

// — Settings —
const int MODE_PIN = 0; //Pin Used as trigger a mode change
const int CLOCK_PIN = 15; //Digital Pin! NOT CLK PIN!
const int LATCH_PIN = 13;
const int DATA_PIN  = 12;
const int digit1_PIN = 5;
const int digit2_PIN = 4;
const int digit3_PIN = 16;
const int digit4_PIN = 14;
const int COLON_PIN = 10;
const boolean militaryTime = false;
const int digitUpdateTime = 3200;//The higher this number is, the slower the display will refresh. If it is too low you may see a ghost of other numbers on the display. If too high, you will see flicker.

// — System Varibles — System will change the variables as it is running
byte currentMode = 0;
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
unsigned long lastTimeMessageSent = 0;
unsigned int currentDigit = 1;
unsigned long lastDigitUpdateTime = 0;

// — Network Parameters —
ESP8266WebServer server(80); //Create instance of WebServer on port 80
WiFiManager wifiManager; //Create instance of WiFiManager
char* ESPWifiSSID;
String esp_ssid;

// — Time Parameters —
const String timezones[] = {"", "AOE12", "NUT11", "HST11HDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "MART9:30,M3.2.0/2:00:00,M11.1.0/2:00:00", "ASKT9AKDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "MST7", "CST6CDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "ART3", "NDT3:30NST,M3.2.0/2:00:00,M11.1.0/2:00:00", "WGST3WGT,M3.2.0/2:00:00,M11.1.0/2:00:00", "CVT1", "GMT", "0GMT,M3.2.0/2:00:00,M11.1.0/2:00:00", "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00", "MSK-3", "GST-4", "RDT-3:30IRST,M3.2.0/2:00:00,M11.1.0/2:00:00", "UZT-5", "IST-5:30", "NPT-5:45", "BST-6", "MMT-6:30", "WIB-7", "CST-8", "ACWST-8:45", "JST-9", "ACST-8:30ACDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "AEST-9AEDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "LHST-9:30LHDT,M3.2.0/2:00:00,M11.1.0/2:00:00", "SBT-11", "ANAT-12", "CHAST-11:45CHADT,M3.2.0/2:00:00,M11.1.0/2:00:00", "TOT-12TOST,M3.2.0/2:00:00,M11.1.0/2:00:00", "LINT-14"};
Timezone time_timezone;
const byte num[] = {
  B11111100, // Zero
  B01100000, // One
  B11011010, // Two
  B11110010, // Three
  B01100110, // Four
  B10110110, // Five
  B10111110, // Six
  B11100000, // Seven
  B11111110, // Eight
  B11100110, // Nine
};

// — System Modes —
const short totalModes = 3; //How many modes are there?
const short MODE_NORMAL     = 0;
const short MODE_SETTINGS   = 1;
const short MODE_WIFI_SETUP = 2;

// — Indexs of EEPROM Data —
const short EEPROM_Mode = 0;
const short EEPROM_TimeZone = 1;

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

  if (currentMode >= totalModes) {
    EEPROM.write(EEPROM_Mode, MODE_NORMAL);
    EEPROM.commit();
    currentMode = MODE_NORMAL;
  }

  // — Setup Pins —
  pinMode(MODE_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);

  pinMode(digit1_PIN, OUTPUT);
  pinMode(digit2_PIN, OUTPUT);
  pinMode(digit3_PIN, OUTPUT);
  pinMode(digit4_PIN, OUTPUT);
  pinMode(COLON_PIN, OUTPUT);

  if (currentMode == MODE_NORMAL) {
    digitalWrite(COLON_PIN, HIGH);
  }
  else {
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, 0);
    digitalWrite(LATCH_PIN, HIGH);
    digitalWrite(COLON_PIN, LOW);
  }
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
    if (modeChangePinHoldTime > 10000) {
      blinkInterval = 100;
    }
    else if (modeChangePinHoldTime > 3000) {
      blinkInterval = 250;
    }
  }
  else {
    if (modeChangePinHoldTime > 10000) {
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
    else if (modeChangePinHoldTime > 3000) {
      modeChangePinHoldTime = 0;
      blinkInterval = 0;

      if (currentMode != MODE_SETTINGS) {
        EEPROM.write(EEPROM_Mode, MODE_SETTINGS);
        EEPROM.commit();

        Serial.println("Restarting!");
        Serial.println("Restarting!");
        Serial.println("Restarting!");
        ESP.restart();
      }
      else {
        Serial.println("Already in settings.");
      }
    }
    /*else if (modeChangePinHoldTime > 200) {
      modeChangePinHoldTime = 0;
      Serial.print("Current Mode in Code: "); Serial.println(currentMode);
      Serial.print("Current Mode in EEPROM: "); Serial.println(EEPROM.read(EEPROM_Mode));
      }*/
  }

  if (!isReady) {
    if (currentMode == MODE_NORMAL) {
      //Serial.println(">>> Starting in mode: NORMAL");
      if (wifiManager.getWiFiSSID() == "") {
        Serial.println("Network SSID is INVALID. Switching to setup mode!");
        EEPROM.write(EEPROM_Mode, MODE_WIFI_SETUP);
        EEPROM.commit();
        ESP.restart();
      }
      else if (WiFi.status() == WL_IDLE_STATUS) {
        Serial.println("I guess the network was not found????");
        delay(1000); //TODO: Remove this
      }
      else if (WiFi.status() == WL_CONNECTED) {
        LEDOn();
        LEDOffIn(5000);
        Serial.println();
        Serial.print("Got IP: ");  Serial.println(WiFi.localIP()); //Print IP that router gave client

        const String timezone = timezones[String(EEPROM.read(EEPROM_TimeZone)).toInt()]; //Reads the timezone ID from the EEPROM and then get the Posix timezone code
        Serial.println("Normal mode!!! TimeZone (" + String(EEPROM.read(EEPROM_TimeZone)) + "): " + timezone);

        time_timezone.setPosix(timezone); //Sets the timezone
        isReady = true;
      }
      else if (WiFi.status() == WL_DISCONNECTED) {
        if (!triedConnectingToWifi) {
          Serial.println("Starting to connect to network: " + wifiManager.getWiFiSSID());
          WiFi.begin(wifiManager.getWiFiSSID(), wifiManager.getWiFiPass());
          triedConnectingToWifi = true;
          delay(250); //Wait a moment to allow WiFi Module to start connecting
        }
        /*else {
          Serial.println("Failed to connect???");
          delay(1000);
          }*/
      }
    }
    else if (currentMode == MODE_SETTINGS) {
      //Serial.println(">>> Starting in mode: SETTINGS");
      Serial.println("Starting HTTP Server");
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0)); //Not being set for some reason...
      Serial.print("Setting soft-AP ... ");
      Serial.println(WiFi.softAP(ESPWifiSSID) ? "Ready" : "Failed!"); //Starts hosted directly on ESP

      delay(500); //A small pause to allow WiFi chip to fully start

      server.on("/", handle_OnConnect);
      server.on("/submit", handle_Submit);
      server.on("/restart", handle_SaveAndRestart);
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
    }
    else if (currentMode == MODE_WIFI_SETUP) {
      if (wifiPortalRunning) {
        MDNS.update();

        if (WiFi.softAPgetStationNum() == 0) {
          if (clientIsConnected) {
            clientIsConnected = false;
            blinkInterval = 500;
          }
        }
        else {
          if (!clientIsConnected) {
            clientIsConnected = true;
            blinkInterval = 1500;
            LEDOn();
          }
        }

        if (wifiManager.process()) { //True when saved
          Serial.println("WiFi Config Finished");

          EEPROM.write(EEPROM_Mode, MODE_SETTINGS);
          EEPROM.commit();

          Serial.println("Clock Mode Saved!");

          wifiManager.stopConfigPortal(); //Restarts the ESP
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
      server.handleClient();
      if (WiFi.softAPgetStationNum() == 0) {
        if (clientIsConnected) {
          clientIsConnected = false;
          blinkInterval = 500;
        }
      }
      else {
        if (!clientIsConnected) {
          clientIsConnected = true;
          blinkInterval = 1500;
          LEDOn();
        }
      }
    }
    else if (currentMode == MODE_NORMAL) {
      events(); //Updates time
      if (timeStatus() != 0 && (millis() - lastTimeMessageSent > 1000)) {
        lastTimeMessageSent = millis();

        //Serial.println("Epoch Time: " + String(now()));
        /*
          Serial.print(timeClient.getHours());
          Serial.print(":");
          Serial.print(timeClient.getMinutes());
          Serial.print(":");
          Serial.println(timeClient.getSeconds());
        */

        Serial.println(String(time_timezone.hourFormat12()) + ":" + String(time_timezone.minute()) + ":" + String(time_timezone.second()) + " " + String((time_timezone.isAM() ? "AM" : "PM")));
      }
      const int h = militaryTime ? time_timezone.hour() : time_timezone.hourFormat12();
      if (micros() - lastDigitUpdateTime >= digitUpdateTime) {
        lastDigitUpdateTime = micros();

        if (currentDigit == 1) {
          SetDigit(1);
          digitalWrite(LATCH_PIN, LOW);
          shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, num[(h / 10U) % 10]);
          digitalWrite(LATCH_PIN, HIGH);
          currentDigit = 2;
        }
        else if (currentDigit == 2) {
          SetDigit(2);
          digitalWrite(LATCH_PIN, LOW);
          shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, num[h % 10]);
          digitalWrite(LATCH_PIN, HIGH);
          currentDigit = 3;
        }
        else if (currentDigit == 3) {
          SetDigit(3);
          digitalWrite(LATCH_PIN, LOW);
          shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, num[(time_timezone.minute() / 10U) % 10]);
          digitalWrite(LATCH_PIN, HIGH);
          currentDigit = 4;
        }
        else if (currentDigit == 4) {
          SetDigit(4);
          digitalWrite(LATCH_PIN, LOW);
          shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, num[time_timezone.minute() % 10]);
          digitalWrite(LATCH_PIN, HIGH);
          currentDigit = 1;
        }
      }
    }
    //Serial.println("Everything is ready!!!");
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", HTML_Main());
}

void handle_SaveAndRestart() {
  EEPROM.write(EEPROM_Mode, MODE_NORMAL);
  EEPROM.commit();

  server.send(200, "text/html", "Restarting...");
  delay(1000);

  ESP.restart();
}

void handle_Submit() {
  String timezone = server.arg("timezone");

  Serial.print("Timezone Selected: ");
  Serial.println(timezone);

  EEPROM.write(EEPROM_TimeZone, timezone.toInt());
  EEPROM.commit();

  server.send(200, "text/html", HTML_Submit());
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

String HTML_Main() {
  return (
           "<!doctype html>\n"
           "<html>\n"
           "<head>\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
           "<title>ESP Clock - Settings</title>\n"
           "<style>\n"
           "html {\n"
           " font-family: Helvetica;\n"
           " display: inline-block;\n"
           " margin: 0px auto;\n"
           " text-align: center;\n"
           "}\n"
           "h1 {\n"
           " color: #444444;\n"
           " margin: 50px auto 30px;\n"
           "}\n"
           "h3 {\n"
           " color: #444444;\n"
           " margin-bottom: 50px;\n"
           "}\n"
           "p {\n"
           " font-size: 14px;\n"
           " color: #888;\n"
           " margin-bottom: 10px;\n"
           "}\n"
           "input[type=text] {\n"
           " padding: 5px;\n"
           " border: 2px solid #ccc;\n"
           " -webkit-border-radius: 5px;\n"
           " border-radius: 5px;\n"
           "}\n"
           "input[type=text]:focus {\n"
           " border-color: #333;\n"
           "}\n"
           "input[type=submit] {\n"
           " padding: 5px 15px;\n"
           " background: #ccc;\n"
           " border: 0 none;\n"
           " cursor: pointer;\n"
           " -webkit-border-radius: 5px;\n"
           " border-radius: 5px;\n"
           "}\n"
           "</style>\n"
           "</head>\n"
           "<body>\n"
           "<h1>ESP Clock</h1>\n"
           "<h3>Settings</h3>\n"
           "<form action=\"/submit\">\n"
           "  <select name=\"timezone\" id=\"tzone\">\n"
           "<option value=\"none\" selected disabled hidden>Select a Timezone</option>\n"
           "<option value=\"1\">(UTC-12:00) US Baker Island</option>\n"
           "<option value=\"2\">(UTC-11:00) America Samoa</option>\n"
           "<option value=\"3\">(UTC-10:00) Hawaii</option>\n"
           "<option value=\"4\">(UTC-9:30) French Polynesia</option>\n"
           "<option value=\"5\">(UTC-9:00) Alaska</option>\n"
           "<option value=\"6\">(UTC-8:00) Los Angeles</option>\n"
           "<option value=\"7\">(UTC-7:00) Denver, Colorado</option>\n"
           "<option value=\"8\">(UTC-7:00) US, Phoenix, Arizona</option>\n"
           "<option value=\"9\">(UTC-6:00) US, Chicago, Illinois</option>\n"
           "<option value=\"10\">(UTC-5:00) US, New York</option>\n"
           "<option value=\"11\">(UTC-3:00) Argentina</option>\n"
           "<option value=\"12\">(UTC-2:30) Newfoundland</option>\n"
           "<option value=\"13\">(UTC-2:00) Greenland</option>\n"
           "<option value=\"14\">(UTC-1:00) Cabo Verde</option>\n"
           "<option value=\"15\">(UTC+0:00) Iceland</option>\n"
           "<option value=\"16\">(UTC+1:00) UK</option>\n"
           "<option value=\"17\">(UTC+2:00) Germany</option>\n"
           "<option value=\"18\">(UTC+3:00) Greece</option>\n"
           "<option value=\"19\">(UTC+4:00) Azerbaijan</option>\n"
           "<option value=\"20\">(UTC+4:30) Iran</option>\n"
           "<option value=\"21\">(UTC+5:00) Pakistan</option>\n"
           "<option value=\"22\">(UTC+5:30) India</option>\n"
           "<option value=\"23\">(UTC+5:45) Nepal</option>\n"
           "<option value=\"24\">(UTC+6:00) Bangladesh</option>\n"
           "<option value=\"25\">(UTC+6:30) Myanmar</option>\n"
           "<option value=\"26\">(UTC+7:00) Indonesia</option>\n"
           "<option value=\"27\">(UTC+8:00) China</option>\n"
           "<option value=\"28\">(UTC+8:45) Western Australia</option>\n"
           "<option value=\"29\">(UTC+9:00) Japan</option>\n"
           "<option value=\"30\">(UTC+9:30) Central Australia</option>\n"
           "<option value=\"31\">(UTC+10:00) Eastern Australia</option>\n"
           "<option value=\"32\">(UTC+10:30) Lord Howe Island</option>\n"
           "<option value=\"33\">(UTC+11:00) Solomon Islands</option>\n"
           "<option value=\"34\">(UTC+12:00) New Zealand</option>\n"
           "<option value=\"35\">(UTC+12:45) Chatham Islands</option>\n"
           "<option value=\"36\">(UTC+13:00) Tonga</option>\n"
           "<option value=\"37\">(UTC+14:00) Christmas Island</option>"
           "  </select>\n"
           "  <p></p>\n"
           "  <input class=\"\" type=\"submit\" value=\"Save\"></input>\n"
           "</form>\n"
           "</body>\n"
           "</html>"
         );
}

String HTML_Submit() {
  return (
           "<!doctype html>\n"
           "<html>\n"
           "<head>\n"
           "<style>\n"
           "body {\n"
           "  font-family: Helvetica;\n"
           " background-color: #81d4fa;\n"
           " font-weight: bold;\n"
           "}\n"
           ".text-box {\n"
           " margin-left: 44vw;\n"
           " margin-top: 42vh;\n"
           "}\n"
           ".btn:link, .btn:visited {\n"
           " text-transform: uppercase;\n"
           " text-decoration: none;\n"
           " padding: 15px 40px;\n"
           " display: inline-block;\n"
           " border-radius: 100px;\n"
           " transition: all .2s;\n"
           " position: absolute;\n"
           "}\n"
           ".btn:hover {\n"
           " transform: translateY(-3px);\n"
           " box-shadow: 0 10px 20px rgba(0, 0, 0, 0.2);\n"
           "}\n"
           ".btn:active {\n"
           " transform: translateY(-1px);\n"
           " box-shadow: 0 5px 10px rgba(0, 0, 0, 0.2);\n"
           "}\n"
           ".btn-white {\n"
           " background-color: #fff;\n"
           " color: #000;\n"
           "}\n"
           ".btn-red {\n"
           " background-color: #ff6f6b;\n"
           " color: #000;\n"
           "}\n"
           ".btn::after {\n"
           " content: \"\";\n"
           " display: inline-block;\n"
           " height: 100%;\n"
           " width: 100%;\n"
           " border-radius: 100px;\n"
           " position: absolute;\n"
           " top: 0;\n"
           " left: 0;\n"
           " z-index: -1;\n"
           " transition: all .4s;\n"
           "}\n"
           ".btn-white::after {\n"
           " background-color: #fff;\n"
           "}\n"
           ".btn-red::after {\n"
           " background-color: #ff6f6b;\n"
           "}\n"
           ".btn:hover::after {\n"
           " transform: scaleX(1.4) scaleY(1.6);\n"
           " opacity: 0;\n"
           "}\n"
           ".btn-animated {\n"
           " animation: moveInBottom 5s ease-out;\n"
           " animation-fill-mode: backwards;\n"
           "}\n"
           "@keyframes moveInBottom {\n"
           "0% {\n"
           "opacity: 0;\n"
           "transform: translateY(30px);\n"
           "}\n"
           "100% {\n"
           "opacity: 1;\n"
           "transform: translateY(0px);\n"
           "}\n"
           "}\n"
           "</style>\n"
           "</head>\n"
           "<div class=\"text-box\">\n"
           "    <a href=\"/\" class=\"btn btn-white btn-animate\">Go Back</a>\n"
           " <br><br><br><br>\n"
           "    <a href=\"/restart\" class=\"btn btn-red btn-animate\">Restart</a>\n"
           "</div>\n"
           "<body>\n"
           "</body>\n"
           "</html>"
         );
}

void LEDToggle() {
  const int set = !digitalRead(LED_BUILTIN);
  digitalWrite(LED_BUILTIN, set);
  digitalWrite(COLON_PIN, set);
}

void LEDOffIn(unsigned long ms) { //Turn off the LED in x amount of milliseconds
  turnOffLightTime = millis() + ms;
}

void LEDOn() {
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(COLON_PIN, HIGH);
}

void LEDOff() {
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(COLON_PIN, LOW);
}

void SetDigit(int d) {
  digitalWrite(digit1_PIN, d != 1);
  digitalWrite(digit2_PIN, d != 2);
  digitalWrite(digit3_PIN, d != 3);
  digitalWrite(digit4_PIN, d != 4);
}
