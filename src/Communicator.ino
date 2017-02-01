#include <ESP8266WiFiMulti.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

ESP8266WiFiMulti WiFiMulti;
SoftwareSerial AboveWaterSerial(12, 14);
WiFiClient Client;

 String askForData() {
   /* Grabs the temperature and configuration values from the other chips

   Asks the AboveWater Arduino for an entry of data and parses it into an array.
   The data included the temperature reading from the BelowWater Arduino.
   With this we constuct an array consumable by the JSON builder.
   */

   String dataCSV;
   AboveWaterSerial.print('d');
   while(dataCSV.length() <= 0) {
     dataCSV = Serial.readStringUntil('z');
   }
   return dataCSV;
 }

 void splitCSV(String dataCSV, String * data){
   /* Splits the received message into an array we use to build the JSON

   When it comes over the wire the data is in CSV form (valueA,valueB,valueC,)
   We pass this function an empty array which we can populate with the separated
   values.
   */

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
   /* Builds the data object expected by the server

   The server expects all data to be in JSON format, so using the array we created
   with the CSV values we constuct an object including all relevant data.
   */

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
   jsonString.replace("[","");
   jsonString.replace("]","");
   return jsonString;
 }

 boolean connectedToHost() {
   // Attempts to make a connection with the server we'll send our data

   const char * host = "clouddev.mote.org";
   const uint16_t port = 4001;
   while (!Client.connect(host, port)) {
     delay(5000);
     Serial.print(".");
   }
   return true;
 }

 void sendToClient(String data) {
   // Sends the data we constructed to the server

   Serial.println(data);
   Client.print(data);
 }

void setup() {
  Serial.begin(115200);
  AboveWaterSerial.begin(115200);
  WiFiMulti.addAP("Verizon-791L-00CC", "78d8b2d1");
  while (WiFiMulti.run() != WL_CONNECTED) {
     Serial.print(".");
     delay(500);
   }
   Serial.println("WiFi Connected");
 }

 void loop() {
   /* Gathers the temperature and metadata and sends it to the server.

   We ask the AboveWater Arduino for data, which ultimately results in a CSV
   message being sent over serial. That string is separated into an array, which
   is used to create a JSON object. Once constructed we send that JSON to the
   server and delay the set amount of time.
   */

   if (connectedToHost()) {
     String rawData = askForData();
     String data[10];
     splitCSV(rawData, data);
     String json = buildJSON(data);
     sendToClient(json);
     Client.stop();
     float time = data[5].toFloat() * 60 * 1000;
     delay(time);
     }
 }
