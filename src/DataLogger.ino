#include <ESP8266WiFiMulti.h>
#include "Adafruit_MCP9808.h"
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <SD.h>

#define SD_PIN 15

const int timeZone = -4;
static const char ntpServerName[] = "us.pool.ntp.org";
Adafruit_MCP9808 temperatureSensor = Adafruit_MCP9808();
unsigned int localPort = 8888;
ESP8266WiFiMulti WiFiMulti;
WiFiClient Client;
WiFiUDP Udp;

struct configs {
  int number;
  String values;
  String file;
  String date;
  String time;
};
struct configs configuration; 

void setup() {
  Serial.begin(115200);

  WiFiMulti.addAP("NSA Surveillance Van", "O3fntvrpDc9GDI1NkIKxH");
  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("WiFi Connected");
  Udp.begin(localPort);
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  while (!temperatureSensor.begin()) {
    Serial.println("Couldn't find MCP9808!");
  }

  while (!SD.begin(SD_PIN)) {
    Serial.println("Card failed, or not present");
  }

  configuration = setConfigVariables();
}

void loop() {
  if (connectedToHost()) {
    String temperature = String(askForTemperature(configuration.number));
    String data = createDataString(temperature, &configuration);
    writeDataToDisk(configuration.file, data);
    String csv[10];
    splitCSV(String(configuration.values + data), csv);
    String json = buildJSON(csv);
    sendToClient(json);
    Client.stop();
    float time = csv[5].toFloat() * 60 * 1000;
    delay(time);
  }
}

struct configs setConfigVariables() {
  struct configs currentConfigs;
  File configFile = SD.open("_config.txt");
  if (configFile) {
    while (configFile.available()) {
      currentConfigs.number = configFile.readStringUntil(',').toInt();
      String configVariables = configFile.readStringUntil('\n');
      configVariables.trim();
      currentConfigs.values = configVariables;
      break;
    }
    configFile.close();
  } else {
    Serial.println("error opening _config.txt");
  }
  return currentConfigs;
}

void splitCSV(String dataCSV, String * data) {
  char holder[dataCSV.length() + 1];
  dataCSV.toCharArray(holder, dataCSV.length() + 1);
  int counter = 0;
  int prev = 0;
  for (int i = 0; i <= dataCSV.length() + 1; i++) {
    if (holder[i] == ',') {
      data[counter] = dataCSV.substring(prev, i);
      prev = i + 1;
      counter++;
    }
  }
}
String buildJSON (String* data) {
  StaticJsonBuffer<375> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& systems = jsonBuffer.createObject();
  JsonObject& sensors = jsonBuffer.createObject();
  JsonObject& gps = jsonBuffer.createObject();

  systems["ModelNumber"] = data[0].toInt();
  systems["SerialNumber"] = data[1].toInt();
  systems["Location"] = data[2];
  gps["Lat"] = data[3].toFloat();
  gps["Lon"] = data[4].toFloat();
  systems["Date"] = data[6];
  systems["Time"] = data[7];
  sensors["Water_Temperature"] = data[8].toFloat();

  JsonArray& System = root.createNestedArray("System");
  JsonArray& GPS = systems.createNestedArray("GPS");
  JsonArray& Sensor = root.createNestedArray("Sensors");

  System.add(systems);
  GPS.add(gps);
  Sensor.add(sensors);

  char json[350];
  root.printTo(json, sizeof(json));
  root.prettyPrintTo(Serial);
  String jsonString = json;
  jsonString.replace("[", "");
  jsonString.replace("]", "");
  return jsonString;
}


float askForTemperature(int numberOfReadings) {
  temperatureSensor.shutdown_wake(0);
  delay(250);
  float c = temperatureSensor.readTempC();
  float f = c * 9.0 / 5.0 + 32;
  temperatureSensor.shutdown_wake(1);
  return f;
}

void writeDataToDisk(String fileName, String dataString) {
  File dataFile = SD.open(fileName, FILE_WRITE);
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
  } else {
    Serial.print("error opening " + fileName);
  }
}


boolean connectedToHost() {
  const char * host = "clouddev.mote.org";
  const uint16_t port = 4001;
  while (!Client.connect(host, port)) {
    delay(5000);
    Serial.print(".");
  }
  return true;
}

void sendToClient(String data) {
  Client.print(data);
}

String createDataString(String data, struct configs* configuration) {
  getDateAndTime(configuration);
  String dataString = String(configuration->date + "," + configuration->time + "," + data + ",");
  return dataString;
}

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

time_t getNtpTime() {
  IPAddress ntpServerIP;
  while (Udp.parsePacket() > 0) ;
  Serial.println("Transmit NTP Request");
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response...");
  return 0;
}

void sendNTPpacket(IPAddress &address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void getDateAndTime(struct configs* configuration) {
  String fileName = (String(month()) + "_" + String(year()) + ".txt");
  String date = (String(month()) + "/" + String(day()) + "/" + String(year()));
  String time =  (String(hour()) + ":" + String(minute()) + ":" + String(second()));
  configuration->file = fileName;
  configuration->date = date;
  configuration->time = time;
}

