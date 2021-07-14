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
//ID : GMT Offset, Has Daylight Savings //Increased by 1 so it lines up with website. 0,0 is added to lign up with website.
const float timezones[83][2] = {{0,0},{-12,0},{-11,0},{-10,0},{-9,1},{-8,1},{-8,1},{-7,0},{-7,1},{-7,1},{-6,0},{-6,1},{-6,1},{-6,0},{-5,0},{-5,1},{-5,1},{-4,1},{-4,0},{-4,0},{-4,1},{-3.5,1},{-3,1},{-3,0},{-3,1},{-3,1},{-2,1},{-1,0},{-1,1},{0,0},{0,1},{1,1},{1,1},{1,1},{1,1},{1,1},{2,1},{2,1},{2,1},{2,1},{2,0},{2,1},{2,1},{2,1},{2,1},{3,0},{3,1},{3,0},{3,0},{3.5,1},{4,0},{4,1},{4,1},{4.5,0},{5,1},{5,0},{5.5,0},{5.5,0},{5.7,0},{6,1},{6,0},{6.5,0},{7,0},{7,1},{8,0},{8,0},{8,0},{8,0},{8,0},{9,0},{9,0},{9,1},{9.5,0},{9.5,0},{10,0},{10,1},{10,1},{10,0},{10,1},{11,1},{12,1},{12,0},{13,0}};
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

  //wifiManager.setHostname(ESPWifiSSID); //TODO: may not be needed

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
      WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0)); //Not being set for some reason...
      Serial.print("Setting soft-AP ... ");
      Serial.println(WiFi.softAP(ESPWifiSSID) ? "Ready" : "Failed!"); //Starts hosted directly on ESP

      delay(500); //A small pause to allow WiFi chip to fully start

      server.on("/", handle_OnConnect);
      server.on("/submit", handle_Submit);
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
      server.handleClient();
      if (WiFi.softAPgetStationNum() == 0) {
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
  server.send(200, "text/html", HTML_Main());
}

void handle_Submit() {
  String timezone = server.arg("timezone");

  Serial.print("Timezone Selected: ");
  Serial.println(timezone);

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
"    <option value=\"1\" >(GMT-12:00) International Date Line West</option>\n"
"    <option value=\"2\" >(GMT-11:00) Midway Island, Samoa</option>\n"
"    <option value=\"3\" >(GMT-10:00) Hawaii</option>\n"
"    <option value=\"4\" >(GMT-09:00) Alaska</option>\n"
"    <option value=\"5\" >(GMT-08:00) Pacific Time (US & Canada)</option>\n"
"    <option value=\"6\" >(GMT-08:00) Tijuana, Baja California</option>\n"
"    <option value=\"7\" >(GMT-07:00) Arizona</option>\n"
"    <option value=\"8\" >(GMT-07:00) Chihuahua, La Paz, Mazatlan</option>\n"
"    <option value=\"9\" >(GMT-07:00) Mountain Time (US & Canada)</option>\n"
"    <option value=\"10\">(GMT-06:00) Central America</option>\n"
"    <option value=\"11\">(GMT-06:00) Central Time (US & Canada)</option>\n"
"    <option value=\"12\">(GMT-06:00) Guadalajara, Mexico City, Monterrey</option>\n"
"    <option value=\"13\">(GMT-06:00) Saskatchewan</option>\n"
"    <option value=\"14\">(GMT-05:00) Bogota, Lima, Quito, Rio Branco</option>\n"
"    <option value=\"15\">(GMT-05:00) Eastern Time (US & Canada)</option>\n"
"    <option value=\"16\">(GMT-05:00) Indiana (East)</option>\n"
"    <option value=\"17\">(GMT-04:00) Atlantic Time (Canada)</option>\n"
"    <option value=\"18\">(GMT-04:00) Caracas, La Paz</option>\n"
"    <option value=\"19\">(GMT-04:00) Manaus</option>\n"
"    <option value=\"20\">(GMT-04:00) Santiago</option>\n"
"    <option value=\"21\">(GMT-03:30) Newfoundland</option>\n"
"    <option value=\"22\">(GMT-03:00) Brasilia</option>\n"
"    <option value=\"23\">(GMT-03:00) Buenos Aires, Georgetown</option>\n"
"    <option value=\"24\">(GMT-03:00) Greenland</option>\n"
"    <option value=\"25\">(GMT-03:00) Montevideo</option>\n"
"    <option value=\"26\">(GMT-02:00) Mid-Atlantic</option>\n"
"    <option value=\"27\">(GMT-01:00) Cape Verde Is.</option>\n"
"    <option value=\"28\">(GMT-01:00) Azores</option>\n"
"    <option value=\"29\">(GMT+00:00) Casablanca, Monrovia, Reykjavik</option>\n"
"    <option value=\"30\">(GMT+00:00) Greenwich Mean Time : Dublin, Edinburgh, Lisbon, London</option>\n"
"    <option value=\"31\">(GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna</option>\n"
"    <option value=\"32\">(GMT+01:00) Belgrade, Bratislava, Budapest, Ljubljana, Prague</option>\n"
"    <option value=\"33\">(GMT+01:00) Brussels, Copenhagen, Madrid, Paris</option>\n"
"    <option value=\"34\">(GMT+01:00) Sarajevo, Skopje, Warsaw, Zagreb</option>\n"
"    <option value=\"35\">(GMT+01:00) West Central Africa</option>\n"
"    <option value=\"36\">(GMT+02:00) Amman</option>\n"
"    <option value=\"37\">(GMT+02:00) Athens, Bucharest, Istanbul</option>\n"
"    <option value=\"38\">(GMT+02:00) Beirut</option>\n"
"    <option value=\"39\">(GMT+02:00) Cairo</option>\n"
"    <option value=\"40\">(GMT+02:00) Harare, Pretoria</option>\n"
"    <option value=\"41\">(GMT+02:00) Helsinki, Kyiv, Riga, Sofia, Tallinn, Vilnius</option>\n"
"    <option value=\"42\">(GMT+02:00) Jerusalem</option>\n"
"    <option value=\"43\">(GMT+02:00) Minsk</option>\n"
"    <option value=\"44\">(GMT+02:00) Windhoek</option>\n"
"    <option value=\"45\">(GMT+03:00) Kuwait, Riyadh, Baghdad</option>\n"
"    <option value=\"46\">(GMT+03:00) Moscow, St. Petersburg, Volgograd</option>\n"
"    <option value=\"47\">(GMT+03:00) Nairobi</option>\n"
"    <option value=\"48\">(GMT+03:00) Tbilisi</option>\n"
"    <option value=\"49\">(GMT+03:30) Tehran</option>\n"
"    <option value=\"50\">(GMT+04:00) Abu Dhabi, Muscat</option>\n"
"    <option value=\"51\">(GMT+04:00) Baku</option>\n"
"    <option value=\"52\">(GMT+04:00) Yerevan</option>\n"
"    <option value=\"53\">(GMT+04:30) Kabul</option>\n"
"    <option value=\"54\">(GMT+05:00) Yekaterinburg</option>\n"
"    <option value=\"55\">(GMT+05:00) Islamabad, Karachi, Tashkent</option>\n"
"    <option value=\"56\">(GMT+05:30) Sri Jayawardenapura</option>\n"
"    <option value=\"57\">(GMT+05:30) Chennai, Kolkata, Mumbai, New Delhi</option>\n"
"    <option value=\"58\">(GMT+05:45) Kathmandu</option>\n"
"    <option value=\"59\">(GMT+06:00) Almaty, Novosibirsk</option>\n"
"    <option value=\"60\">(GMT+06:00) Astana, Dhaka</option>\n"
"    <option value=\"61\">(GMT+06:30) Yangon (Rangoon)</option>\n"
"    <option value=\"62\">(GMT+07:00) Bangkok, Hanoi, Jakarta</option>\n"
"    <option value=\"63\">(GMT+07:00) Krasnoyarsk</option>\n"
"    <option value=\"64\">(GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi</option>\n"
"    <option value=\"65\">(GMT+08:00) Kuala Lumpur, Singapore</option>\n"
"    <option value=\"66\">(GMT+08:00) Irkutsk, Ulaan Bataar</option>\n"
"    <option value=\"67\">(GMT+08:00) Perth</option>\n"
"    <option value=\"68\">(GMT+08:00) Taipei</option>\n"
"    <option value=\"69\">(GMT+09:00) Osaka, Sapporo, Tokyo</option>\n"
"    <option value=\"70\">(GMT+09:00) Seoul</option>\n"
"    <option value=\"71\">(GMT+09:00) Yakutsk</option>\n"
"    <option value=\"72\">(GMT+09:30) Adelaide</option>\n"
"    <option value=\"73\">(GMT+09:30) Darwin</option>\n"
"    <option value=\"74\">(GMT+10:00) Brisbane</option>\n"
"    <option value=\"75\">(GMT+10:00) Canberra, Melbourne, Sydney</option>\n"
"    <option value=\"76\">(GMT+10:00) Hobart</option>\n"
"    <option value=\"77\">(GMT+10:00) Guam, Port Moresby</option>\n"
"    <option value=\"78\">(GMT+10:00) Vladivostok</option>\n"
"    <option value=\"79\">(GMT+11:00) Magadan, Solomon Is., New Caledonia</option>\n"
"    <option value=\"80\">(GMT+12:00) Auckland, Wellington</option>\n"
"    <option value=\"81\">(GMT+12:00) Fiji, Kamchatka, Marshall Is.</option>\n"
"    <option value=\"82\">(GMT+13:00) Nuku'alofa</option>\n"
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
