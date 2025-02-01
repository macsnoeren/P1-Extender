/*
  - How to get the Wemos installed in the Ardiuno IDE: https://siytek.com/wemos-d1-mini-arduino-wifi/
  - Install library WiFiManager by tablatronics: https://github.com/tzapu/WiFiManager
  - Install library JsonArduino by Banoit Blanchon: https://arduinojson.org/?utm_source=meta&utm_medium=library.properties
  - Install library knolleary/PubSubClient by Nick O'Leary: https://github.com/knolleary/pubsubclient
*/
#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define DATA_REQUEST_PIN D5
#define SERIAL_SEND_ENABLE_PIN D1
#define FACTORY_RESET_PIN D6

#define P1_TELEGRAM_SIZE 2048
#define P1_MAX_DATAGRAM_SIZE 2048
#define MQTT_USERNAME_LENGTH 32
#define MQTT_PASSWORD_LENGTH 32
#define MQTT_ID_TOKEN_LENGTH 64
#define MQTT_TOPIC_STRING_LENGTH 64
#define MQTT_REMOTE_HOST_LENGTH 128
#define MQTT_REMOTE_PORT_LENGTH 10
#define P1_BAUDRATE_LENGTH 10

#define MQTT_USERNAME "smartmeter"
#define MQTT_PASSWORD "se_smartmeter"
#define MQTT_REMOTE_HOST "sendlab.nl"
#define MQTT_REMOTE_PORT "11883"
#define MQTT_TOPIC "smartmeter/raw"
#define MQTT_MSGBUF_SIZE 2048

// Create struct for application config.
typedef struct {
   char     mqtt_username[MQTT_USERNAME_LENGTH];
   char     mqtt_password[MQTT_PASSWORD_LENGTH];
   char     mqtt_id[MQTT_ID_TOKEN_LENGTH];
   char     mqtt_topic[MQTT_TOPIC_STRING_LENGTH];
   char     mqtt_remote_host[MQTT_REMOTE_HOST_LENGTH];
   char     mqtt_remote_port[MQTT_REMOTE_PORT_LENGTH];
   char     p1_baudrate[P1_BAUDRATE_LENGTH];
} APP_CONFIG_STRUCT;

// Global variables
WiFiManager wifiManager;
bool shouldSaveConfig = false;
WiFiClient wifiClient;
WiFiClient tcpServerClient;
PubSubClient mqttClient("", 0, wifiClient); // Only with some dummy values seems to work ... instead of mqttClient();
char p1[P1_MAX_DATAGRAM_SIZE]; // Complete P1 telegram
uint8_t p1State = 0;
uint16_t p1Pointer = 0;
uint32_t p1Timer = 0;
APP_CONFIG_STRUCT app_config;
char mqtt_topic[128];

bool deleteAppConfig() {
  if( LittleFS.begin() ) {
    if( LittleFS.exists("/config.json") ) {
      if( LittleFS.remove("/config.json") ) {
        return true;
      }
    }
  } 
  return false;
}

void create_unique_mqtt_topic_string (char *topic_string) {
   char tmp[30];
   strcpy(topic_string,"P1EXT-V01");
   sprintf(tmp,"-%06X",ESP.getChipId());
   strcat(topic_string,tmp);
   sprintf(tmp,"-%06X",ESP.getFlashChipId()); 
   strcat(topic_string,tmp);
}

void create_unigue_mqtt_id (char *signature) {
   char tmp[30];
   strcpy(signature,"2025-P1-EXT");
   strcat(signature,"-V01");
   sprintf(tmp,"-%06X",ESP.getChipId());
   strcat(signature,tmp);
   sprintf(tmp,"-%06X",ESP.getFlashChipId()); 
   strcat(signature,tmp);
}

void factoryDefault () {
  wifiManager.resetSettings();
  deleteAppConfig();
  delay(2000);
  ESP.reset();
}

bool readAppConfig(APP_CONFIG_STRUCT *app_config) {
  if( LittleFS.begin() ) {
    if( LittleFS.exists("/config.json") ) {
       File configFile = LittleFS.open("/config.json","r");
       if( configFile ) {

          size_t size = configFile.size();
          if (size > 1024) {
            Serial.println("Config file size is too large");
          }

          std::unique_ptr<char[]> buf(new char[size]);
          configFile.readBytes(buf.get(), size);
        
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, buf.get());
          
          if( error == DeserializationError::Ok ) {
             strcpy(app_config->mqtt_username, doc["MQTT_USERNAME"]);
             strcpy(app_config->mqtt_password, doc["MQTT_PASSWORD"]);
             strcpy(app_config->mqtt_remote_host, doc["MQTT_HOST"]);
             strcpy(app_config->mqtt_remote_port, doc["MQTT_PORT"]);
             strcpy(app_config->p1_baudrate, doc["P1_BAUDRATE"]);
             return true;
          }
       }
    }
  }  
  return false;
}

bool writeAppConfig(APP_CONFIG_STRUCT *app_config) {
  deleteAppConfig(); // Delete config file if exists

  JsonDocument doc;
  doc["MQTT_USERNAME"] = app_config->mqtt_username;
  doc["MQTT_PASSWORD"] = app_config->mqtt_password;
  doc["MQTT_HOST"] = app_config->mqtt_remote_host;
  doc["MQTT_PORT"] = app_config->mqtt_remote_port;
  doc["P1_BAUDRATE"]= app_config->p1_baudrate;
  
  File configFile = LittleFS.open("/config.json","w+");
  if( configFile ) {
     serializeJson(doc, configFile);
     configFile.close();
     return true;
  }    
  return false;
}

void saveConfigCallback () {
  shouldSaveConfig = true;
}

void setup() {
  // Configure the serial port
  Serial.begin(115200); // open the port at 19200 baud
  Serial.println("\nP1 Extender setup...");

  // Configure the data request pin as input and enable the internal pull-up resistor.
  pinMode(D4, OUTPUT); // Serial 1 output
  pinMode(DATA_REQUEST_PIN, INPUT_PULLUP);
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  pinMode(SERIAL_SEND_ENABLE_PIN, OUTPUT);
  digitalWrite(SERIAL_SEND_ENABLE_PIN, LOW); // Enable the serial send.

  if ( digitalRead(FACTORY_RESET_PIN) == LOW ) { // If pull low at start-up, start the factory default functionality
  Serial.println("Go to factory defaults!!");
    factoryDefault();
  }

  // Setup unique mqtt id and mqtt topic string
  create_unique_mqtt_topic_string(app_config.mqtt_topic);
  create_unigue_mqtt_id(app_config.mqtt_id);
  sprintf(mqtt_topic, MQTT_TOPIC);

  // Read config file or generate default
  if( !readAppConfig(&app_config) ) {
    strcpy(app_config.mqtt_username, MQTT_USERNAME);
    strcpy(app_config.mqtt_password, MQTT_PASSWORD);
    strcpy(app_config.mqtt_remote_host, MQTT_REMOTE_HOST);
    strcpy(app_config.mqtt_remote_port, MQTT_REMOTE_PORT);
    strcpy(app_config.p1_baudrate, "115200");
    writeAppConfig(&app_config);
  }

  wifiManager.setMinimumSignalQuality(20);
  wifiManager.setTimeout(300);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Adds some parameters to the default webpage
  WiFiManagerParameter wmp_text("<br/>MQTT setting:</br>");
  wifiManager.addParameter(&wmp_text);
  WiFiManagerParameter custom_mqtt_username("mqtt_username", "Username", app_config.mqtt_username, MQTT_USERNAME_LENGTH);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "Password", app_config.mqtt_password, MQTT_PASSWORD_LENGTH);
  WiFiManagerParameter custom_mqtt_remote_host("mqtt_remote_host", "Host", app_config.mqtt_remote_host, MQTT_REMOTE_HOST_LENGTH);
  WiFiManagerParameter custom_mqtt_remote_port("mqtt_remote_port", "Port", app_config.mqtt_remote_port, MQTT_REMOTE_PORT_LENGTH);
  WiFiManagerParameter custom_p1_baudrate("p1_baudrate", "Baudrate", app_config.p1_baudrate, P1_BAUDRATE_LENGTH);

  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_remote_host);
  wifiManager.addParameter(&custom_mqtt_remote_port);
  wifiManager.addParameter(&custom_p1_baudrate);

  // Add the unit ID to the webpage
  char fd_str[128]="<p>Your P1 Extender ID: <b>";
  strcat(fd_str, app_config.mqtt_topic);
  strcat(fd_str, "</b> Make a SCREENSHOT - you will need this info later!</p>");
  WiFiManagerParameter mqqt_topic_text(fd_str);
  wifiManager.addParameter(&mqqt_topic_text);

  if( !wifiManager.autoConnect("P1 Extender")) {
    delay(1000);
    ESP.reset();
  }

  if ( shouldSaveConfig ) {
    strcpy(app_config.mqtt_username, custom_mqtt_username.getValue());
    strcpy(app_config.mqtt_password, custom_mqtt_password.getValue());
    strcpy(app_config.mqtt_remote_host, custom_mqtt_remote_host.getValue());
    strcpy(app_config.mqtt_remote_port, custom_mqtt_remote_port.getValue());
    strcpy(app_config.p1_baudrate, custom_p1_baudrate.getValue());
    writeAppConfig(&app_config);
  }

  Serial.printf("\n");
  Serial.printf("************ P1 Extender ********************\n");
  Serial.printf("ESP8266 info\n");
  Serial.printf("\tSDK Version     : %s\n", ESP.getSdkVersion() );
  Serial.printf("\tCore Version    : %s\n", ESP.getCoreVersion().c_str() );
  Serial.printf("\tCore Frequency  : %d Mhz\n", ESP.getCpuFreqMHz());
  Serial.printf("\tLast reset      : %s\n", ESP.getResetReason().c_str() );

  Serial.printf("MQTT settings\n");
  Serial.printf("\tmqtt_username   : %s\n", app_config.mqtt_username);
  Serial.printf("\tmqtt_password   : %s\n", app_config.mqtt_password);
  Serial.printf("\tmqtt_id         : %s\n", app_config.mqtt_id);
  Serial.printf("\tmqtt_topic      : %s\n", mqtt_topic);
  Serial.printf("\tmqtt_remote_host: %s\n", app_config.mqtt_remote_host);
  Serial.printf("\tmqtt_remote_port: %s\n", app_config.mqtt_remote_port);

  Serial.printf("DSMR settings\n");
  Serial.printf("\tP1 Baudrate     : %s baud\n", app_config.p1_baudrate);
  Serial.printf("************ P1 Extender ********************\n");

  // Configure the second (half) serial port
  // https://www.robmiles.com/journal/2021/1/29/using-the-second-serial-port-on-the-esp8266
  Serial1.begin(atol(app_config.p1_baudrate), SERIAL_8N1); // simulating the smart meter on port D4

  // Allow bootloader to connect: do not remove!
  delay(2000);

  // Set p1 timer timeout
  p1Timer = millis();
}

bool isDataRequest () {
  return digitalRead(DATA_REQUEST_PIN) == LOW; // If enabled, the pin will be low
}

void mqtt_callback (char* topic, byte* payload, unsigned int length) {
  Serial.println("mqtt_callback: not implemented!");
}

void mqtt_connect () {
  if ( !mqttClient.connected() ) {
    char *host = app_config.mqtt_remote_host;
    int port = atoi(app_config.mqtt_remote_port);
    
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(host, port );
    mqttClient.setBufferSize(MQTT_MSGBUF_SIZE);
    if ( mqttClient.connect(app_config.mqtt_id, app_config.mqtt_username, app_config.mqtt_password) ) {

      // Subscribe to mqtt topic
      mqttClient.subscribe(mqtt_topic);

      // Set callback
      mqttClient.setCallback(mqtt_callback);
      Serial.printf("%s: MQTT connected to %s:%d\n", __FUNCTION__, host, port);

    } else {
      Serial.printf("%s: MQTT connection ERROR (%s:%d)\n", __FUNCTION__, host, port);
    }
  }
}

bool captureP1 () {
  if ( tcpServerClient.connected() && tcpServerClient.available() ) {
    while ( tcpServerClient.available() ) {
      char c = tcpServerClient.read();
      switch(p1State) {
        case 0: // state start
          if ( c == '/' ) {
            p1[p1Pointer] = c;
            p1Pointer++;
            p1State = 1;
          }
          break;
        case 1: // state read message
          p1[p1Pointer] = c;
          p1Pointer++;
          if ( c == '!' ) {
            p1State = 2;
          }
          break;
        case 2: // End
          p1[p1Pointer] = c;
          p1Pointer++;
          if ( c == '\n' ) {
            p1[p1Pointer] = '\0';
            p1Pointer = 0;
            p1State = 0;
            return true;
          }
          break;
      }
      if ( p1Pointer >= P1_MAX_DATAGRAM_SIZE ) {
        Serial.println("captureP1: Buffer overflow detected.");
        p1Pointer = 0;
        p1State = 0;
        tcpServerClient.flush();
        return false;
      }
    }
  }

  return false;
}

void loop () {
  if( WiFi.status() == WL_CONNECTED) {
    /*if( !mqttClient.connected() ) {
      Serial.printf("State: %d\n", mqttClient.state());
      mqtt_connect();
      delay(1000);
    }*/
  }

  // Handel the MQTT
  //mqttClient.loop();
  
  // Connect to the p1 data server
  if ( !tcpServerClient.connected() ) {
    delay(1000);
//  if ( tcpServerClient.connect("192.168.2.80", 3141) ) {
    if ( tcpServerClient.connect("diy_smartmeter.local", 3141) ) {
      char message[100];
      size_t len = tcpServerClient.readBytesUntil('\n', message, 100); // Read the first message 'Smartmeter'
      message[len] = '\0';
      Serial.printf("wifiClient: Connected: '%s'\n", message);

    } else {
      Serial.println("wifiClient: Could not establish the connection.");
    }
    p1Timer = millis(); // No timer required here!
  }

  // Get the p1 message
  if ( captureP1() ) {
    //Serial.printf("P1: '%s'\n", p1);
    if ( isDataRequest() ) {
      Serial.println("Recieved P1 and forward P1");
      Serial1.print(p1);
    } else {
      Serial.println("Recieved P1");
    }
    p1Timer = millis();
  }

  // Check p1 timeout
  if ( millis() > p1Timer + 10000 ) {
    Serial.println("P1: Timeout...");
    tcpServerClient.flush();
    tcpServerClient.stop();
    p1Timer = millis();
  }

}
