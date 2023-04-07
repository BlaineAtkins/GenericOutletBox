#include <nRF24L01.h>
#include <RF24.h>
#include <SPI.h>

RF24 radio(7,8); //(CNS, CE)

const byte address[6] = "00011";

unsigned long lastStateChange;

void setup() {
  Serial.begin(9600);
  Serial.println(radio.begin());
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_MAX);
  radio.stopListening();
  radio.setAutoAck(true);
  radio.setDataRate(RF24_250KBPS);

  pinMode(2,INPUT_PULLUP);

}

void loop() {

  if(!digitalRead(2)){
    delay(5); //if button has been let go already after this, it was a bounce
    if(!digitalRead(2)){
      delay(50); //wait to stop bouncing in case it still is
      const char text[] = "t";
      radio.write(&text, sizeof(text));
      while(!digitalRead(2)){}; //wait to start detecting new presses until user lets go
    }
  }

/*
  //keep transmitting for a little bit immediately after a state change to combat lost packets
  if(millis()-lastStateChange<2000){
    Serial.println("Transmitting state...");
    if(lampOn){
      const char text[] = "on";
      radio.write(&text, sizeof(text));
    }else{
      const char text[] = "off";
      radio.write(&text, sizeof(text));
    }
  }
  */
}
