#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

#define DATA_REQUEST_PIN D5
#define P1_TELEGRAM_SIZE 2048
#define P1_MAX_DATAGRAM_SIZE 2048
#define MQTT_USERNAME_LENGTH 32
#define MQTT_PASSWORD_LENGTH 32
#define MQTT_ID_TOKEN_LENGTH 64
#define MQTT_TOPIC_STRING_LENGTH 64
#define MQTT_REMOTE_HOST_LENGTH 128
#define MQTT_REMOTE_PORT_LENGTH 10
#define P1_BAUDRATE_LENGTH 10

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
WiFiClient wifiClient;
PubSubClient mqttClient("", 0, wifiClient); // Only with some dummy values seems to work ... instead of mqttClient();
char p1[P1_MAX_DATAGRAM_SIZE]; // Complete P1 telegram
APP_CONFIG_STRUCT app_config;
char mqtt_topic[128];

void saveConfigCallback () {
  Serial.println("(TODO): Should save config!");
}

void setup() {
  // Configure the serial port
  Serial.begin(115200); // open the port at 19200 baud
  Serial.println("\nP1 Extender setup...");

  // Configure the data request pin as input and enable the internal pull-up resistor.
  pinMode(DATA_REQUEST_PIN, INPUT_PULLUP);

  pinMode(D4, OUTPUT); // Problem, hardware pulls it low, so boot fails is pulled low!

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
  Serial1.begin(115200);//atol(app_config.p1_baudrate), SERIAL_8N1); // simulating the smart meter on port D4

  // Allow bootloader to connect: do not remove!
  delay(2000);
}

bool isDataRequest () {
  return digitalRead(DATA_REQUEST_PIN) == LOW; // If enabled, the pin will be low
}

void loop() {
  if ( isDataRequest() ) {
    Serial.println("Send serial");
    Serial1.print(R"(
/Ene5\T211 ESMR 5.0

1-3:0.2.8(50)
0-0:1.0.0(250126164506W)
0-0:96.1.1(4530303632303030303130363330373232)
1-0:1.8.1(000123.456*kWh)
1-0:1.8.2(001234.567*kWh)
1-0:2.8.1(001234.567*kWh)
1-0:2.8.2(001234.567*kWh)
0-0:96.14.0(0001)
1-0:1.7.0(01.234*kW)
1-0:2.7.0(00.000*kW)
0-0:96.7.21(00017)
0-0:96.7.9(00007)
1-0:99.97.0(2)(0-0:96.7.19)(230621102410S)(0000004757*s)(221007100958S)(0000024711*s)
1-0:32.32.0(00002)
1-0:52.32.0(00002)
1-0:72.32.0(00004)
1-0:32.36.0(00000)
1-0:52.36.0(00000)
1-0:72.36.0(00000)
0-0:96.13.0()
1-0:32.7.0(224.0*V)
1-0:52.7.0(225.0*V)
1-0:72.7.0(226.0*V)
1-0:31.7.0(001*A)
1-0:51.7.0(002*A)
1-0:71.7.0(003*A)
1-0:21.7.0(00.678*kW)
1-0:41.7.0(00.567*kW)
1-0:61.7.0(00.456*kW)
1-0:22.7.0(00.000*kW)
1-0:42.7.0(00.000*kW)
1-0:62.7.0(00.000*kW)
0-1:24.1.0(003)
0-1:96.1.0(4730303533303033363734313433343137)
0-1:24.2.1(250126164500W)(012345.678*m3)
!51D3
)");
  } else {
    Serial.println("Data request not enabled!");
  }
  delay(5000);
}
