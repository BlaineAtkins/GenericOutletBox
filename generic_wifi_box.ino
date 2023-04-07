/*
 * button press -- toggle relay
button hold -- start config portal

status LED:
green: connected to wifi
red: thinking/blocking -- temporarily trying to connect or reconnect to wifi
orange: config portal active
off: wifi inactive
blinking green/orange: config portal active and connected to wifi
 */
  // known issue -- if connected to wifi without internet, it will be blocking in the mqtt loop

//TODO: make button presses for less than 1-5 ms invalid
//re-initialize radio a few ms after each relay toggle
//add disclaimer to beginning about radio re-initialization and a way to disable that

//~~~~~~~WIRING~~~~~~~
/*
 * NRF24::ESP8266
 * CE::D4
 * SCK::D5
 * MISO::D6
 * CSN::D2
 * MOSI::D7
 * 
 * Green leg of LED to D8 with 470 ohm resistor
 * Red leg of LED to D3 with 470 ohm resistor
 * Relay to D0
 * Button between D1 and GND
 */

//~~~PARAMETERS TO SET~~~
boolean reinitializeRadioAfterRelayOperation=true; //Set this to true if your radio stops working until reboot after a few relay operations. Setting this true causes the relay to be re-initialized after each relay operation


#include <RF24.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h> //for mqtt

const char* mqtt_server = "broker.mqtt-dashboard.com";
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long delayTimer=0;
unsigned long configTimeoutTimer=0;
unsigned long ledBlinkTimer=0;
unsigned long buttonTimer=0;
unsigned long debounceTimer=0;
boolean ledBlinkState=true;
boolean relayState=false;
boolean configPortalActive=false;

RF24 radio(D4,D2); //CSN,CE D2,D4
const byte address[6] = "00011";

#define RELAY D0
#define GLED D8
#define RLED D3

WiFiManager wifiManager;

void setup() {
  // put your setup code here, to run once:
  pinMode(D1,INPUT_PULLUP);
  pinMode(RELAY,OUTPUT);
  pinMode(GLED,OUTPUT);
  pinMode(RLED,OUTPUT);
  Serial.begin(9600);

  Serial.println(radio.begin());
  radio.openReadingPipe(0, address);
  radio.setPALevel(RF24_PA_MAX);
  //radio.setPALevel(RF24_PA_MIN);
  radio.startListening();
  radio.setDataRate(RF24_250KBPS);
  radio.setAutoAck(true);
  
  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  wifiManager.setConfigPortalBlocking(false); //to allow radio communication even if we're not connected to wifi, while still running the portal in the bg for config
  //wifiManager.setConfigPortalTimeout(10); //doesn't appear to work in non-blocking mode
  statusLEDColor("red"); //turn red until we get to the loop -- not accepting radio or button input right now
  Serial.println("Will now try to connect to saved wifi network...");
  WiFi.begin(); //try to connect to saved network (blank credentials uses saved ones, put there by wifimanager)
  unsigned long beginTime=millis();
  while(WiFi.status()!=WL_CONNECTED){
    yield(); //prevent WDT reset
    if(millis()-beginTime>6000){
      break;
    }
  }
  if(WiFi.status()==WL_CONNECTED){ 
    Serial.println("Succesfully connected to saved network, config portal off");
  }else{
    Serial.println("failed to connect, starting config portal");
    wifiManager.startConfigPortal("Blaine Smart Outlet");
    configPortalActive=true;
  }
  


  client.setServer(mqtt_server, 1883);
  client.setCallback(Received_Message);
  
}

void loop() {
  wifiManager.process(); //to let wifimanager config portal run in the background

  handleStatusLED();

  if(configPortalActive && WiFi.softAPgetStationNum()>0){ //reset timeout of AP if client is connected to portal
    configTimeoutTimer=millis();
  }
  if(configPortalActive && millis()-configTimeoutTimer>1*60000){
    Serial.println("Config portal inactive, shutting off AP");
    WiFi.softAPdisconnect(true);
    configPortalActive=false;
  }


  if (radio.available()){
    char text[5] = "";
    radio.read(&text, sizeof(text));
    Serial.print("Radio command: ");
    Serial.println(text);
    if(text[0]=='t'){
      relayToggle("Radio",String(text));
    }
    if(text[0]=='0'){
      relayOff("Radio",String(text));
    }
    if(text[0]=='1'){
      relayOn("Radio",String(text));
    }
  }

  //HANDLE BUTTON: press = toggle. hold = start config portal
  if(!digitalRead(D1)){
    delay(10); //ignore button presses less than this duration in hopes that this will get rid of the ghost triggers
    if(!digitalRead(D1)){
      if(millis()-debounceTimer>500){
        relayToggle("Button","N/A");
      }
      debounceTimer=millis();
    }
  }else{ //reset button timer if it's not being held down
    buttonTimer = millis();
  }

  if(millis()-buttonTimer>2000){ //if button is held down
    if(!configPortalActive){
      Serial.println("starting config portal");
      wifiManager.startConfigPortal("Blaine Smart Outlet");
      configPortalActive=true;
      configTimeoutTimer=millis(); //if we don't reset this, the portal will immediately time out once it's turned on
    }
  }  
  
  if (!client.connected() && (WiFi.status()==WL_CONNECTED)) { //MQTT reconnect. Only try to reconnect if we're actually connected to wifi.
    reconnect(); //make this stop blocking!!
  }
  client.loop();

}

void Received_Message(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String messageString;
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    messageString+=(char)payload[i];
  }
  Serial.println();

  if((char)payload[0]=='0'){
    relayOff("MQTT",messageString);
  }else if((char)payload[0]=='1'){
    relayOn("MQTT",messageString);
  }else if((char)payload[0]=='t'){
    relayToggle("MQTT",messageString);
  }
}

void debugSend(String msg){
  char buf[70];
  msg.toCharArray(buf,msg.length()+1);
  client.publish("BlaineProjects/genericSmartOutlet1/debug",buf,true);
}

void relayOn(String source, String payload){
  digitalWrite(RELAY,HIGH);
  relayState=true;
  String msg = "Last command=On, Source= "+source+", Payload= "+payload;
  debugSend(msg);
  reinitializeRadio(15); //according to datasheet activation time is 10ms. Wait for at least this long to give relay time to kill radio if it's gonna
}
void relayOff(String source, String payload){
  digitalWrite(RELAY,LOW);
  relayState=false;
  String msg = "Last command=Off, Source= "+source+", Payload= "+payload;
  debugSend(msg);
  reinitializeRadio(10); //according to datasheet release time is 5ms. Wait for at least this long to give relay time to kill radio if it's gonna
}
void relayToggle(String source, String payload){
  if(relayState){
    relayOff("undefToggle","undefToggle");
  }else{
    relayOn("undefToggle","undefToggle");
  }
  String msg = "Last command=Toggle, Source= "+source+", Payload= "+payload;
  debugSend(msg);
}

void reinitializeRadio(int wait){
  if(reinitializeRadioAfterRelayOperation){
    delay(wait);
    radio.begin();
    radio.openReadingPipe(0, address);
    radio.setPALevel(RF24_PA_MAX);
    //radio.setPALevel(RF24_PA_MIN);
    radio.startListening();
    radio.setDataRate(RF24_250KBPS);
    radio.setAutoAck(true);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected() && (WiFi.status()==WL_CONNECTED)) { //WiFi.status doesn't change to disconnected immediately so this loop runs once if wifi is lost. But nbd cause that's just ~2 seconds of downtime for an edge case that should very rarely happen
    statusLEDColor("red"); //to indicate we're currently blocking as we try to reconnect
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    char* clientName="genericSmartOutlet1";
    if (client.connect(clientName)) { //name of this client
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("BlaineChelseyHeart/ChelseyRcv", "value"));
      // ... and resubscribe
      client.subscribe("BlaineProjects/genericSmartOutlet1/command");
      
    }else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in  seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void handleStatusLED(){
  if(WiFi.status()==WL_CONNECTED){
    if(configPortalActive){ //if we are connected to wifi but also running the config portal, blink green/orange
      if(millis()-ledBlinkTimer>900){
        if(ledBlinkState){
          statusLEDColor("orange");
          ledBlinkState=false;
        }else{
          statusLEDColor("green");
          ledBlinkState=true;
        }
        ledBlinkTimer=millis();
      }
    }else{
      statusLEDColor("green");
    }
  }else if(configPortalActive){      //}else if(WiFi.status()==7){ //returns 7 when in AP mode (I think. Documentation doesn't say anything about "7")
    statusLEDColor("orange");
  }else{ //not connected to wifi, nor in AP mode. Basically wifi is off.
    statusLEDColor("off");
    //Serial.println(WiFi.status());
  }
}

void statusLEDColor(String color){
  if(color=="orange"){
    digitalWrite(RLED,HIGH);
    analogWrite(GLED,5);
  }if(color=="green"){
    analogWrite(GLED,5);
    digitalWrite(RLED,0);
  }if(color=="red"){
    digitalWrite(RLED,HIGH);
    digitalWrite(GLED,LOW);
  }if(color=="off"){
    digitalWrite(RLED,LOW);
    digitalWrite(GLED,LOW);
  }
}
