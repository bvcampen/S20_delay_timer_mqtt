/*
 S20 sonoff switch mod.
 short push sets relay on and add to delay off timer
 long push with relay off sets relay on without any delay to off.
 long push with relay on sets relay off and clears delay timer.

 Compile options for dev board or S20 target
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ladywood.h>
#include <LedFlasher.h>
#include <PressButton.h>

#define S20 true

#ifdef S20

#define LED_PIN 13
#define RELAY_PIN 12
#define BUTTON_PIN 0
#define LED_IS_ACTIVE_LOW true
#define RELAY_IS_ACTIVE_LOW false
#define BUTTON_IS_ACTIVE_LOW true

#else

#define LED_PIN 13
#define RELAY_PIN BUILTIN_LED    // GPIO 2 / D4
#define BUTTON_PIN = 14;       // GPIO14 / D5
#define LED_IS_ACTIVE_LOW    1     // active ? on S20
#define RELAY_IS_ACTIVE_LOW  true  // active ? on S20 
#define BUTTON_IS_ACTIVE_LOW true

#endif

// time constants (milliseconds)
#define DELAY_TO_OFF_INCREMENT 1000 * 60 * 10 // 10 minutes

#define LED_FLASH_SLOW led.set(500,500);
#define LED_FLASH_OFF led.set(0,0);

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];
long turnOffTime = 0;

union ipv4_t {
  uint8_t bytes[4];
  uint32_t dword;
};

long lastReconnectAttempt = 0;
char clientId [8];
char relayTopicIn [16];
char relayTopicOut [16];

// the led object
LedFlasher led(LED_PIN, LED_IS_ACTIVE_LOW, 0, 0);

// the button object
PressButton button(BUTTON_PIN, BUTTON_IS_ACTIVE_LOW);

// Changes relay state with MQTT messaging
void setRelayWithMessage(int reqState) {
  #ifdef DEBUG
  Serial.print("Relay ");
  Serial.print(reqState ? "on" : "off");
  Serial.print("\n");
  #endif
  digitalWrite(RELAY_PIN, reqState ^ RELAY_IS_ACTIVE_LOW);     
  client.publish(relayTopicOut, reqState ? "on" : "off");
  LED_FLASH_OFF;
}

// EVENT FUNCTIONS

void eventButtonShortPress() {
  if (digitalRead(RELAY_PIN) ^ RELAY_IS_ACTIVE_LOW == OFF) {
    // relay was off, set to on and set off timer
    turnOffTime = millis() + DELAY_TO_OFF_INCREMENT;
    setRelayWithMessage(ON);
  }
  else {
    // increment off timer
    turnOffTime += DELAY_TO_OFF_INCREMENT;
  }
  LED_FLASH_SLOW;
}

void eventButtonLongPress() {
  if (digitalRead(RELAY_PIN) ^ RELAY_IS_ACTIVE_LOW == ON) {
    // relay is on
    setRelayWithMessage(OFF);
  }
  else {
    // relay is off
    setRelayWithMessage(ON);
  }
  LED_FLASH_OFF;
  turnOffTime = 0;
}

void messageArrived(char* topic, byte* _payload, unsigned int length) {
  #ifdef DEBUG  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] message:");
  #endif

  // convert payload to printable string in lower case
  String payload;
  for (int i = 0; i < length; i++) {
    if (_payload[i] > 0x7F || _payload[i] < 0x20)
      break;
    payload += char( _payload[i] >= 0x41 && _payload[i] < 0x5A ? _payload[i] + 0x20 : _payload[i] );
  }
  #ifdef DEBUG  
  Serial.println(payload);
  #endif

  if (strcmp(topic, relayTopicIn) == 0) {
    if (payload.indexOf("on") == 0) {
      setRelayWithMessage(1);
    }
    else if (payload.indexOf("off") == 0) {
      setRelayWithMessage(0);
    }
  }
}

// INITIALISATION FUNCTIONS

void setup() {
  //set the mode of the pins...
  pinMode(BUTTON_PIN, INPUT);

  digitalWrite(LED_PIN, OFF ^ LED_IS_ACTIVE_LOW);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, OFF ^ RELAY_IS_ACTIVE_LOW);
  pinMode(RELAY_PIN, OUTPUT);

  #ifdef DEBUG
  Serial.begin(115200);
  #endif

  button.registerShortPressHandler(eventButtonShortPress);
  button.registerLongPressHandler(eventButtonLongPress);

  delay(10);
  // We start by connecting to a WiFi network
  #ifdef DEBUG
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  #endif
  
  WiFi.begin(ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    #ifdef DEBUG    
    Serial.print(".");
    #endif
  }

  IPAddress ipAddr = WiFi.localIP();
  #ifdef DEBUG
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(ipAddr);
  #endif

  // The host address will define our device id and topics
  IPAddress ipSubnet = WiFi.subnetMask();

  // prepares clientID and topic strings based on allocated IP. Called once from main
  ipv4_t hostBytes;
  hostBytes.dword = (~ ipSubnet) & ipAddr;

  // move hostBytes to hostId as a little endian
  uint32_t hostId = 0;
  for (int i = 0; i < 4; ++i)
  {
    hostId = hostId << 8;
    hostId += hostBytes.bytes[i];
  }
  // now prepare the global clientId string based on host IP address bits
  sprintf(clientId, "d%d", hostId);

  strcpy(relayTopicIn, clientId);
  strcat(relayTopicIn, "/relay/in");
  strcpy(relayTopicOut, clientId);
  strcat(relayTopicOut, "/relay/out");

  #ifdef DEBUG
  Serial.print("MQTT client ID: ");
  Serial.println(clientId);
  #endif

  client.setServer(mqtt_server, 1883);
  client.setCallback(messageArrived);
}

boolean reconnect() {
  if (client.connect(clientId, mqtt_user, mqtt_password)) {
    // Once connected, publish an announcement...
    client.publish(relayTopicOut,"connected");
    // ... and resubscribe
    client.subscribe(relayTopicIn);
  }
  return client.connected();
}

void loop() {
  led.run();
  button.run();
  
  // Try to stay connected to MQTT server
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      #ifdef DEBUG
      Serial.println("client connect attempt");
      #endif
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
        // message the current relay state
        client.publish(relayTopicOut, digitalRead(RELAY_PIN) ^ RELAY_IS_ACTIVE_LOW ? "on" : "off");
        #ifdef DEBUG
        Serial.println("connected to MQTT");
        #endif
      }
    }
  } else {
    // This client is connected
    client.loop();
  }

  // process delay to off timer
  if (turnOffTime && millis() > turnOffTime) {
    // timer expired
    turnOffTime = 0;
    LED_FLASH_OFF;
    setRelayWithMessage(0);
  }
  
}


