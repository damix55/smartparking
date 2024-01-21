#include <ESP8266WiFi.h>                            // WiFi library
#include <MQTT.h>
#include <ESP8266HTTPClient.h>
HTTPClient http;

#include "secrets.h"
#include "config.h"

#define PHOTORESISTOR A0                            // photoresistor pin    
#define LED D1                                      // LED pin
#define LED_ESP8266 LED_BUILTIN                     // LED MQTT
#define LED_ONBOARD LED_BUILTIN_AUX                 // LED WiFi

// WiFi cfg
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
#ifdef IP
IPAddress ip(IP);
IPAddress subnet(SUBNET);
IPAddress dns(DNS);
IPAddress gateway(GATEWAY);
#endif

// MQTT data
MQTTClient mqttClient;            // handles the MQTT communication protocol
WiFiClient networkClient;         // handles the network connection to the MQTT broker

String mqttTopic = "smartParking/" + (String) PARKING_NAME + "/" + WiFi.macAddress();
String mqttTopicStatus = mqttTopic + "/status";            // topic to publish the parking status (free/occupied)
String mqttTopicOnline = mqttTopic + "/online";            // topic to publish the parking
String mqttTopicBooked = mqttTopic + "/booked";            // topic to control the parking status
String mqttTopicClosed = mqttTopic + "/closed";            // topic to control the parking status

unsigned long lastThresholdUpdate = 0;

bool isFree;
bool booked = false;
bool closed = false;

int photoresistorThreshold = (PHOTORESISTOR_MIN + PHOTORESISTOR_MAX)/2;

void setup() {
  isFree = false;
  
  WiFi.mode(WIFI_STA);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, HIGH);

  pinMode(LED_ESP8266, OUTPUT);
  digitalWrite(LED_ESP8266, HIGH);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  pinMode(LED_ESP8266, OUTPUT);
  digitalWrite(LED_ESP8266, HIGH);

  // setup MQTT
  mqttClient.begin(MQTT_BROKERIP, 1883, networkClient);   // setup communication with MQTT broker
  mqttClient.onMessage(mqttMessageReceived);              // callback on message received from MQTT broker

  Serial.begin(115200);
  Serial.println(F("\n\nSetup completed.\n\n"));
}

void loop() {
  static unsigned int lightSensorValue;
  static bool firstLoop = true;

  connectToWiFi();                                       // connect to WiFi

  connectToMQTTBroker();                                 // connect to MQTT broker (if not already connected)
  mqttClient.loop();                                     // MQTT client loop

  lightSensorValue = analogRead(PHOTORESISTOR);
//  Serial.print(F("Light sensor value: "));
  Serial.println(lightSensorValue);
  getPhotoresistorThreshold();

  if (firstLoop) {
    isFree = lightSensorValue < photoresistorThreshold;
    firstLoop = false;
  }
  
  if (lightSensorValue >= photoresistorThreshold && !isFree) {
    isFree = true;
    mqttClient.publish(mqttTopicStatus, "free", true, 0);            // parking free (led on)
    
  } else if (lightSensorValue < photoresistorThreshold && isFree) {
    isFree = false;
    mqttClient.publish(mqttTopicStatus, "occupied", true, 0);       // parking occupied (led off)
  }

  if (booked || !isFree || closed) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }

  if (closed) {
    Serial.println("Deep sleeping...");
    ESP.deepSleep(0);
  }

  delay(200);
}

int getPhotoresistorThreshold(){
  if(millis() - lastThresholdUpdate > 5*60*1000){
    http.begin((String) FLASK_URL + "/parking/" + (String) PARKING_NAME + "/threshold");
    int httpResponseCode = http.POST("{\"min_threshold\": " + (String) PHOTORESISTOR_MIN + ", " + "\"max_threshold\": " + (String) PHOTORESISTOR_MAX + "}");
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    if(httpResponseCode == 200){
      String payload = http.getString();
      http.end();
      photoresistorThreshold = std::atoi(payload.c_str());
    }
    http.end();
    lastThresholdUpdate = millis();
  } 
  Serial.println(photoresistorThreshold);
  return photoresistorThreshold;
}

void connectToWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("Attempting to connect to SSID: "));
    Serial.println(SECRET_SSID);

    while (WiFi.status() != WL_CONNECTED) {
#ifdef IP
      WiFi.config(ip, dns, gateway, subnet);
#endif
      WiFi.mode(WIFI_STA);
      WiFi.begin(SECRET_SSID, SECRET_PASS);
      Serial.print(F("."));
      digitalWrite(LED_ONBOARD, HIGH);       // led off when not connected
      delay(5000);
    }
    digitalWrite(LED_ONBOARD, LOW);          // led on when connected
    Serial.println(F("\nConnected"));
    printWifiStatus();
  } else {
    digitalWrite(LED_ONBOARD, LOW);
  }
}

void connectToMQTTBroker() {
  if (!mqttClient.connected()) {   // not connected
    Serial.print(F("\nConnecting to MQTT broker..."));
    mqttClient.setWill(mqttTopicOnline.c_str(), "false", true, 0);
    while (!mqttClient.connect(WiFi.macAddress().c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print(F("."));
      digitalWrite(LED_ESP8266, HIGH);      // led off when not connected
      delay(1000);
    }
    digitalWrite(LED_ESP8266, LOW);         // led on when connected
    Serial.println(F("\nConnected!"));
    mqttClient.publish(mqttTopicOnline, "true", true, 0);
    mqttClient.publish(mqttTopicBooked, booked ? "true" : "false", true, 0);
    mqttClient.publish(mqttTopicClosed, closed ? "true" : "false", true, 0);

    // connected to broker, subscribe topics
    mqttClient.subscribe(mqttTopicBooked);
    mqttClient.subscribe(mqttTopicClosed);
    
    Serial.println(F("\nSubscribed to booked topic!"));
  } else {
    digitalWrite(LED_ESP8266, LOW);
  }
}

void mqttMessageReceived(String &topic, String &payload) {
  
    // this function handles a message from the MQTT broker
    Serial.println("Incoming MQTT message: " + topic + " - " + payload);

    if (topic == mqttTopicBooked) {
      if (payload == "true") {
        booked = true;
      } else if (payload == "false") {
        booked = false;
      } else {
        Serial.println(F("MQTT Payload not recognized, message skipped"));
      }
    } else if (topic == mqttTopicClosed) {
      if (payload == "true") {
        closed = true;
      } else if (payload == "false") {
        closed = false;
      } else {
        Serial.println(F("MQTT Payload not recognized, message skipped"));
      }
    }
}

void printWifiStatus() {
  Serial.println(F("\n=== WiFi connection status ==="));

  // SSID
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());

  // signal strength
  Serial.print(F("Signal strength (RSSI): "));
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // current IP
  Serial.print(F("IP Address: "));
  Serial.println(WiFi.localIP());

  // subnet mask
  Serial.print(F("Subnet mask: "));
  Serial.println(WiFi.subnetMask());

  // gateway
  Serial.print(F("Gateway IP: "));
  Serial.println(WiFi.gatewayIP());

  // DNS
  Serial.print(F("DNS IP: "));
  Serial.println(WiFi.dnsIP());

  Serial.println(F("==============================\n"));
}
