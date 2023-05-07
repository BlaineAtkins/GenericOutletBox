//Upload this sketch before flashing the real outletBox code to load valid data in to EEPROM (otherwise, program will likely crash on boot)

#include <EEPROM.h>
void setup() {
  // put your setup code here, to run once:

  EEPROM.begin(200); //200 bytes is a little bit more than enough for our 10 parameters
  int address = 0;
  
  EEPROM.put(address,"false");
  address+=6;
  EEPROM.put(address,"false");
  address+=6;
  EEPROM.put(address,"0");
  address+=3;
  EEPROM.put(address,"false");
  address+=6;
  EEPROM.put(address,"0");
  address+=3;
  EEPROM.put(address,"-8");
  address+=4;
  EEPROM.put(address,"00000");
  address+=6;
  EEPROM.put(address,"broker.mqtt-dashboard.com");
  address+=51;
  EEPROM.put(address,"YourTopicHere");
  address+=51;
  EEPROM.put(address,"YourDebugTopicHere");
  address+=51;

  EEPROM.commit();
  EEPROM.end();

}

void loop() {
  // put your main code here, to run repeatedly:

}
