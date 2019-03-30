/*
GarageController.ino for esp8266

Copyright (c) 2019 Rodney Adams (rodney.adams.69@gmail.com). All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Required libraries:  

// TimeLib for ntp here:  https://github.com/PaulStoffregen/Time
// Arduino Json here:  https://github.com/bblanchon/ArduinoJson --- NOTE: Version 5.13.5 required, version 6+ will not work.
// Async Web Server https://github.com/me-no-dev/ESPAsyncWebServer
// Async TCP Library https://github.com/me-no-dev/ESPAsyncTCP
// latest arduino/esp8266 core:  https://github.com/esp8266/Arduino

#include <htmlEmbedBig.h>
#include <myWebServerAsync.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <FS.h>
#include <ArduinoJson.h> 
#include <TimeLib.h>
#include <String.h>

String Version = "Garage Opener 1.0";
String SafeIP = "";   String SafePort = ""; //IP and port that is able to access system unauthenticated
String Door1Url = ""; String Door2Url = ""; String Door3Url = "";  //User settings for URL's on home automation system
String Method = "";   String OpenBody = ""; String CloseBody = ""; //Additional home automation values

int StatusLed = 2; //GPIO for onboard LED
int Door1RelayGPIO = 14; int Door2RelayGPIO = 12; int Door3RelayGPIO = 13; //GPIO pins for door relays
int Door1SensrGPIO = 16; int Door2SensrGPIO = 5;  int Door3SensrGPIO = 4;  //GPIO pins for door sensors
  
bool StatusLedIsOn = false; bool GlobalPause = false; bool InAPMode = false;
  
int Door1Enable = 0; String Door1Name = ""; int Door1State = 0; int Door1Relay = 0; //Stored JSON values stored in SPIFFS
int Door2Enable = 0; String Door2Name = ""; int Door2State = 0; int Door2Relay = 0;
int Door3Enable = 0; String Door3Name = ""; int Door3State = 0; int Door3Relay = 0;
 
int CurDoor1Enable = 0; String CurDoor1Name = ""; int CurDoor1State = 0; int CurDoor1Relay = 0; //Current values to compare with last written
int CurDoor2Enable = 0; String CurDoor2Name = ""; int CurDoor2State = 0; int CurDoor2Relay = 0; //stored in SPIFFS. Any changes when compaired
int CurDoor3Enable = 0; String CurDoor3Name = ""; int CurDoor3State = 0; int CurDoor3Relay = 0; //will be written to stored JSON values

bool ActuateCalled = false; int ActuatedPin = 0; //HandleActuateDoor() global vars needed due to Async inability to use delay()
bool NotifyHA = false; //HandleGetState() global var to initiate door states be sent to Home Automation Server
  
unsigned long lastCheck = millis(); unsigned long APModeTimer = millis(); //Timers to reboot ESP if no client connect in 5 mins

void HandleStatusOn(AsyncWebServerRequest *request)  //Handle request to turn status LED ON
{
  digitalWrite (StatusLed, HIGH);
  StatusLedIsOn = true;
  DebugPrintln("Turn on LED");
  String message = "<meta http-equiv=\"refresh\" content=\"0; URL='/'\" />";
  request->send(200, "text/html", message);
}

void HandleStatusOff(AsyncWebServerRequest *request)  //Handle request to turn status LED OFF
{
  digitalWrite (StatusLed, LOW);
  StatusLedIsOn = false;
  DebugPrintln("Turn off LED");
  String message = "<meta http-equiv=\"refresh\" content=\"0; URL='/'\" />";
  request->send(200, "text/html", message);
}

void HandleActuateDoor(AsyncWebServerRequest *request) //Handle built-in browser requests to actuate doors
{
  if (!WebServer.isAuthorized(request)) return;
  int GPIOPin = 0;
  int paramsNr = request->params();
  String RequestedDoor = ""; String Message = "";
  
  for(int i=0;i<paramsNr;i++){
    AsyncWebParameter* p = request->getParam(i);
    RequestedDoor = p->name();
    RequestedDoor.toLowerCase();
    if (RequestedDoor == "door1") { GPIOPin = Door1RelayGPIO; }
    if (RequestedDoor == "door2") { GPIOPin = Door2RelayGPIO; }
    if (RequestedDoor == "door3") { GPIOPin = Door3RelayGPIO; }
  }
  request->redirect("/index.html");
  request->send(200);
  Message = "Actuating " + RequestedDoor + " relay on PIN " + String(GPIOPin);
  DebugPrintln(Message);
  ActuateCalled = true;
  ActuatedPin = GPIOPin;
}

void HandleRestGet(AsyncWebServerRequest *request)  //Handle REST API requests to GET current door state (Open/Closed)
{
  if (request->client()->remoteIP().toString() != SafeIP) {
    DebugPrintln("Request to HandleRestGet from non safe IP address");
    request->send (404, "text/html", "Error 404 not found");
    return;
  }
  
  String CurrentState = "";

  DebugPrint("REST Client "); DebugPrint(request->client()->remoteIP().toString()); DebugPrint(" requested state of "); DebugPrintln(request->url().substring(5,10));
  
  if (request->url().substring(5,10) == "door1") {CurrentState = CurDoor1State;}
  if (request->url().substring(5,10) == "door2") {CurrentState = CurDoor2State;}
  if (request->url().substring(5,10) == "door3") {CurrentState = CurDoor3State;}
  if (CurrentState == "0") {
    request->send (200, "text/html", "open");
  }
  else {
    request->send (200, "text/html", "closed");
  }
  return;
}

void HandleGetBulkState(AsyncWebServerRequest *request) //Handle request from HA Server for current door states
{
  if (request->client()->remoteIP().toString() != SafeIP) {
    DebugPrintln("Request to HandleGetState from non safe IP address");
    request->send (404, "text/html", "Error 404 not found");
    return;
  }
  request->send (200, "text/plain", "Request Received");
  DebugPrintln("HandleGetState called from HA Server");
  NotifyHA = true;
  return;
}

void HandleRestPut(AsyncWebServerRequest *request)  //Handle REST API requests to open/close doors
{
  if (request->client()->remoteIP().toString() != SafeIP) {
    DebugPrintln("Request to HandleRestPut from non safe IP address");
    request->send (404, "text/html", "Error 404 not found");
    return;
  }
  
  int GPIOPin = 0;
  bool StateChanged = false; String Message = "";
  String RequestedDoor = request->url().substring(5,10);
  String RequestedState = request->url().substring(17,request->url().length());

  DebugPrint("REST Client "); DebugPrint(request->client()->remoteIP().toString()); DebugPrintln(" requested " + RequestedDoor + " be set to " + RequestedState);
  
  if ((RequestedDoor == "door1") && (BinaryState(RequestedState) != CurDoor1State)) {
    GPIOPin = Door1RelayGPIO;
    StateChanged = true;
  }
  if ((RequestedDoor == "door2") && (BinaryState(RequestedState) != CurDoor2State)) {
    GPIOPin = Door2RelayGPIO;
    StateChanged = true;
  }
  if ((RequestedDoor == "door3") && (BinaryState(RequestedState) != CurDoor3State)) {
    GPIOPin = Door3RelayGPIO;
    StateChanged = true;
  }
  if (StateChanged) {
    if (RequestedState == "open") {
      Message = "Opening " + RequestedDoor + " relay on PIN " + String(GPIOPin);
      request->send (200, "text/html", Message);
      ActuateCalled = true;
      ActuatedPin = GPIOPin;
    }
    else if (RequestedState == "close") {
      Message = "Closing " + RequestedDoor + " relay on PIN " + String(GPIOPin);
      request->send (200, "text/html", Message);
      ActuateCalled = true;
      ActuatedPin = GPIOPin;
    }
  }
  else {
    request->send (400, "text/html", "Door already in requested state");
  }
  return;
}

void HandleGetState(AsyncWebServerRequest *request) //Handle request from HA Server for current door states
{
  if (request->client()->remoteIP().toString() != SafeIP) {
    DebugPrintln("Request to HandleGetState from non safe IP address");
    request->send (404, "text/html", "Error 404 not found");
    return;
  }
  request->send (200, "text/plain", "Request Received");
  DebugPrintln("HandleGetState called from HA Server");
  NotifyHA = true;
  return;
}

void HandleTogglePause(AsyncWebServerRequest *request) //Handle request to PAUSE main loop from reading/writing JSON files
{
  if (!WebServer.isAuthorized(request)) return;
  if (GlobalPause == true) {
    GlobalPause = false;
    GetGarageConfig("new");
    GetGarageConfig("cur");
    request->send (200, "text/html", "UNPAUSED");
  }
  else {
    GlobalPause = true;
    request->send (200, "text/html", "PAUSED");
  }
  DebugPrintln(GlobalPause);
  return;
}

void setup()
{
  pinMode (Door1RelayGPIO, OUTPUT);  // Set for HIGH level relays and set to high immediately so doors do not actuate
  digitalWrite (Door1RelayGPIO, HIGH);
  pinMode (Door2RelayGPIO, OUTPUT);
  digitalWrite (Door2RelayGPIO, HIGH);
  pinMode (Door3RelayGPIO, OUTPUT);
  digitalWrite (Door3RelayGPIO, HIGH);
  pinMode (Door1SensrGPIO, INPUT);
  pinMode (Door2SensrGPIO, INPUT);
  pinMode (Door3SensrGPIO, INPUT);
  pinMode (StatusLed, OUTPUT);
  
  Serial.begin(115200); //Open Serial for debug output
  delay(500); //Delay for Serial Output to be ready
  DebugPrintln(""); DebugPrintln("");
  DebugPrintln(Version);

  WebServer.begin();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite (StatusLed, HIGH);
    StatusLedIsOn = true;
  } 

  String CurrentAPIP = WiFi.softAPIP().toString();

  if (CurrentAPIP == "0.0.0.0") {
    DebugPrintln("WiFi Station Mode");
    GlobalPause = false;
    InAPMode = false;
  }
  else {
    DebugPrintln("WiFi AP Mode....Auto restart in 5 minutes.");
    GlobalPause = true;
    InAPMode = true;
    APModeTimer = millis();
  }

  if (!GlobalPause) {
    DebugPrintln("Getting initial configuration....");
    GetGarageConfig("stored");
    GetGarageConfig("current");
    if (SafeIP != "") {
      NotifyHomeAutomation(); //Initial notification to HA Server of door states
    }
  }

//  DebugPrint("Safe IP is currently set to "); DebugPrintln(SafeIP);

  server.on("/statuson", HTTP_GET, HandleStatusOn);
  server.on("/statusoff", HTTP_GET, HandleStatusOff);
  server.on("/actuate", HTTP_GET, HandleActuateDoor);
  server.on("/api/door1/state", HTTP_GET, HandleRestGet);       //Requires SafeIP to be set
  server.on("/api/door2/state", HTTP_GET, HandleRestGet);       //Requires SafeIP to be set
  server.on("/api/door3/state", HTTP_GET, HandleRestGet);       //Requires SafeIP to be set
  server.on("/api/getstate", HTTP_GET, HandleGetBulkState);     //Requires SafeIP to be set
  server.on("/api/door1/state/open", HTTP_PUT, HandleRestPut);  //Requires SafeIP to be set
  server.on("/api/door2/state/open", HTTP_PUT, HandleRestPut);  //Requires SafeIP to be set
  server.on("/api/door3/state/open", HTTP_PUT, HandleRestPut);  //Requires SafeIP to be set
  server.on("/api/door1/state/close", HTTP_PUT, HandleRestPut); //Requires SafeIP to be set
  server.on("/api/door2/state/close", HTTP_PUT, HandleRestPut); //Requires SafeIP to be set
  server.on("/api/door3/state/close", HTTP_PUT, HandleRestPut); //Requires SafeIP to be set
  server.on("/pause", HTTP_GET, HandleTogglePause);             //Pause reading/writing to SPIFFS
  DebugPrintln("Initializing, Please wait......");
  delay(5000);
  DebugPrintln("System Online");
}

void loop() {
  unsigned long curTime = millis();

  if (InAPMode == true) {  //Reset ESP after 5 mins to restore connectivity after possible power outage
    if (curTime - APModeTimer >= 300000) {
      DebugPrintln("Resetting ESP to restore connectivity....");
      ESP.restart();
    }
  }

  if (ActuateCalled) //Received notification to actuate door. Out of band due to ASYNC cannot have delay()
  {
    ActuateCalled = false;
    digitalWrite(ActuatedPin, LOW);
    delay(200);
    digitalWrite(ActuatedPin, HIGH);
    ActuatedPin = 0;
    DebugPrintln("Actuate routine called....");
  }

  if (NotifyHA)
  { 
    NotifyHA = false;
    NotifyHomeAutomation();
  }

  WebServer.handle();
 
  if (!GlobalPause) {
    if (curTime - lastCheck >= 5000) {
      GetGarageConfig("stored");
      if (ConfigStateChanged() || SensorsChanged()) {
        WriteCurrentConfig();
        GetGarageConfig("current");
        if (SafeIP != "") {
          NotifyHomeAutomation();
        }
      }
      lastCheck = millis();
    }
  }
}

void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) //Parse charArray to BYTES
{
    for (int i = 0; i < maxBytes; i++) {
        bytes[i] = strtoul(str, NULL, base);  // Convert byte
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
}

void GetGarageConfig(String GetState)  //Parse "setup.json" for new states and parse memory for current door states
{
  File f = SPIFFS.open("/setup.json", "r");
//  delay(500);
  if (!f) {
      DebugPrintln("Garage config not found");
  }
  else {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(f);
    f.close();
    if (!root.success())
    {
      DebugPrintln("Parse of garage config failed");
      return;
    }
    if (GetState == "stored") {
      if (root["door1enable"].asString() != "") {
        Door1Enable = String(root["door1enable"].asString()).toInt();
        Door1Name = root["door1name"].asString();
        Door1State = String(root["door1state"].asString()).toInt();
        Door1Relay = String(root["door1relay"].asString()).toInt();
        Door2Enable = String(root["door2enable"].asString()).toInt();
        Door2Name = root["door2name"].asString();
        Door2State = String(root["door2state"].asString()).toInt();
        Door2Relay = String(root["door2relay"].asString()).toInt();
        Door3Enable = String(root["door3enable"].asString()).toInt();
        Door3Name = root["door3name"].asString();
        Door3State = String(root["door3state"].asString()).toInt();
        Door3Relay = String(root["door3relay"].asString()).toInt();
        SafeIP = root["safeip"].asString();
        SafePort = root["safeport"].asString();
        Method = root["method"].asString();
        OpenBody = root["openbody"].asString();
        CloseBody = root["closebody"].asString();
        Door1Url = root["door1url"].asString();
        Door2Url = root["door2url"].asString();
        Door3Url = root["door3url"].asString();
//        DebugPrintln("Getting STORED door config");
      }
    }
    else if (GetState == "current") {
      if (root["door1enable"].asString() != "") {
        CurDoor1Enable = String(root["door1enable"].asString()).toInt();
        CurDoor1Name = root["door1name"].asString();
        CurDoor1State = String(root["door1state"].asString()).toInt();
        CurDoor1Relay = String(root["door1relay"].asString()).toInt();
        CurDoor2Enable = String(root["door2enable"].asString()).toInt();
        CurDoor2Name = root["door2name"].asString();
        CurDoor2State = String(root["door2state"].asString()).toInt();
        CurDoor2Relay = String(root["door2relay"].asString()).toInt();
        CurDoor3Enable = String(root["door3enable"].asString()).toInt();
        CurDoor3Name = root["door3name"].asString();
        CurDoor3State = String(root["door3state"].asString()).toInt();
        CurDoor3Relay = String(root["door3relay"].asString()).toInt();
//        DebugPrintln("Getting CURRENT door states");
      }
    }
  }
  return;
}

bool ConfigStateChanged()  //Check if configuration file has changed
{
  if ((Door1Enable != CurDoor1Enable) || (Door1Name != CurDoor1Name) || (Door1State != CurDoor1State) || (Door1Relay != CurDoor1Relay) ||
      (Door2Enable != CurDoor2Enable) || (Door2Name != CurDoor2Name) || (Door2State != CurDoor2State) || (Door2Relay != CurDoor2Relay) ||
      (Door3Enable != CurDoor3Enable) || (Door3Name != CurDoor3Name) || (Door3State != CurDoor3State) || (Door3Relay != CurDoor3Relay) )
  {
    DebugPrintln("ConfigStateChanged = true");
    return true;
  }
  else {
    DebugPrintln("ConfigStateChanged = false");
    return false;
  }
}

bool SensorsChanged()  //Check if any door states have changed
{
  int Door1Volt = analogRead(Door1SensrGPIO);
  int Door2Volt = analogRead(Door2SensrGPIO);
  int Door3Volt = analogRead(Door3SensrGPIO);
  float Door1Status = Door1Volt * (3.3/1023.0);
  float Door2Status = Door2Volt * (3.3/1023.0);
  float Door3Status = Door3Volt * (3.3/1023.0);
 
  // Door state is 1 if door is closed and 0 if it is open
  if (Door1Relay == 0 && Door1Status < 1) {CurDoor1State = 0;} //Normally Open & Circuit Open
  if (Door1Relay == 0 && Door1Status > 1) {CurDoor1State = 1;} //Normally Open & Circuit Closed
  if (Door1Relay == 1 && Door1Status < 1) {CurDoor1State = 1;} //Normally Closed & Circuit Open
  if (Door1Relay == 1 && Door1Status > 1) {CurDoor1State = 0;} //Normally Closed & Circuit Closed
  if (Door2Relay == 0 && Door2Status < 1) {CurDoor2State = 0;} //Normally Open & Circuit Open
  if (Door2Relay == 0 && Door2Status > 1) {CurDoor2State = 1;} //Normally Open & Circuit Closed
  if (Door2Relay == 1 && Door2Status < 1) {CurDoor2State = 1;} //Normally Closed & Circuit Open
  if (Door2Relay == 1 && Door2Status > 1) {CurDoor2State = 0;} //Normally Closed & Circuit Closed
  if (Door3Relay == 0 && Door3Status < 1) {CurDoor3State = 0;} //Normally Open & Circuit Open
  if (Door3Relay == 0 && Door3Status > 1) {CurDoor3State = 1;} //Normally Open & Circuit Closed
  if (Door3Relay == 1 && Door3Status < 1) {CurDoor3State = 1;} //Normally Closed & Circuit Open
  if (Door3Relay == 1 && Door3Status > 1) {CurDoor3State = 0;} //Normally Closed & Circuit Closed

  if ((Door1State != CurDoor1State) || (Door2State != CurDoor2State) || (Door3State != CurDoor3State)) {
    DebugPrintln("SensorsChanged = true");
    return true;
  }
  else {
    DebugPrintln("SensorsChanged = false");
    return false;
  }
}

void WriteCurrentConfig()  //Write current state information and configuration to "setup.json"
{
  String jsonString = "";
//  jsonString = "{\"door1enable\":\"" + String(CurDoor1Enable) + "\",";
//  jsonString += "\"door1name\":\"" + CurDoor1Name + "\",";
//  jsonString += "\"door1state\":\"" + String(CurDoor1State) + "\",";
//  jsonString += "\"door1relay\":\"" + String(CurDoor1Relay) + "\",";
//  jsonString += "\"door2enable\":\"" + String(CurDoor2Enable) + "\",";
//  jsonString += "\"door2name\":\"" + CurDoor2Name + "\",";
//  jsonString += "\"door2state\":\"" + String(CurDoor2State) + "\",";
//  jsonString += "\"door2relay\":\"" + String(CurDoor2Relay) + "\",";
//  jsonString += "\"door3enable\":\"" + String(CurDoor3Enable) + "\",";
//  jsonString += "\"door3name\":\"" + CurDoor3Name + "\",";
//  jsonString += "\"door3state\":\"" + String(CurDoor3State) + "\",";
//  jsonString += "\"door3relay\":\"" + String(CurDoor3Relay) + "\",";
//  jsonString += "\"safeip\":\"" + String(SafeIP) + "\",";
//  jsonString += "\"safeport\":\"" + String(SafePort) + "\",";
//  jsonString += "\"method\":\"" + String(Method) + "\",";
//  jsonString += "\"openbody\":\"" + String(OpenBody) + "\",";
//  jsonString += "\"closebody\":\"" + String(CloseBody) + "\",";
//  jsonString += "\"door1url\":\"" + String(Door1Url) + "\",";
//  jsonString += "\"door2url\":\"" + String(Door2Url) + "\",";
//  jsonString += "\"door3url\":\"" + String(Door3Url) + "\"";
//  jsonString += "}";
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["door1enable"] = String(CurDoor1Enable);
  root["door1name"] = CurDoor1Name;
  root["door1state"] = String(CurDoor1State);
  root["door1relay"] = String(CurDoor1Relay);
  root["door2enable"] = String(CurDoor2Enable);
  root["door2name"] = CurDoor2Name;
  root["door2state"] = String(CurDoor2State);
  root["door2relay"] = String(CurDoor2Relay);
  root["door3enable"] = String(CurDoor3Enable);
  root["door3name"] = CurDoor3Name;
  root["door3state"] = String(CurDoor3State);
  root["door3relay"] = String(CurDoor3Relay);  
  root["safeip"] = String(SafeIP);
  root["safeport"] = String(SafePort);
  root["method"] = String(Method);
  root["openbody"] = String(OpenBody);
  root["closebody"] = String(CloseBody);
  root["door1url"] = String(Door1Url);
  root["door2url"] = String(Door2Url);
  root["door3url"] = String(Door3Url);
  root.prettyPrintTo(jsonString);

  File f = SPIFFS.open("/setup.json", "w");
  if (!f) {
    DebugPrintln("Unable to open setup.json");
  }
  f.print(jsonString);
  f.close();
  DebugPrintln("Current config written to /setup.json");
  return;
}

void NotifyHomeAutomation()  //Notify home automation server that a door state has manually changed
{
  int str_len = SafeIP.length() + 1;
  char AdminIP[str_len];
  SafeIP.toCharArray(AdminIP, str_len); //Convert SAFEIP to CharArray
  byte ServerIP[4];
  parseBytes(AdminIP, '.', ServerIP, 4, 10);  //Convert ADMINIP into BYTE format for client.connect
  int ServerPort = SafePort.toInt();
  String SetState = "";

  WiFiClient client;
  if (!client.connect(ServerIP, ServerPort)) {
    Serial.println("Home Automation connection failed");
    return; 
  }
  
  if (Door1Enable) {
    String Url = Method + " " + Door1Url + " HTTP/1.1";
    if (Door1State == 0) {
      SetState = OpenBody;
    }
    else {
      SetState = CloseBody;
    }
    client.println(Url);
    client.println("Host: " + SafeIP);
    client.print("Content-Length: ");
    client.println(SetState.length());
    client.println();
    client.print(SetState);
    client.println();
    Serial.println(Url + " " + SetState);
  }
  if (Door2Enable) {
    String Url = Method + " " + Door2Url + " HTTP/1.1";
    if (Door2State == 0) {
      SetState = OpenBody;
    }
    else {
      SetState = CloseBody;
    }
    client.println(Url);
    client.println("Host: " + SafeIP);
    client.print("Content-Length: ");
    client.println(SetState.length());
    client.println();
    client.print(SetState);
    client.println();
    Serial.println(Url + " " + SetState);
  }
  if (Door3Enable) {
    String Url = Method + " " + Door3Url + " HTTP/1.1";
    if (Door3State == 0) {
      SetState = OpenBody;
    }
    else {
      SetState = CloseBody;
    }
    client.println(Url);
    client.println("Host: " + SafeIP);
    client.print("Content-Length: ");
    client.println(SetState.length());
    client.println();
    client.print(SetState);
    client.println();
    Serial.println(Url + " " + SetState);
  }
  client.stop();
  DebugPrintln("Notified HA Server of door state changes");
  return;
}

int BinaryState(String State)  //Change open/close string to boolean
{
  int BinState = 0;
  if (State == "open") {
    BinState = 0;
  }
  else if (State == "close") {
    BinState = 1;
  }
  return BinState;
}
