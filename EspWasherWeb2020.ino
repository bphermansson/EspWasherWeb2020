// Have to use Arduino IDE, PlatformIO gives trouble with libs, especially EmonLiteESP.

#include <ESP8266WiFi.h>
//#include <ESP8266WiFiMulti.h>
//#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "ESP8266_Nokia5110.h"
#include <dht11.h>
#include <PubSubClient.h>
#include "EmonLiteESP.h"  // For measuring current. 
#include <time.h>
#include <TimeLib.h>
#include "readDht.h"
#include "readPower.h"
#include "printTime.h"
#include "settings.h"

#ifndef STASSID
#define STASSID "BrandstorpWifi"
#define STAPSK  "Brandstorp"
#endif

const char* ssid     = STASSID;
const char* password = STAPSK;

/* Setup hardware */
ESP8266WebServer server(80);
ESP8266_Nokia5110 lcd = ESP8266_Nokia5110(PIN_SCLK,PIN_SDIN,PIN_DC,PIN_SCE,PIN_RESET);
EmonLiteESP power;  // Emoncms power lib
dht11 DHT11;

// Network
// WiFi & Mqtt
WiFiClient WasherWatch;

void callback(char* topic, byte* payload, unsigned int length);

PubSubClient client(MQTT_SERVER, 1883, callback, WasherWatch);

//int status = WL_IDLE_STATUS;

/* Variables */
String temp, hum, output, lip, shour, sminute;
double current, realPower;
char buffer[100];
time_t t;

char datetimebuffer[80];
char hmbuffer[10];
struct tm * timeinfo;
String totalTime;

long lastMsg = 0;

const size_t capacity = JSON_OBJECT_SIZE(6) + 65;
DynamicJsonDocument doc(capacity);

unsigned int currentCallback() {
  // If using the ADC GPIO in the ESP8266
  return analogRead(CURRENT_PIN);
}

void handleRoot() {
    Serial.print("Will send this to browser: ");
    Serial.println(output);
    server.send(200, "text/plain", output);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to ");
    Serial.println(MQTT_SERVER);
    if (client.connect(APPNAME, "emonpi", "emonpimqtt2016")) {
      Serial.println("connected");
       client.subscribe(MQTT_TIME_TOPIC);
    } 
    else 
    {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  Serial.setDebugOutput(true);  // WiFi debug info to serial port
 
  // Start lcd
  lcd.begin();
  lcd.clear();
  lcd.setContrast(0x20);
  lcd.setCursor(0,0);
  lcd.print("Hello World");  

  Serial.println();
  Serial.print("Wait for WiFi... ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.config(staticIP, gateway, subnet, dns);

  while ( WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());

  // Further server settings
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
 
  // Mqtt
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
  if (!client.connected()) {
    reconnect();
  }
  Serial.println("MQTT up and running");
  client.publish(MQTT_PUB_TOPIC, "Setup finished");

   ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA up and running");
  
  // Init current meter
  power.initCurrent(currentCallback, ADC_BITS, REFERENCE_VOLTAGE, CURRENT_RATIO);  

  // Initial readings
  //readDht();
  //realPower = readPower();

  Serial.println("Setup done.");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  // "State machine", check sensor and send values when 2 minutes have passed. 
  unsigned long tnow = millis();
  //  Serial.println(tnow);

  //if (tnow - lastMsg > 120000) {  // Every 2 minutes
  //if (tnow - lastMsg > 10000) {  // Every 10 seconds
  if (tnow - lastMsg > 25000) {  // Every 25 seconds
    lastMsg = tnow;
    Serial.print("Timestamp: ");
    Serial.println(tnow);
    //printLocalTime();

    Serial.println("Read sensors");
    readDht();
    readPower();
    String LocalIP = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." +WiFi.localIP()[3];
    int Rssi = WiFi.RSSI();
    
    Serial.println("Values:");
    Serial.println(temp);
    Serial.println(hum);
    Serial.println(realPower);

    Serial.print("Ip: ");
    Serial.println(LocalIP);
    Serial.print("RSSI: ");
    Serial.println(Rssi);

    Serial.print("Clock: ");
    Serial.println(totalTime);

    // Update lcd
    Serial.println("Update lcd");
    lcd.clear();
    showOnLcd(LocalIP, totalTime, temp, hum, String(realPower));

    // Convert time
    String stime = shour + sminute;
    int itime = stime.toInt();
      
    Serial.println("Send by Mqtt");

    const size_t capacity = JSON_OBJECT_SIZE(7);
    DynamicJsonDocument doc(capacity);    
    doc["temp"] = temp.toInt();
    doc["hum"] = hum.toInt();
    doc["power"] = int(realPower);
    doc["ip"] = LocalIP;
    doc["Rssi"] = Rssi;
    doc["timestamp"] = tnow;
    doc["time"] = itime;
    
    output="";
    serializeJson(doc, output);
    Serial.println(output);

    if (!client.connected()) {
      Serial.println("Reconnect");  
      reconnect();
    }  
    client.publish(MQTT_PUB_TOPIC, (char*) output.c_str(), true);  // Send with retain=true
    Serial.println("Done");
  }
  client.loop();
}
