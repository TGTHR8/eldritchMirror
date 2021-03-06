/*  artnet to OctoWS2811 test

  OctoWS2811 Notes:

  Required Connections
  --------------------
  pin 2:  LED Strip #1    OctoWS2811 drives 8 LED Strips.
  pin 14: LED strip #2    All 8 are the same length.
  pin 7:  LED strip #3
  pin 8:  LED strip #4    A 100 ohm resistor should used
  pin 6:  LED strip #5    between each Teensy pin and the
  pin 20: LED strip #6    wire to the LED strip, to minimize
  pin 21: LED strip #7    high frequency ringining & noise.
  pin 5:  LED strip #8
  pin 15 & 16 - Connect together, but do not use
  pin 4 - Do not use
  pin 3 - Do not use as PWM.  Normal use is ok.

  Ethernet Notes:
  this was tested using a Wiz820io ethernet module. Standard SPI pin connections, as well as reset on Wiz820io to T3 pin 9

  Artnet Notes:
  ARTNET RECEIVER v3.1

  This SCRIPT allows you to use arduino with ethernet shield or wifi shield and recieve artnet data. Up to you to use channels as you want.

  Tested with Arduino 1.0.5, so this code should work with the new EthernetUdp library (instead of the depricated Udp library)

  If you have implemented improvements to this sketch, please contribute by sending back the modified sketch. It will be a pleasure to let them accessible to community

  Original code by (c)Christoph Guillermet, designed to be used with the free open source lighting software WhiteCat: http://www.le-chat-noir-numerique.fr
  karistouf@yahoo.fr

  v3, modifications by David van Schoorisse <d.schoorisse@gmail.com>
  Ported code to make use of the new EthernetUdp library used by Arduino 1.0 and higher.

  V3.1 by MSBERGER 130801
  - performance gain by shrinking buffer sizes from "UDP_TX_PACKET_MAX_SIZE" to 768
  - implementation of selction / filtering SubnetID and UniverseID (was already prepared by karistouf)
  - channel count starts at 0 instead of 1 (the digital and vvvv way)
  - artnet start_address+n is now mapped to "arduino-channel" 0+n (was also start_address+n bevore), now it is similar to lighting fixtures
*/

// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// libraries
#include <stdlib.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <OctoWS2811.h>
// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// from Artnet Receiver Sketch
#define short_get_high_byte(x) ((HIGH_BYTE & x) >> 8)
#define short_get_low_byte(x)  (LOW_BYTE & x)
#define bytes_to_short(h,l) ( ((h << 8) & 0xff00) | (l & 0x00FF) );

byte mac[] = {
  0x90, 0xA2, 0xDA, 0x0D, 0x4C, 0x8C
}
; //the mac adress in HEX of ethernet shield or uno shield board
byte ip[] = {
  192, 168, 1, 10
}; // the IP adress of your device, that should be in same universe of the network you are using

// the next two variables are set when a packet is received
byte remoteIp[4];        // holds received packet's originating IP
unsigned int remotePort; // holds received packet's originating port

//customisation: Artnet SubnetID + UniverseID
//edit this with SubnetID + UniverseID you want to receive
byte SubnetID = {
  0
};
byte UniverseID = {
  0
};
short select_universe = ((SubnetID * 16) + UniverseID);

//customisation: edit this if you want for example read and copy only 4 or 6 channels from channel 12 or 48 or whatever.
const int number_of_channels = 192; //up to 512 channels; using 192 right now = 8 strips * 8 rgb LEDs per strip * 3 colors
const int start_address = 0; // 0 if you want to read from channel 1

//buffers
const int MAX_BUFFER_UDP = 768;
char packetBuffer[MAX_BUFFER_UDP]; //buffer to store incoming data
//byte buffer_channel_arduino[number_of_channels]; //buffer to store filetered DMX data

// art net parameters
unsigned int localPort = 6454;      // artnet UDP port is by default 6454
const int art_net_header_size = 17;
const int max_packet_size = 576;
char ArtNetHead[8] = "Art-Net";
char OpHbyteReceive = 0;
char OpLbyteReceive = 0;
//short is_artnet_version_1=0;
//short is_artnet_version_2=0;
//short seq_artnet=0;
//short artnet_physical=0;
short incoming_universe = 0;
boolean is_opcode_is_dmx = 0;
boolean is_opcode_is_artpoll = 0;
boolean match_artnet = 1;
short Opcode = 0;

EthernetUDP Udp;


// for OctoWS8211 RGB control
const int ledsPerStrip = 8;
int SerialIn;
byte rgb[number_of_channels]; // buffer to hold values for all leds
int rgbSerialIdx = 0; // index to keep track of serial inputs

int displayMemory[ledsPerStrip * 6];
int drawingMemory[ledsPerStrip * 6];

int bytecounter;

const int config = WS2811_GRB | WS2811_800kHz;

OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config);

void setup() {

  leds.begin();

  Serial.begin(921600); // also allows direct serial control for testing


  //setup ethernet and udp socket
  Ethernet.begin(mac, ip);
  Udp.begin(localPort);

  // set up default colors
  initLEDs();
  leds.show();

}


void loop() {

  // control via serial
  while (Serial.available() > 0) {
    // mail call
    SerialIn = Serial.parseInt();
    rgb[rgbSerialIdx] = SerialIn;
    rgbSerialIdx++;
    if (Serial.read() == '\n') { // end of message
      setLEDs();
      leds.show();
      rgbSerialIdx = 0;
    }
  }// end of direct serial control

  // control via artnet
  int packetSize = Udp.parsePacket();

  //FIXME: test/debug check
  if (packetSize > art_net_header_size && packetSize <= max_packet_size) { //check size to avoid unneeded checks
    //if(packetSize) {

    IPAddress remote = Udp.remoteIP();
    remotePort = Udp.remotePort();
    Udp.read(packetBuffer, MAX_BUFFER_UDP);

    if (packetSize != 0)
    {
      Serial.println("heck");
    }

    //read header
    match_artnet = 1;
    for (int i = 0; i < 7; i++) {
      //if not corresponding, this is not an artnet packet, so we stop reading
      if (char(packetBuffer[i]) != ArtNetHead[i]) {
        match_artnet = 0;
        break;
      }
    }

    //if its an artnet header
    if (match_artnet == 1) {
      //artnet protocole revision, not really needed
      //is_artnet_version_1=packetBuffer[10];
      //is_artnet_version_2=packetBuffer[11];*/

      //sequence of data, to avoid lost packets on routeurs
      //seq_artnet=packetBuffer[12];*/

      //physical port of  dmx N°
      //artnet_physical=packetBuffer[13];*/

      //operator code enables to know wich type of message Art-Net it is
      Opcode = bytes_to_short(packetBuffer[9], packetBuffer[8]);

      //if opcode is DMX type
      if (Opcode == 0x5000) {
        is_opcode_is_dmx = 1;
        is_opcode_is_artpoll = 0;
      }

      //if opcode is artpoll
      else if (Opcode == 0x2000) {
        is_opcode_is_artpoll = 1;
        is_opcode_is_dmx = 0;
        //( we should normally reply to it, giving ip adress of the device)
      }

      //if its DMX data we will read it now
      if (is_opcode_is_dmx = 1) {

        //read incoming universe
        incoming_universe = bytes_to_short(packetBuffer[15], packetBuffer[14])
                            //if it is selected universe DMX will be read
        if (incoming_universe == select_universe) {

          //getting data from a channel position, on a precise amount of channels, this to avoid to much operation if you need only 4 channels for example
          //channel position
          for (int i = start_address; i < number_of_channels; i++) {
            //buffer_channel_arduino[i-start_address]= byte(packetBuffer[i+art_net_header_size+1]);
            rgb[i - start_address] = byte(packetBuffer[i + art_net_header_size + 1]);
          }
        }
      }
    }  //end of artnet sniffing
    setLEDs();
    leds.show();
  }
}

void setLEDs()
{
  // set LED colors
  for (int w = 0; w < ledsPerStrip * 8; w++) {
    leds.setPixel(w, rgb[w * 3], rgb[w * 3 + 1], rgb[w * 3 + 2]);
  }
}


void initLEDs()
{
  // set LED colors
  for (int w = 0; w < ledsPerStrip * 8; w++) {
    leds.setPixel(w, 0, 0, 0);
  }
}
