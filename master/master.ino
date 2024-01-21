#include "secrets.h"
#include "config.h"

#include <ESP8266WiFi.h>                            // WiFi library
#include <MQTT.h>

#include <ArduinoJson.h>
#include <string.h>

// include Web server library
#include <ESP8266WebServer.h>
ESP8266WebServer server(80);   // HTTP server on port 80

// Include Display libraries
#include <LiquidCrystal_I2C.h>   // display library
#include <Wire.h>                // I2C library

#define DISPLAY_CHARS 16    // number of characters on a line
#define DISPLAY_LINES 2     // number of display lines
#define DISPLAY_ADDR 0x27   // display address on I2C bus

LiquidCrystal_I2C lcd(DISPLAY_ADDR, DISPLAY_CHARS, DISPLAY_LINES);   // display object

// Include IR libraries
#include <IRrecv.h>            // receiver data types
#include <IRremoteESP8266.h>   // library core
#include <IRutils.h>           // print function

// Include Servo library
#include <Servo.h>

#define RECV_PIN D3                // receiver input pin
#define LED D5
#define SERVO_PIN D6               // PWM pin for servo control
#define PHOTORESISTOR A0           // photoresistor pin
#define LED_ESP8266 LED_BUILTIN    // LED WiFi
#define SLAVE_RESET D7

IRrecv irrecv(RECV_PIN);
Servo servo;

// Servo Motor init
// calibrate the servo
// as per the datasheet ~1ms=-90°, ~2ms=90°, 1.5ms=0°
// in reality seems that the values are: ~0.5ms=-90°, ~2.5ms=90°
#define SERVO_PWM_MIN 500    // minimum PWM pulse in microsecond
#define SERVO_PWM_MAX 2500   // maximum PWM pulse in microsecond
#define DELAY_SERVO 20       // delay between servo position changes

// WiFi cfg
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
#ifdef IP
IPAddress ip(IP);
IPAddress subnet(SUBNET);
IPAddress dns(DNS);
IPAddress gateway(GATEWAY);
#endif
WiFiClient client;

#include <ESP8266HTTPClient.h>
HTTPClient http;

// MySQL libraries
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

// MySQL server cfg
char mysql_user[] = MYSQL_USER;       // MySQL user login username
char mysql_password[] = MYSQL_PASS;   // MySQL user login password
IPAddress server_addr(MYSQL_IP);      // IP of the MySQL *server* here
MySQL_Connection conn((Client *)&client);

// InfluxDB library
#include <InfluxDbClient.h>
// InfluxDB cfg
InfluxDBClient client_idb(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
Point pointDevice("device_status");

// MQTT data
MQTTClient mqttClient;            // handles the MQTT communication protocol
WiFiClient networkClient;         // handles the network connection to the MQTT broker

String mqttTopic = "smartParking/" + (String) PARKING_NAME + "/" + WiFi.macAddress();
String mqttTopicSub = "smartParking/" + (String) PARKING_NAME + "/#";
String mqttTopicStatus = mqttTopic + "/status";                                        // topic to publish the parking
String mqttTopicOnline = mqttTopic + "/online";                                        // topic to publish the parking
String mqttTopicBooked = mqttTopic + "/booked";                                        // topic to control the parking status


unsigned long lastIRMeasure = 0;
unsigned long lastIRSignal;
unsigned long lastInfluxWrite = 0;
bool lastIRSignalStatus;
unsigned long lastThresholdUpdate = 0;

bool isFree;
bool booked = false;
bool closed = false;

int freeParking = 0;
int totalParking = 0;

String lastDisplayed = "";

decode_results resultsIR;

StaticJsonDocument<400> network;
int photoresistorThreshold = (PHOTORESISTOR_MIN + PHOTORESISTOR_MAX)/2;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  digitalWrite(LED_ESP8266, HIGH);

  pinMode(SLAVE_RESET, OUTPUT);
  digitalWrite(SLAVE_RESET, HIGH);

  server.on("/", handle_root);
  server.on("/book", HTTP_POST, handle_book);
  server.on("/close", HTTP_POST, handle_close);

  // setup MQTT
  mqttClient.begin(MQTT_BROKERIP, 1883, networkClient);   // setup communication with MQTT broker
  mqttClient.onMessage(mqttMessageReceived);              // callback on message received from MQTT broker


  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  irrecv.enableIRIn(true);                                 // enable the receiver
  lastIRSignal = millis();

  servo.attach(SERVO_PIN, SERVO_PWM_MIN, SERVO_PWM_MAX);
  servo.write(0);

  Wire.begin();
  Wire.beginTransmission(DISPLAY_ADDR);
  byte error = Wire.endTransmission();

  if (error == 0) {
    Serial.println(F("LCD found."));
    lcd.begin(DISPLAY_CHARS, 2);   // initialize the lcd
    lcd.setBacklight(255);
    displayInformations();


  } else {
    Serial.print(F("LCD not found. Error "));
    Serial.println(error);
    Serial.println(F("Check connections and configuration. Reset to try again!"));
    while (true)
      delay(1);
  }

  server.begin();
  Serial.println(F("HTTP server started"));
  printWifiStatus();

  Serial.println(F("\n\nSetup completed.\n\n"));
}


void loop() {
  static unsigned int lightSensorValue;
  static bool firstLoop = true;

  wifi_connection();

  displayInformations();

  connectToMQTTBroker();                                 // connect to MQTT broker (if not already connected)
  mqttClient.loop();                                     // MQTT client loop

  writeToInflux();

  server.handleClient();   // listening for clients on port 80

  lightSensorValue = analogRead(PHOTORESISTOR);
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


  if (!closed) {
    if (!presenceDetectorIR()) {                                      // if car is not there
  
      if (servo.read() == 90) {                                       // if the bar is opened close it
        for (int pos = 90; pos >= 0; pos--) {
          servo.write(pos);
          delay(DELAY_SERVO);
        }
      } else {
        servo.write(0);
      }
  
    } else {
      if (servo.read() == 90) {                                       // if car is there keep the bar opened
        servo.write(90);
        delay(DELAY_SERVO);
  
      } else {
        for (int pos = 0; pos <= 90; pos++) {                         // otherwise open the bar
          servo.write(pos);
          delay(DELAY_SERVO);
        }
      }
    }
  } else {
    if (servo.read() == 90) {      
      for (int pos = 90; pos >= 0; pos--) {
        servo.write(pos);
        delay(DELAY_SERVO);
      }
    }
  }
  delay(200);
}

void wifi_connection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("Connecting to SSID: "));
    Serial.println(SECRET_SSID);
    lcd.home();
    lcd.clear();
    lcd.print("Connecting to");
    lcd.setCursor(0, 1);
    lcd.print(SECRET_SSID);

    #ifdef IP
    WiFi.config(ip, dns, gateway, subnet);   // by default network is configured using DHCP
    #endif

    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(F("."));
      digitalWrite(LED_ESP8266, HIGH);
      delay(250);
    }
    digitalWrite(LED_ESP8266, LOW);
    Serial.println(F("\nConnected!"));
    
    printWifiStatus();
    writeToDB();
  } else {
    digitalWrite(LED_ESP8266, LOW);
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


bool presenceDetectorIR() {
  if (millis() - lastIRMeasure > 200) {
    lastIRMeasure = millis();
    bool signalReceived = irrecv.decode(&resultsIR);
    irrecv.resume();

    if (signalReceived) {
      lastIRSignal = millis();
      lastIRSignalStatus = false;
      return false;
    }
    else {
      if (millis() - lastIRSignal > 1500) {
        lastIRSignalStatus = true;
        return true;
      }
      lastIRSignalStatus = false;
      return false;
    }

  }

  return lastIRSignalStatus;

}


void displayInformations() {
  if (closed) {
    if(!lastDisplayed.equals("chiuso")) {
      lastDisplayed = "chiuso";
      lcd.home();               // move cursor to 0,0
      lcd.clear();              // clear text
      lcd.print(" SMART PARKING  ");   // show text
      lcd.setCursor(0, 1);
      lcd.print(" chiuso  ");   // show text
    }
  } else {
    if (updateParkingNumber() || !lastDisplayed.equals("posti")) {
      lastDisplayed = "posti";
      lcd.home();               // move cursor to 0,0
      //lcd.clear();              // clear text
      lcd.print(" SMART PARKING  ");   // show text
      lcd.setCursor(0, 1);
      lcd.print(freeParking);
      lcd.print("/");
      lcd.print(totalParking);
      lcd.print(" posti liberi");   // show text
    }
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

    // connected to broker, subscribe topics
    mqttClient.subscribe(mqttTopicSub);
    Serial.println(F("\nSubscribed!"));
  } else {
    digitalWrite(LED_ESP8266, LOW);
  }
}


void mqttMessageReceived(String &topic, String &payload) {
  // this function handles a message from the MQTT broker
  Serial.println("Incoming MQTT message: " + topic + " - " + payload);

  if (topic == mqttTopicBooked) {
    if (payload == "false") {
      booked = false;
    } else {
      booked = true;
    }
  }

  String msg_mac = topic.substring(mqttTopic.length() - 17, mqttTopic.length());
  String msg_key = topic.substring(mqttTopic.length() + 1);

  JsonObject parking_data = network[msg_mac];

  if (parking_data.isNull()) {
    parking_data = network.createNestedObject(msg_mac);
  }
  parking_data[msg_key] = payload;

  String output;
  serializeJson(network, output);
  sendHttpPost((String) FLASK_URL + "/parking/" + (String) PARKING_NAME, output);
}


void handle_root() {
  String output;
  serializeJson(network, output);
  server.send(200, F("application/json"), output);
}


void handle_book() {
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, server.arg("plain"));

  Serial.println(server.arg("plain"));

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    server.send(400);
    return;
  }

  String targetMac = body["mac"];
  String bookStatus = body["book"];

  if (!network[targetMac].isNull()) {
    mqttClient.publish("smartParking/" + (String) PARKING_NAME + "/" + targetMac + "/booked", bookStatus, true, 0);
    server.send(200);
  }
  else {
    server.send(400);
  }
}


void handle_close() {
  StaticJsonDocument<200> body;
  DeserializationError desError = deserializeJson(body, server.arg("plain"));

  Serial.println(server.arg("plain"));

  if (desError) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(desError.f_str());
    server.send(400);
    return;
  }

  String closeStatus = body["close"];
  Serial.println(closeStatus);

  if (closeStatus.equals("true") || closeStatus.equals("false")) {
    JsonObject root = network.as<JsonObject>();
    
    for (JsonPair kv : root) {
      String parkingMac = kv.key().c_str();
      Serial.println(parkingMac);
      mqttClient.publish("smartParking/" + (String) PARKING_NAME + "/" + parkingMac + "/closed", closeStatus, true, 0);
    }

    //set variabile
    closed = closeStatus.equals("true");
    

    //set db
    int error;
    if (conn.connect(server_addr, 3306, mysql_user, mysql_password)) {
      Serial.println(F("MySQL connection established."));
  
      MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);

      String closeStatusNum = closeStatus.equals("true") ? "1" : "0";
  
      String query = "UPDATE `ddovico`.`parking` SET `closed` = '" + closeStatusNum + "' WHERE (`id` = '" + (String) PARKING_NAME + "');";
      Serial.println(query);
      
      cur_mem->execute(query.c_str());
      // Note: since there are no results, we do not need to read any data
      // deleting the cursor also frees up memory used
      delete cur_mem;
      error = 1;
      Serial.println(F("Data recorded on MySQL"));
  
      conn.close();
    } else {
      Serial.println(F("MySQL connection failed."));
      error = -1;
    }

    if (!closed) {
      // wake slaves
      digitalWrite(SLAVE_RESET, LOW);
      delay(200);
      digitalWrite(SLAVE_RESET, HIGH);
    }
    
    server.send(200);
  }
  else {
    server.send(400);
  }
}


bool sendHttpPost(String url, String data) {
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(data);
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  http.end();
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


bool updateParkingNumber() {
  int freeCount = 0;
  int totalCount = 0;

  JsonObject root = network.as<JsonObject>();
  
  for (JsonPair kv : root) {
    String parkingOnline = kv.value()["online"].as<char*>();
    String parkingBooked = kv.value()["booked"].as<char*>();
    String parkingStatus = kv.value()["status"].as<char*>();
    
    if (parkingOnline.equals("true")) {
      totalCount += 1;
      if (parkingBooked.equals("false") && parkingStatus.equals("free")) {
        freeCount += 1;
      }
    }
  }

  if (freeParking != freeCount || totalParking != totalCount) {
    freeParking = freeCount;
    totalParking = totalCount;
    return true;
  }
  return false;
}


int writeToDB() {
  int error;
  if (conn.connect(server_addr, 3306, mysql_user, mysql_password)) {
    Serial.println(F("MySQL connection established."));

    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);

    String publicIp = PARKING_EXTERNAL_ADDRESS;
    String parkingName = PARKING_NAME;
    String parkingFullName = PARKING_FULL_NAME;
    String parkingPosition = PARKING_POSITION;
    String parkingAddress = PARKING_ADDRESS;

    String query = "INSERT INTO `ddovico`.`parking` (`id`, `name`, `position`, `closed`, `ipAddress`, `address`) "
    "VALUES ('" + parkingName + "', '" + parkingFullName + "', '" + parkingPosition + "', '" + closed + "', '" + publicIp + "', '" + parkingAddress + "')"
    "ON DUPLICATE KEY UPDATE `name`='" + parkingFullName + "', `position`='" + parkingPosition + "', `closed`='" + closed + "', `ipAddress`='" + publicIp + "', `address`='" + parkingAddress + "'";

    Serial.println(query);
    
    cur_mem->execute(query.c_str());
    // Note: since there are no results, we do not need to read any data
    // deleting the cursor also frees up memory used
    delete cur_mem;
    error = 1;
    Serial.println(F("Data recorded on MySQL"));

    conn.close();
  } else {
    Serial.println(F("MySQL connection failed."));
    error = -1;
  }

  return error;
}


int writeToInflux() {
  if(millis() - 10000 > lastInfluxWrite) {
    lastInfluxWrite = millis();
    // Store measured value into point
    pointDevice.clearFields();
    pointDevice.addField("postiLiberi", freeParking);
    pointDevice.addField("postiTotali", totalParking);
    pointDevice.addField("chiuso", closed);
    Serial.print(F("Writing: "));
    Serial.println(pointDevice.toLineProtocol());
    if (!client_idb.writePoint(pointDevice)) {
      Serial.print(F("InfluxDB write failed: "));
      Serial.println(client_idb.getLastErrorMessage());
    }
  }
}
