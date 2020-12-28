/*
Winvane 

    * Author :             F. Guiet 
    * Creation           : 20201224
    * Last modification  : 
    * History            : 1.0 - First version
                                                                 
    * Tips ! :
      - Change baudrate in PlatformIO : Ctrl+T b 115200    

    * References:    
    
    * Power consumption :    
      - 150 mA when Led and phototransistor are ON
      - 66 mA when ESP8266 is on and led and phototransistor are OFF
      - 140 uA when in deep sleep
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <secret.h>
#include <ESP8266WiFi.h> 
#include <PubSubClient.h>

#define DEBUG 0
#define MAX_RETRY 500
#define MQTT_CLIENT_ID "WindvaneSensor"
#define FIRMWARE_VERSION "1.0"

const int PIN_LINE1 = 13;
const int PIN_LINE2 = 12;
const int PIN_LINE3 = 14;
const int PIN_LINE4 = 2;
const int PIN_LINE5 = 4;
const int SAMPLES = 5;
const int SAMPLES_FREQUENCY = 2000;  //10s = 10000
//const int DEEP_SLEEP_S = 900;  //900s = 15min
const int DEEP_SLEEP_S = 900;  //900s = 15min

const int PIN_PHOTOTRANSISTOR_LED_ONOFF = 5;

const int ANALOG_PIN = A0;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

String directions[] = { "North", "North-East", "East" ,"South-East", "South", "South-West", "West", "North-West"};

unsigned long last_millis = 0;
int counter = 0;
DynamicJsonDocument doc(500);
JsonObject winddir = doc.to<JsonObject>();

struct Sensor {
    String Name;    
    String SensorId;
    String Mqtt_topic;
};

#define SENSORS_COUNT 1
Sensor sensors[SENSORS_COUNT];

float getDegree(String direction) {
  
  if (direction=="South") {
    return 180;
  }  

  if (direction=="South-East") {
    return 135;
  }

  if (direction=="East") {
    return 90;
  }

  if (direction=="North-East") {
    return 45;
  }

  if (direction=="North") {
    return 0;
  }

  if (direction=="North-West") {
    return 315;
  }

  if (direction=="West") {
    return 270;
  }

  if (direction=="South-West") {
    return 225;
  }

  return -1;
}

void debug_message(String message, bool doReturnLine) {
  if (DEBUG) {
    if (doReturnLine) { 
      Serial.println(message);
    }
    else {      
      Serial.print(message);
    }

    //Be sure everything is printed on screen!
    Serial.flush();
  }
}

boolean connectToMqtt() {

  client.setServer(MQTT_SERVER, 1883); 

  int retry = 0;
  // Loop until we're reconnected
  while (!client.connected() && retry < MAX_RETRY) {
    debug_message(F("Attempting MQTT connection..."), true);
    
    if (client.connect(MQTT_CLIENT_ID)) {
      debug_message(F("connected to MQTT Broker..."), true);
    } else {
      retry++;
      // Wait 5 seconds before retrying
      delay(500);      
    }
  }

  if (retry >= MAX_RETRY) {
    debug_message(F("MQTT connection failed..."), true);      
    return false;
  }

  return true;
}

boolean connectToWifi() {
  
  debug_message(F("Connecting to WiFi..."), true);
  
  int retry = 0;

  WiFi.hostname(MQTT_CLIENT_ID);

  WiFi.begin(ssid, password);  
  
  while (WiFi.status() != WL_CONNECTED && retry < MAX_RETRY) {
    retry++;
    delay(500);
    debug_message(F("."), false);
  }

  if (WiFi.status() == WL_CONNECTED) {  
     debug_message(F("WiFi connected"), true);  
     
     // Print the IP address
     if (DEBUG) {
      Serial.println(WiFi.localIP());
     }
     
     return true;
  } else {
    debug_message(F("WiFi connection failed..."), true);   
    return false;
  }  
}

void burn8Readings(int pin)
{
  for (int i = 0; i < 8; i++)
  {
    analogRead(pin);
    delay(2);
  }
}

float ReadVoltage() {
  
  burn8Readings(ANALOG_PIN);                            // make 8 readings but don't use them
  delay(10);                                            // idle some time
  unsigned int sensorValue = analogRead(ANALOG_PIN);    // read actual value

  //with R1 = 51kOhm, R2 = 15kOhm
  //Max voltage 4.2v of fully charged battery produces = 0.955v on A0
  //Input voltage range of bare ESP8266 is 0 — 1.0V
  //According to voltage divider formula : Vout = Vin * (R2 / (R1 + R2))

  //So 4.2v is roughly represented by 1023 value on A0

  //This live experience shows 1013 for full charged lithium battery (4.2v)
  //So 4.2v is roughly represented by 1013 value on A0 (we are closed to the theoric 1023 :))

  float voltage = (sensorValue * 4.2) / 1013;

  if (DEBUG) {
    Serial.println(sensorValue);
    Serial.println("Battery voltage is : " + String(voltage,2));
  }

  return voltage;
}

void InitSensors() {
  
  sensors[0].Name = "Windvane";
  sensors[0].SensorId = "21";
  sensors[0].Mqtt_topic = "guiet/outside/sensor/21";
}

void setup() {
  Serial.begin(9600);

  //Turn on / off (led and phototransistor)
  pinMode(PIN_PHOTOTRANSISTOR_LED_ONOFF, OUTPUT);
  digitalWrite(PIN_PHOTOTRANSISTOR_LED_ONOFF, LOW);

  pinMode(PIN_LINE1, INPUT); //Line 1
  pinMode(PIN_LINE2, INPUT); //Line 2
  pinMode(PIN_LINE3, INPUT); //Line 3
  pinMode(PIN_LINE4, INPUT); //Line 4
  pinMode(PIN_LINE5, INPUT); //Line 5  

  InitSensors();

  debug_message(F("Setup completed successfully..."), true);
}

void disconnectMqtt() {
  debug_message("Disconnecting from mqtt...", true);
  client.disconnect();
}

void disconnectWifi() {
  debug_message("Disconnecting from wifi...", true);
  WiFi.disconnect();
}

String getDirection(int position) {

  if (position==15 || position==5 || position==3) return directions[0]; //North
  if (position==1 || position==17 || position==19) return directions[1]; //North-East
  if (position==23 || position==22 || position==18) return directions[2]; //East
  if (position==16 || position==24 || position==27) return directions[3]; //South-East
  if (position==9 || position==8 || position==12) return directions[4]; //South
  if (position==29 || position==21) return directions[5]; //South-West
  if (position==20 || position==6 || position==4 || position==30 || position==14) return directions[6]; //West
  if (position==26 || position==10 || position==2) return directions[7]; //North-West  

  return "Unknown";
}

int getPosition() {

  debug_message(F("Turn leds and phototransistors ON..."), true);     
  digitalWrite(PIN_PHOTOTRANSISTOR_LED_ONOFF, HIGH);
  delay(500);

  int val1 = digitalRead(PIN_LINE1);
  int val2 = digitalRead(PIN_LINE2);
  int val3 = digitalRead(PIN_LINE3);
  int val4 = digitalRead(PIN_LINE4);
  int val5 = digitalRead(PIN_LINE5);
  
  int position =  (val5 * 16) + (val4 * 8) + (val3 * 4) + (val2 * 2) + val1;

  if (DEBUG) {
    Serial.print(val1); 
    Serial.print(val2); 
    Serial.print(val3); 
    Serial.print(val4); 
    Serial.print(val5);
    Serial.print(" => ");
    Serial.println(position);
  }

  debug_message(F("Turn leds and phototransistors OFF..."), true);   
  digitalWrite(PIN_PHOTOTRANSISTOR_LED_ONOFF, LOW);

  return position;
}

String convertToJSon(String battery, String dirWin) {
    //Create JSon object
    DynamicJsonDocument  jsonBuffer(200);
    JsonObject root = jsonBuffer.to<JsonObject>();
    
    root["id"] = sensors[0].SensorId;
    root["name"] = sensors[0].Name;
    root["firmware"]  = FIRMWARE_VERSION;
    root["battery"] = battery;    
    root["winddirection"] = dirWin;
    root["degree"] = String(getDegree(dirWin),1);
    
    String result;    
    serializeJson(root, result);

    return result;
}

void sendResult() {

  if (WiFi.status() != WL_CONNECTED) {
    if (!connectToWifi())
      return;
  }  

  if (!client.connected()) {
    if (!connectToMqtt()) {
      return;
    }
  }

  int winner = 0;
  String dirWin = "Unknown";
  for (int i=0;i<8;i++) {
    if (winddir.containsKey(directions[i])) {
      if (winddir[directions[i]].as<int>() > winner) {
        dirWin=directions[i];
        winner=winddir[directions[i]].as<int>();
      }
    }    
  }

  debug_message("Winner is : " + dirWin, true);

  char message_buff[200];
  float vin = ReadVoltage();
  String mess = convertToJSon(String(vin,2), dirWin);
  debug_message("JSON Sensor : " + mess + ", topic : " +sensors[0].Mqtt_topic, true);
  mess.toCharArray(message_buff, mess.length()+1);
    
  client.publish(sensors[0].Mqtt_topic.c_str(),message_buff);

  disconnectMqtt();
  delay(100);
  disconnectWifi();
  delay(100);
}

void loop() {

  //Get Wind direction every SAMPLES_FREQUENCY ms
  if (millis() - last_millis >= SAMPLES_FREQUENCY ) { 

    debug_message("Reading number : " + String(counter), true);  

    int position = getPosition();
    String windDirection = getDirection(position);

    if (DEBUG)
      Serial.println("Sens du vent : " + windDirection);
        
    counter++;

    if (winddir.containsKey(windDirection)) {
      winddir[windDirection]=winddir[windDirection].as<int>()+1;
    }
    else {
      winddir[windDirection]=1;
    }    

    last_millis = millis();
  }

  //For testing purpose
  //return;

  if (counter >= SAMPLES) {

    sendResult();

    debug_message("Going to sleep...na night", true);

    ESP.deepSleep(DEEP_SLEEP_S * 1000000); //900s = 15 min (Deep sleep)
  }
}