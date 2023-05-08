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
 * 
 * ViOut of ACS720 current sensor to A0. (ACS720 is powered by +5v)
 */

//PARAMETERS SET BY CONFIG PORTAL
bool reinitializeRadioAfterRelayOperation=false;
bool turnOffAtSpecifiedTime=false;
int turnOffTime=0; //time in hours (eg: 13 is 1:00pm)
bool turnOnAtSpecifiedTime=false;
int turnOnTime=0;
int GMTTimezone=0;
byte radioAddress[6] = "00000"; //Address for radio to listen on. NOTE: if you want multiple radios to listen on the same address, you must disable autoack and code your own retry function. b&d room is 00011. porch lights is 00111, living room lighst are 00001
char mqtt_server[50] = "";
char MQTTTopic[50] = "";
char debugTopic[50] = "";


#include <RF24.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h> //for mqtt
#include <WiFiUdp.h> //to send serial-monitor-esque messages
#include <NTPClient.h>
#include <EEPROM.h>
WiFiUDP Udp;

int maxAmpSteps=0;

float avgCurrent[10] = {4.5,4.5,4.5,4.5,4.5,4.5,4.5,4.5,4.5,4.5}; //initialize to 4.5 amps so if overcurrent is immediate on boot it doesn't take forever to exceed the average dominated by 0s
int avgCurrentPtr=0;

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

unsigned long calcAmperageTimer=0;
unsigned long analogReadTimer=0;

unsigned long ntpCheckTimer=60000; //set to a large value so check is run at boot, and is immediately negated in case it's plugged in during the hour that it should otherwise switch off

//to turn on/off at a certain time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
bool timerTurnedOffToday=false;
bool timerTurnedOnToday=false;

RF24 radio(D4,D2); //CSN,CE D2,D4
//radio address is at top in settable parameters

#define RELAY D0
#define GLED D8
#define RLED D3

WiFiManager wifiManager;
//parameters declared here for global, then updated in setup() with correct values
WiFiManagerParameter param_radioReInit("paramID_radioReInit", "Reinitialize Radio After Relay Operation (set true if radio stops working after some time)", "TEMP", 10);
WiFiManagerParameter param_turnOffAtSpecifiedTime("paramID_turnOffAtSpecifiedTime", "Turn Off Outlet At Time (set true if you want this outlet to turn off automatically at a certain time", "TEMP", 10);
WiFiManagerParameter param_turnOffTime("paramID_turnOffTime", "Turn Off Outlet At... (time in hours to turn off at. ex: 13 is 1:00pm)", "TEMP", 10);
WiFiManagerParameter param_turnOnAtSpecifiedTime("paramID_turnOnAtSpecifiedTime", "Turn On Outlet At Time (set true if you want this outlet to turn on automatically at a certain time", "TEMP", 10);
WiFiManagerParameter param_turnOnTime("paramID_turnOnTime", "Turn On Outlet At... (time in hours to turn on at. ex: 13 is 1:00pm", "TEMP", 10);
WiFiManagerParameter param_GMTTimezone("paramID_GMTTimezone", "GMT Timezone (ex: put \"-7\" for -7 GMT", "TEMP", 10);
WiFiManagerParameter param_radioAddress("paramID_radioAddress", "Radio byte address", "TEMP", 10);
WiFiManagerParameter param_mqtt_server("paramID_mqtt_server", "MQTT Server", "TEMP", 10);
WiFiManagerParameter param_mqtt_topic("paramID_mqtt_topic", "MQTT command topic (box receives commands from here)", "TEMP", 10);
WiFiManagerParameter param_mqtt_debugTopic("paramID_mqtt_debugTopic", "MQTT debug topic (box sends debug messages here)", "TEMP", 10);


void saveConfigParams(){

  //first, get values input from web portal and put them in char arrays
  String tempVal = param_radioReInit.getValue();
  char ch_radioReInit[6];
  tempVal.toCharArray(ch_radioReInit,6);

  tempVal = param_turnOffAtSpecifiedTime.getValue();
  char ch_turnOffAtSpecifiedTime[6];
  tempVal.toCharArray(ch_turnOffAtSpecifiedTime,6);

  tempVal = param_turnOffTime.getValue();
  char ch_turnOffTime[3];
  tempVal.toCharArray(ch_turnOffTime,3);

  tempVal = param_turnOnAtSpecifiedTime.getValue();
  char ch_turnOnAtSpecifiedTime[6];
  tempVal.toCharArray(ch_turnOnAtSpecifiedTime,6);

  tempVal = param_turnOnTime.getValue();
  char ch_turnOnTime[3];
  tempVal.toCharArray(ch_turnOnTime,3);

  tempVal = param_GMTTimezone.getValue();
  char ch_GMTTimezone[4];
  tempVal.toCharArray(ch_GMTTimezone,4);

  tempVal = param_radioAddress.getValue();
  char ch_radioAddress[6];
  tempVal.toCharArray(ch_radioAddress,6);

  tempVal = param_mqtt_server.getValue();
  char ch_mqtt_server[51];
  tempVal.toCharArray(ch_mqtt_server,51);

  tempVal = param_mqtt_topic.getValue();
  char ch_mqtt_topic[51];
  tempVal.toCharArray(ch_mqtt_topic,51);

  tempVal = param_mqtt_debugTopic.getValue();
  char ch_mqtt_debugTopic[51];
  tempVal.toCharArray(ch_mqtt_debugTopic,51);


  //do some validation before saving them to eeprom so we only save parameters in a valid form (only necessary for some)
  if(strcmp(ch_radioReInit,"true")!=0 && strcmp(ch_radioReInit,"false")!=0){ //set to false if user entered something other than true or false
    reinitializeRadioAfterRelayOperation=false;
    strcpy(ch_radioReInit,"false");
  }
  param_radioReInit.setValue(ch_radioReInit,5); //update parameter so if user stays in config portal, it will show new value without a reboot

  if(strcmp(ch_turnOffAtSpecifiedTime,"true")!=0 && strcmp(ch_turnOffAtSpecifiedTime,"false")!=0){ 
    turnOffAtSpecifiedTime=false;
    strcpy(ch_turnOffAtSpecifiedTime,"false");
  }
  param_turnOffAtSpecifiedTime.setValue(ch_turnOffAtSpecifiedTime,5);

  turnOffTime=atoi(ch_turnOffTime); //returns 0 if user entered non-number, which is ok because that's just midnight
  if(turnOffTime>23){
    turnOffTime=0;
  }
  strcpy(ch_turnOffTime,itoa(turnOffTime,ch_turnOffTime,10));
  param_turnOffTime.setValue(ch_turnOffTime,2);

  if(strcmp(ch_turnOnAtSpecifiedTime,"true")!=0 && strcmp(ch_turnOnAtSpecifiedTime,"false")!=0){ //strcmp() returns 0 if they're equal
    turnOnAtSpecifiedTime=false;
    strcpy(ch_turnOnAtSpecifiedTime,"false");
  }
  param_turnOnAtSpecifiedTime.setValue(ch_turnOnAtSpecifiedTime,5);

  turnOnTime=atoi(ch_turnOnTime); //returns 0 if user entered non-number, which is ok because that's just midnight
  if(turnOnTime>23){ //default to midnight if greater than 24 hr clock 
    turnOnTime=0;
  }
  strcpy(ch_turnOnTime,itoa(turnOnTime,ch_turnOnTime,10));
  param_turnOnTime.setValue(ch_turnOnTime,2);

  GMTTimezone=atoi(ch_GMTTimezone);
  strcpy(ch_GMTTimezone,itoa(GMTTimezone,ch_GMTTimezone,10));
  param_GMTTimezone.setValue(ch_GMTTimezone,3);


  //now we save the values we got above to eeprom
  EEPROM.begin(200); //200 bytes is a little bit more than enough for our 10 parameters
  int address = 0;
  
  EEPROM.put(address,ch_radioReInit);
  address+=6;
  EEPROM.put(address,ch_turnOffAtSpecifiedTime);
  address+=6;
  EEPROM.put(address,ch_turnOffTime);
  address+=3;
  EEPROM.put(address,ch_turnOnAtSpecifiedTime);
  address+=6;
  EEPROM.put(address,ch_turnOnTime);
  address+=3;
  EEPROM.put(address,ch_GMTTimezone);
  address+=4;
  EEPROM.put(address,ch_radioAddress);
  address+=6;
  EEPROM.put(address,ch_mqtt_server);
  address+=51;
  EEPROM.put(address,ch_mqtt_topic);
  address+=51;
  EEPROM.put(address,ch_mqtt_debugTopic);
  address+=51;

  EEPROM.commit();
  EEPROM.end();
}

void setupConfigParameters(){

  wifiManager.setSaveParamsCallback(saveConfigParams); //when user presses save in captive portal, it'll fire this function so we can get the values
  // * setPreSaveConfigCallback, set a callback to fire before saving wifi or params [this could be useful if other function reboots esp before eeprom can save]

  //first we read in values from EEPROM to know what to display in the browser. We also use this to set global parameters
  EEPROM.begin(200);
  int address=0;
  //Serial.println("VALUE FROM EEPROM IS:");
  char ch_radioReInit[6];
  EEPROM.get(address,ch_radioReInit);
  address+=6;

  char ch_turnOffAtSpecifiedTime[6];
  EEPROM.get(address,ch_turnOffAtSpecifiedTime);
  address+=6;

  char ch_turnOffTime[3];
  EEPROM.get(address,ch_turnOffTime);
  address+=3;

  char ch_turnOnAtSpecifiedTime[6];
  EEPROM.get(address,ch_turnOnAtSpecifiedTime);
  address+=6;

  char ch_turnOnTime[3];
  EEPROM.get(address,ch_turnOnTime);
  address+=3;

  char ch_GMTTimezone[4];
  EEPROM.get(address,ch_GMTTimezone);
  address+=4;

  char ch_radioAddress[6];
  EEPROM.get(address,ch_radioAddress);
  address+=6;

  char ch_mqtt_server[51];
  EEPROM.get(address,ch_mqtt_server);
  address+=51;

  char ch_mqtt_topic[51];
  EEPROM.get(address,ch_mqtt_topic);
  address+=51;

  char ch_mqtt_debugTopic[51];
  EEPROM.get(address,ch_mqtt_debugTopic);
  address+=51;

  EEPROM.end();


  //now set global variables with the config we just read from EEPROM
  if(strcmp(ch_radioReInit,"true")==0){ //strcmp() returns 0 if they're equal
    reinitializeRadioAfterRelayOperation=true;
  }else{
    reinitializeRadioAfterRelayOperation=false;
  }

  if(strcmp(ch_turnOffAtSpecifiedTime,"true")==0){ //strcmp() returns 0 if they're equal
    turnOffAtSpecifiedTime=true;
  }else{
    turnOffAtSpecifiedTime=false;
  }

  turnOffTime=atoi(ch_turnOffTime); //returns 0 if user entered non-number, which is ok because that's just midnight
  if(turnOffTime>23){ //default to midnight if greater than 24 hr clock
    turnOffTime=0;
  }

  if(strcmp(ch_turnOnAtSpecifiedTime,"true")==0){ //strcmp() returns 0 if they're equal
    turnOnAtSpecifiedTime=true;
  }else{
    turnOnAtSpecifiedTime=false;
  }

  turnOnTime=atoi(ch_turnOnTime); //returns 0 if user entered non-number, which is ok because that's just midnight
  if(turnOnTime>23){ //default to midnight if greater than 24 hr clock (this value currently 21 because my function can't handle values of 22 or 23, but once that's fixed it should be changed to 23)
    turnOnTime=0;
  }

  GMTTimezone=atoi(ch_GMTTimezone);

  for(int i=0;i<6;i++){
    radioAddress[i]=ch_radioAddress[i];
  }

  strcpy(mqtt_server,ch_mqtt_server);

  strcpy(MQTTTopic,ch_mqtt_topic);

  strcpy(debugTopic,ch_mqtt_debugTopic);
  

  //now update the values in WiFiManager parameters to reflect values from EEPROM
  param_radioReInit.setValue(ch_radioReInit,5);
  param_turnOffAtSpecifiedTime.setValue(ch_turnOffAtSpecifiedTime,5);
  param_turnOffTime.setValue(ch_turnOffTime,2);
  param_turnOnAtSpecifiedTime.setValue(ch_turnOnAtSpecifiedTime,5);
  param_turnOnTime.setValue(ch_turnOnTime,2);
  param_GMTTimezone.setValue(ch_GMTTimezone,3);
  param_radioAddress.setValue(ch_radioAddress,5);
  param_mqtt_server.setValue(ch_mqtt_server,50);
  param_mqtt_topic.setValue(ch_mqtt_topic,50);
  param_mqtt_debugTopic.setValue(ch_mqtt_debugTopic,50);
  
  //after we've read from eeprom and updated the values, now we add the parameters (before config portal is launched)
  wifiManager.addParameter(&param_radioReInit);
  wifiManager.addParameter(&param_turnOffAtSpecifiedTime);
  wifiManager.addParameter(&param_turnOffTime);
  wifiManager.addParameter(&param_turnOnAtSpecifiedTime);
  wifiManager.addParameter(&param_turnOnTime);
  wifiManager.addParameter(&param_GMTTimezone);
  wifiManager.addParameter(&param_radioAddress);
  wifiManager.addParameter(&param_mqtt_server);
  wifiManager.addParameter(&param_mqtt_topic);
  wifiManager.addParameter(&param_mqtt_debugTopic);
}

void setup() {
  Serial.begin(9600);
  setupConfigParameters();
  
  pinMode(D1,INPUT_PULLUP);
  pinMode(RELAY,OUTPUT);
  pinMode(GLED,OUTPUT);
  pinMode(RLED,OUTPUT);
  

  timeClient.begin();
  timeClient.setTimeOffset(3600*GMTTimezone);

  Serial.print("Radio initialization code: ");
  Serial.println(radio.begin());
  radio.openReadingPipe(0, radioAddress);
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

void sendUDP(int num){
  Udp.beginPacket("192.168.68.118", 8888);
  char buf[10];
  itoa(num,buf,10);
  Udp.write(buf);
  Udp.endPacket();
}

void sendUDP(float num){ //this might do weird things?? It's approximately right, but it was bouncing between 0.16551 and 0.9930
  Udp.beginPacket("192.168.68.118", 8888);
  char sz[20] = {' '} ;
  int val_int = (int) num;   // compute the integer part of the float
  float val_float = (abs(num) - abs(val_int)) * 100000;
  int val_fra = (int)val_float;
  sprintf (sz, "%d.%d", val_int, val_fra);
  //Serial.println(sz);
  Udp.write(sz);
  Udp.endPacket();
}

void readAmperageVals(){
  if(millis()-analogReadTimer>10){ //using ADC more frequently than every 3ms causes wifi to die. But still at 3ms the DHCP server fails in softap mode... 5ms worked for awhile but then seemed inconsistent, so using 10ms now
    int currAmpVal=abs(analogRead(A0)-500);
    //int currAmpVal = 500;
    analogReadTimer=millis();
    if(currAmpVal>maxAmpSteps){
      maxAmpSteps=currAmpVal;
    }
  }
}

void checkOvercurrent(){
  //first calculate the value of amperage for this cycle
  if(millis()-calcAmperageTimer>500){
    float amperage = (maxAmpSteps/27.93)/1.0816;    //27.93 is steps/amp -- 1024/3.3 steps/volt * 0.09 volts/amp (datasheet). 1.0816 is experimental percentage error based on graphing multimeter value vs. output of reading using equation without this factor
    //PLEASE NOTE: Under 1A may be innacurate. Above 1A is approximately accurate but untested. This may over-report by 0.9A
    //sendUDP(amperage);
    //Serial.println(amperage);
    
    calcAmperageTimer=millis();
    maxAmpSteps=0;

    //now check for overcurrent
    avgCurrent[avgCurrentPtr] = amperage; //first add the new value to the running average array
    //Serial.println(avgCurrentPtr);
    if(avgCurrentPtr<9){ //increase pointer for next loop
      avgCurrentPtr++;
    }else{
      avgCurrentPtr=0;
    }
    //check running average
    float sum=0;
    for(int i=0;i<10;i++){
      sum+=avgCurrent[i];
    }
    float avgCurrentNow = sum/10;

    //sendUDP(avgCurrentNow);

    //decide if we're in overcurrent
    bool overcurrent=false;
    if(avgCurrentNow>6.0){ //if averaging above 6 amps for the last 5 seconds, we're in overcurrent
      overcurrent=true;
    }
    if(avgCurrent[avgCurrentPtr-1]>9 && avgCurrent[avgCurrentPtr-2]>9){ //if we've been over 9A for 1 second, break immediately
        overcurrent=true;
    }

    if(overcurrent){
      relayOff("Overcurrent",String(avgCurrentNow)); //turn relay off to stop overcurrent event
      while(true){ //stick in loop flashing light until user reboots (and hopefully unplugs device)
        delay(200);
        statusLEDColor("red");
        delay(200);
        statusLEDColor("off");
        yield(); //prevent WDT reset
      }
    }
  }
}

void turnOffAtTime(){ 
  //turnOffTime=1; //in hours, ex: 13 is 1:00pm
  if(millis()-ntpCheckTimer>60000*5){ //only process every 5 mins
    Serial.println("checking time...");
    Serial.print("Target time: ");
    Serial.println(turnOffTime);
    timeClient.update();
    Serial.println(timeClient.getHours());
    if(timeClient.getHours()==turnOffTime){ //turn off at 1am, but stop trying to after 2am.
      if(!timerTurnedOffToday){
        Serial.println("Turning off due to timer");
        relayOff("Timer",String(timeClient.getHours()));
        timerTurnedOffToday=true;
        //Serial.println("timer turning relay off");
      }
    }
    if(timeClient.getHours()>turnOffTime || (turnOffTime==23 && timeClient.getHours()==0)){
      timerTurnedOffToday=false;
    }
    ntpCheckTimer=millis();
  }
}

void turnOnAtTime(){
  if(millis()-ntpCheckTimer>60000*5){ //only process every 5 mins
    //Serial.println("checking time...");
    //Serial.print("Target time: ");
    //Serial.println(turnOnTime);
    timeClient.update();
    //Serial.println(timeClient.getHours());
    if(timeClient.getHours()==turnOnTime){
      if(!timerTurnedOnToday){
        Serial.println("Turning on due to timer");
        relayOn("Timer",String(timeClient.getHours()));
        timerTurnedOnToday=true;
        //Serial.println("timer turning relay off");
      }
    }
    if(timeClient.getHours()>turnOnTime || (turnONTime==23 && timeClient.getHours()==0)){
      timerTurnedOnToday=false;
    }
    ntpCheckTimer=millis();
  }
}

void loop() {

  readAmperageVals(); //every 5ms

  checkOvercurrent(); //every 500 ms

  if(turnOffAtSpecifiedTime){
    turnOffAtTime();
  }
  if(turnOnAtSpecifiedTime){
    turnOnAtTime();
  }
  
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
  client.publish(debugTopic,buf,true);
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
    radio.openReadingPipe(0, radioAddress);
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
    //char* clientName="genericSmartOutlet1";
    if (client.connect(WiFi.macAddress().c_str())) { //name of this client -- make MAC so it's unique
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe(MQTTTopic);
      
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
