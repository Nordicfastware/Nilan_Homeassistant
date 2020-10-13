#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include "configuration.h"
#if SERIAL == SERIAL_SOFTWARE
#include <SoftwareSerial.h>
#endif

#define SERIAL_SOFTWARE 1
#define SERIAL_HARDWARE 2

#define HOST "NilanGW-%s" // Change this to whatever you like. 
#define MAXREGSIZE 26
#define SENDINTERVAL 30000 // normally set to 180000 milliseconds = 3 minutes. Define as you like
#define VENTSET 1003
#define RUNSET 1001
#define MODESET 1002
#define TEMPSET 1004
#define PROGSET 500
#define BYPASSSET 1300
#define KEYPRESS 2000

#if SERIAL == SERIAL_SOFTWARE
SoftwareSerial SSerial(SERIAL_SOFTWARE_RX, SERIAL_SOFTWARE_TX); // RX, TX
#endif

const char* ssid = WIFISSID;
const char* password = WIFIPASSWORD;
char chipid[12];
const char* mqttserver = MQTTSERVER;
const char* mqttusername = MQTTUSERNAME;
const char* mqttpassword = MQTTPASSWORD;
WiFiServer server(80);
WiFiClient client;
PubSubClient mqttclient(client);
static long lastMsg = -SENDINTERVAL;
static int16_t rsbuffer[MAXREGSIZE];
ModbusMaster node;

String req[4]; //operation, group, address, value
enum reqtypes
{
  reqtemp = 0,
  reqalarm,
  reqtime,
  reqcontrol,
  reqprogram,
  reqspeed,
  reqairtemp,
  reqairflow,
  reqairheat,
  requser,
  requser2,
  reqinfo,
  reqinputairtemp,
  reqapp,
  reqoutput,
  reqdisplay1,
  reqdisplay2,
  reqdisplay,
  reqmax
};

String groups[] = {   "temp", "alarm", "time", "control", "program", "speed", "airtemp", "airflow", "airheat", "user", "user2", "info", "inputairtemp", "app", "output", "display1", "display2", "display"};
byte regsizes[] = {   23,     10,      6,       8,         1,        2,      6,          2,         0,          6,      6,      14,     7,              4,     26,       4,          4,          1,};
int regaddresses[] = {200,    400,     300,    1000,       500,      200,    1200,       1100,      0,          600,    610,    100,    1200,           0,     100,      2002,       2007,       2000};
byte regtypes[] = {   8,      0,       1,      1,          1,        1,      1,          1,         1,          1,      1,      0,      0,              2,     1,        4,          4,          0};
char *regnames[][MAXREGSIZE] = {
    //temp
    {"T0_Controller", "T1_Intake", "T2_Inlet", "T3_Exhaust", "T4_Outlet", "T5_Cond", "T6_Evap", "T7_Inlet", "T8_Outdoor", "T9_Heater", "T10_Extern", "T11_Top", "T12_Bottom", "T13_Return", "T14_Supply", "T15_Room", "T16", "T17_PreHeat", "T18_PresPibe", "pSuc", "pDis", "RH", "CO2"},
    //alarm
    {"Status", "List_1_ID", "List_1_Date", "List_1_Time", "List_2_ID", "List_2_Date", "List_2_Time", "List_3_ID", "List_3_Date", "List_3_Time"},
    //time
    {"Second", "Minute", "Hour", "Day", "Month", "Year"},
    //control
    {"Type", "RunSet", "ModeSet", "VentSet", "TempSet", "ServiceMode", "ServicePct", "Preset"},
    //week program
    {"WeekProgramSelect"},
    //speed
    {"ExhaustSpeed", "InletSpeed"},
    //airtemp
    {"CoolSet", "TempMinSum", "TempMinWin", "TempMaxSum", "TempMaxWin", "TempSummer"},
    //airflow
    {"AirExchMode", "CoolVent"},
    //airheat
    {},
    //program.user
    {"UserFuncAct", "UserFuncSet", "UserTimeSet", "UserVentSet", "UserTempSet", "UserOffsSet"},
    //program.user2
    {"User2FuncAct", "User2FuncSet", "User2TimeSet", "User2VentSet", "UserTempSet", "UserOffsSet"},
    //info
    {"UserFunc", "AirFilter", "DoorOpen", "Smoke", "MotorThermo", "Frost_overht", "AirFlow", "P_Hi", "P_Lo", "Boil", "3WayPos", "DefrostHG", "Defrost", "UserFunc_2"},
    //inputairtemp
    {"IsSummer", "TempInletSet", "TempControl", "TempRoom", "EffPct", "CapSet", "CapAct"},
    //app
    {"Bus.Version", "VersionMajor", "VersionMinor", "VersionRelease"},
    //output
    {"AirFlap", "SmokeFlap", "BypassOpen", "BypassClose", "AirCircPump", "AirHeatAllow", "AirHeat_1", "AirHeat_2", "AirHeat_3", "Compressor", "Compressor_2", "4WayCool", "HotGasHeat", "HotGasCool", "CondOpen", "CondClose", "WaterHeat", "3WayValve", "CenCircPump", "CenHeat_1", "CenHeat_2", "CenHeat_3", "CenHeatExt", "UserFunc", "UserFunc_2", "Defrosting"},
    //display1
    {"Text_1_2", "Text_3_4", "Text_5_6", "Text_7_8"},
    //display2
    {"Text_9_10", "Text_11_12", "Text_13_14", "Text_15_16"},
    //airbypass (display)
    {"AirBypass/IsOpen"},};

char *getName(reqtypes type, int address)
{
  if (address >= 0 && address <= regsizes[type])
  {
    return regnames[type][address];
  }
  return NULL;
}

JsonObject &HandleRequest(JsonDocument doc)
{
  JsonObject root = doc.to<JsonObject>();
  reqtypes r = reqmax;
  char type = 0;
  if (req[1] != "")
  {
    for (int i = 0; i < reqmax; i++)
    {
      if (groups[i] == req[1])
      {
        r = (reqtypes)i;
      }
    }
  }
  type = regtypes[r];
  if (req[0] == "read")
  {
    int address = 0;
    int nums = 0;
    char result = -1;
    address = regaddresses[r];
    nums = regsizes[r];

    result = ReadModbus(address, nums, rsbuffer, type & 1);
    if (result == 0)
    {
      root["status"] = "Modbus connection OK";
      for (int i = 0; i < nums; i++)
      {
        char *name = getName(r, i);
        if (name != NULL && strlen(name) > 0)
        {
          if ((type & 2 && i > 0) || type & 4)
          {
            String str = "";
            str += (char)(rsbuffer[i] & 0x00ff);
            str += (char)(rsbuffer[i] >> 8);
            root[name] = str;
          }
          else if (type & 8)
          {
            root[name] = rsbuffer[i] / 100.0;
          }
          else
          {
            root[name] = rsbuffer[i];
          }
        }
      }
    }
    else {
      root["status"] = "Modbus connection failed";
    }
    root["requestaddress"] = address;
    root["requestnum"] = nums;
  }
  if (req[0] == "set" && req[2] != "" && req[3] != "")
  {
    int address = atoi(req[2].c_str());
    int value = atoi(req[3].c_str());
    char result = WriteModbus(address, value);
    root["result"] = result;
    root["address"] = address;
    root["value"] = value;
  }
  if (req[0] == "help")
  {
    for (int i = 0; i < reqmax; i++)
    {
      root[groups[i]] = 0;
    }
  }
  root["operation"] = req[0];
  root["group"] = req[1];
  return root;
}

void setup()
{
  if(USE_WIFI_LED) pinMode(WIFI_LED, OUTPUT);
  char host[64];
  sprintf(chipid, "%08X", ESP.getChipId());
  sprintf(host, HOST, chipid);
  delay(500);
  if(CUSTOM_HOSTNAME) 
  {
    WiFi.hostname(CUSTOM_HOSTNAME);
    ArduinoOTA.setHostname(CUSTOM_HOSTNAME);
  } else 
  {
    WiFi.hostname(host);
    ArduinoOTA.setHostname(host);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    if(USE_WIFI_LED) digitalWrite(WIFI_LED, !digitalRead(WIFI_LED));
    delay(5000);
    ESP.restart();
  }
  if (WiFi.status() == WL_CONNECTED && USE_WIFI_LED)
  {
    digitalWrite(WIFI_LED, 0);
  }
  ArduinoOTA.onStart([]() {
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  });
  ArduinoOTA.onError([](ota_error_t error) {
  });
  ArduinoOTA.begin();
  server.begin();

  #if SERIAL == SERIAL_SOFTWARE
    #warning Compiling for software serial
    SSerial.begin(19200); // SERIAL_8E1
    node.begin(30, SSerial);
  #elif SERIAL == SERIAL_HARDWARE
    #warning Compiling for hardware serial
    Serial.begin(19200, SERIAL_8E1);
    node.begin(30, Serial);
  #else
    #error hardware og serial serial port?
  #endif

  mqttclient.setServer(mqttserver, 1883);
  mqttclient.setCallback(mqttcallback);
}

void mqttcallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, "ap/technical/ventilation/ventset") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '4') {
      int16_t speed = payload[0] - '0';
      WriteModbus(VENTSET, speed);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/keypress") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '4') {
      int16_t mode = payload[0] - '0';
      WriteModbus(MODESET, mode);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/modeset") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '4') {
      int16_t mode = payload[0] - '0';
      WriteModbus(MODESET, mode);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/runset") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '1') {
      int16_t run = payload[0] - '0';
      WriteModbus(RUNSET, run);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/progset") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '4') {
      int16_t prog = payload[0] - '0';
      WriteModbus(PROGSET, prog);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/bypassset") == 0) {
    if (length == 1 && payload[0] >= '0' && payload[0] <= '1') {
      int16_t byp = payload[0] - '0';
      WriteModbus(BYPASSSET, byp);
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/keypress") == 0) {
    if (length == 4 && payload[0] >= '0' && payload[0] <= '2') {
      String str;
      for (int i = 0; i < length; i++) {
        str += (char)payload[i];
      }
      WriteModbus(KEYPRESS, str.toInt());
    }
  }
  if (strcmp(topic, "ap/technical/ventilation/tempset") == 0) {
    if (length == 4 && payload[0] >= '0' && payload[0] <= '2') {
      String str;
      for (int i = 0; i < length; i++) {
        str += (char)payload[i];
      }
      WriteModbus(TEMPSET, str.toInt());
    }
  }
  lastMsg = -SENDINTERVAL;
}

bool readRequest(WiFiClient &client)
{
  req[0] = "";
  req[1] = "";
  req[2] = "";
  req[3] = "";

  int n = -1;
  bool readstring = false;
  while (client.connected())
  {
    if (client.available())
    {
      char c = client.read();
      if (c == '\n')
      {
        return false;
      }
      else if (c == '/')
      {
        n++;
      }
      else if (c != ' ' && n >= 0 && n < 4)
      {
        req[n] += c;
      }
      else if (c == ' ' && n >= 0 && n < 4)
      {
        return true;
      }
    }
  }

  return false;
}

void writeResponse(WiFiClient &client, JsonObject root)  
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  serializeJsonPretty(root,client);
}

char ReadModbus(uint16_t addr, uint8_t sizer, int16_t *vals, int type)
{
  char result = 0;
  switch (type)
  {
  case 0:
    result = node.readInputRegisters(addr, sizer);
    break;
  case 1:
    result = node.readHoldingRegisters(addr, sizer);
    break;
  }
  if (result == node.ku8MBSuccess)
  {
    for (int j = 0; j < sizer; j++)
    {
      vals[j] = node.getResponseBuffer(j);
    }
    return result;
  }
  return result;
}
char WriteModbus(uint16_t addr, int16_t val)
{
  node.setTransmitBuffer(0, val);
  char result = 0;
  result = node.writeMultipleRegisters(addr, 1);
  return result;
}

void mqttreconnect()
{
  int numretries = 0;
  while (!mqttclient.connected() && numretries < 3)
  {
    if (mqttclient.connect(chipid, mqttusername, mqttpassword))
    {
      mqttclient.publish("ap/technical/ventilation/status", "Online");
      mqttclient.subscribe("ap/technical/ventilation/ventset");
      mqttclient.subscribe("ap/technical/ventilation/modeset");
      mqttclient.subscribe("ap/technical/ventilation/runset");
      mqttclient.subscribe("ap/technical/ventilation/tempset");
      mqttclient.subscribe("ap/technical/ventilation/progset");
      mqttclient.subscribe("ap/technical/ventilation/bypassset");
      mqttclient.subscribe("ap/technical/ventilation/keypress");
    }
    else
    {
      delay(1000);
    }
    numretries++;
  }
}

void loop()
{
#ifdef DEBUG_TELNET
  // handle Telnet connection for debugging
  handleTelnet();
#endif

  ArduinoOTA.handle();
  WiFiClient client = server.available();
  if (client)
  {
    bool success = readRequest(client);
    if (success)
    {
      StaticJsonDocument<500> doc;
      JsonObject root = HandleRequest(doc); 

      writeResponse(client, root);
    }
    client.stop();
  }

  if (!mqttclient.connected())
  {
    mqttreconnect();
  }

  if (mqttclient.connected())
  {
    mqttclient.loop();
    long now = millis();
    if (now - lastMsg > SENDINTERVAL)
    {
      reqtypes rr[] = {reqtemp, reqcontrol, reqprogram, reqtime, reqinfo, reqoutput, reqdisplay, reqspeed, reqalarm, reqairtemp, reqinputairtemp, reqairflow, requser, reqapp}; // put another register in this line to subscribe
      for (int i = 0; i < (sizeof(rr)/sizeof(rr[0])); i++)
      {
        reqtypes r = rr[i];
        char result = ReadModbus(regaddresses[r], regsizes[r], rsbuffer, regtypes[r] & 1); 
        if (result == 0)
        {
          for (int i = 0; i < regsizes[r]; i++)
          {
            char *name = getName(r, i);
            char numstr[8];
            if (name != NULL && strlen(name) > 0)
            {
              String mqname = "temp/";
              switch (r)
              {
              case reqcontrol:
                mqname = "ap/technical/ventilation/control/"; // Subscribe to the "control" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqprogram:
                mqname = "ap/technical/ventilation/program/"; // Subscribe to the "control" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqtime:
                mqname = "ap/technical/ventilation/time/"; // Subscribe to the "info" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqinfo:
                mqname = "ap/technical/ventilation/info/"; // Subscribe to the "info" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqoutput:
                mqname = "ap/technical/ventilation/output/"; // Subscribe to the "output" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqspeed:
                mqname = "ap/technical/ventilation/speed/"; // Subscribe to the "speed" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqalarm:
                mqname = "ap/technical/ventilation/alarm/"; // Subscribe to the "alarm" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqairtemp:
                mqname = "ap/technical/ventilation/airtemp/"; // Subscribe to the "airtemp" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqinputairtemp:
                mqname = "ap/technical/ventilation/inputairtemp/"; // Subscribe to the "inputairtemp" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqairflow:
                mqname = "ap/technical/ventilation/airflow/"; // Subscribe to the "airflow" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqapp:
                mqname = "ap/technical/ventilation/app/"; // Subscribe to the "app" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case requser:
                mqname = "ap/technical/ventilation/user/"; // Subscribe to the "user" register
                itoa((rsbuffer[i]), numstr, 10);
                break;
              case reqdisplay:
                mqname = "ap/technical/ventilation/display/"; // Subscribe to the "AirBypass.IsOpen" register
                itoa((rsbuffer[i]), numstr, 10);
                break;            
              case reqtemp:
                if (strncmp("RH", name, 2) == 0) {
                  mqname = "ap/technical/ventilation/humidity/"; // Subscribe to humidity-level
                } else {
                  mqname = "ap/technical/ventilation/temp/"; // Subscribe to "temp" register
                }
                dtostrf((rsbuffer[i] / 100.0), 5, 2, numstr);
                break;
              }
              mqname += (char *)name;
              mqttclient.publish(mqname.c_str(), numstr);
            }
          }
        }
      }

      // Handle text fields
      reqtypes rr2[] = {reqdisplay1, reqdisplay2}; // put another register in this line to subscribe
      for (int i = 0; i < (sizeof(rr2)/sizeof(rr2[0])); i++) // change value "5" to how many registers you want to subscribe to
      {
        reqtypes r = rr2[i];

        char result = ReadModbus(regaddresses[r], regsizes[r], rsbuffer, regtypes[r] & 1);
        if (result == 0)
        {
          String text = "";
          String mqname = "ap/technical/ventilation/text/";

          for (int i = 0; i < regsizes[r]; i++)
          {
              char *name = getName(r, i);

              if ((rsbuffer[i] & 0x00ff) == 0xDF) {
                text += (char)0x20; // replace degree sign with space
              } else {
                text += (char)(rsbuffer[i] & 0x00ff);
              }
              if ((rsbuffer[i] >> 8) == 0xDF) {
                text += (char)0x20; // replace degree sign with space
              } else {
                text += (char)(rsbuffer[i] >> 8);
              }
              mqname += (char *)name;
          }
          mqttclient.publish(mqname.c_str(), text.c_str());
        }
      }
      lastMsg = now;
    }
  }
}
