    /* V2.70 - 30/07/2023 - Daniel Desmartins
     *  in collaboration and test with Lolo85 and BricBric
     *  Connected to the Relay Port in AgOpenGPS
     *  If you find any mistakes or have an idea to improove the code, feel free to contact me. N'hésitez pas à me contacter en cas de problème ou si vous avez une idée d'amélioration.
     *
     *  simulateur   ecrivain
     *  Mode Demo, launches the demonstration of the servomotors. 
     *  Advanced in a field in simulation in AOG... admired the result!!!
     *  To activate do pulse on A0 to ground!
     *  bric bric  29/07/2023
     */

//-----------------------------------------------------------------------------------------------
// Change this number to reset and reload default parameters To EEPROM
#define EEP_Ident 0x5425  

//the default network address
struct ConfigIP {
    uint8_t ipOne = 192;
    uint8_t ipTwo = 168;
    uint8_t ipThree = 5;
};  ConfigIP networkAddress;   //3 bytes
//-----------------------------------------------------------------------------------------------

#include <EEPROM.h> 
#include <Wire.h>
#include "EtherCard_AOG.h"
#include <IPAddress.h>

// ethernet interface ip address
static uint8_t myip[] = { 0,0,0,123 };

// gateway ip address
static uint8_t gwip[] = { 0,0,0,1 };

//DNS- you just need one anyway
static uint8_t myDNS[] = { 8,8,8,8 };

//mask
static uint8_t mask[] = { 255,255,255,0 };

//this is port of this autosteer module
uint16_t portMy = 5123;

//sending back to where and which port
static uint8_t ipDestination[] = { 0,0,0,255 };
uint16_t portDestination = 9999; //AOG port that listens

// ethernet mac address - must be unique on your network
static uint8_t mymac[] = { 0x00,0x00,0x56,0x00,0x00,0x7B };

uint8_t Ethernet::buffer[200]; // udp send and receive buffer

//Program counter reset
void(*resetFunc) (void) = 0;

//ethercard 10,11,12,13 Nano = 10 depending how CS of ENC28J60 is Connected
#define CS_Pin 10

//Communication with AgOpenGPS
int16_t EEread = 0;

//pins:
#define NUM_OF_SECTIONS 8 //16 relays max for PCA9685
#define PinAogReady 2 //Pin AOG Ready
#define PinAogConnected 3 //Pin AOG Connected
#define PinDemoMode A0 //launches the demonstration of the servomotors. Advanced in a field in simulation in AOG... admired the result!!!
const uint8_t LED_PinArray[] = {4, 5, 6, 7, 8, 9, A1, A2, A3}; //Pins, Led activation sections
bool readyIsActive = LOW;

///////////Régler ici la position d'ouverture, fermeture et neutre de vos servotmoteur (dans l'orde de 1 à 16)/////////////
uint8_t positionOpen[] =    { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};   //position ouvert en degré
uint8_t positionNeutral[] = { 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90};   //position neutre en degré
uint8_t positionClosed[] =  {155,155,155,155,155,155,155,155,155,155,155,155,155,155,155,155};   //position fermé en degré
#define TIME_RETURN_NEUTRAL 10 //temps de retour au neutre en cycle 10 = 1000ms //0 = pas de retour au neutre et trop court pas le temps d'aller à la position demandée!

#define ANGLE_MIN 0
#define ANGLE_MAX 180

//Réglage des servomoteurs Attention les positions max et min doivent être définies au préalable par vous même pour vos servomoteurs.
#define SERVO_MIN 90
#define SERVO_MAX 540
#define SERVO_FREQ 50 //Généralement par defaut 50hz pour les SG90

#include <Adafruit_PWMServoDriver.h>
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

//Demo Mode
const uint8_t message[] = { 0, 0, 0, 126, 9, 9, 126, 0, 0, 62, 65, 65, 62, 0, 0, 62, 65, 73, 58, 0, 0, 127, 2, 4, 2, 127, 0, 0, 126, 9, 9, 126, 0, 1, 1, 127, 1, 1, 0, 0, 65, 127, 65, 0, 0, 62, 65, 65, 34, 0, 0, 0, 0};
const uint16_t tableau = 530;
uint16_t boucle = 0;
bool demoMode = false;

//Variables:
const uint8_t loopTime = 100; //10hz
uint32_t lastTime = loopTime;
uint32_t currentTime = loopTime;
uint8_t lastTimeSectionMove[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
bool lastPositionMove[] = {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};

//Comm checks
uint8_t watchdogTimer = 12;     //make sure we are talking to AOG
uint8_t serialResetTimer = 0;   //if serial buffer is getting full, empty it

//hello from AgIO
uint8_t helloFromMachine[] = { 128, 129, 123, 123, 5, 0, 0, 0, 0, 0, 71 };

uint8_t AOG[] = { 0x80, 0x81, 0x7B, 0xEA, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0xCC };

//The variables used for storage
uint8_t sectionLo = 0, sectionHi = 0;

uint8_t count = 0;

boolean autoModeIsOn = false;
boolean manuelModeIsOn = false;
boolean aogConnected = false;
boolean firstConnection = true;

//End of variables

void setup() {  
  pinMode(PinAogReady, OUTPUT);
  pinMode(PinAogConnected, OUTPUT);
  for (count = 0; count < NUM_OF_SECTIONS; count++) {
    pinMode(LED_PinArray[count], OUTPUT);
    digitalWrite(LED_PinArray[count], LOW);
  }
  pinMode(PinDemoMode, INPUT_PULLUP);

  digitalWrite(PinAogConnected, HIGH);
  digitalWrite(PinAogReady, !readyIsActive);
  
  Serial.begin(38400);  //set up communication
  while (!Serial) {
    // wait for serial port to connect. Needed for native USB
  }
  
  EEPROM.get(0, EEread);              // read identifier

  if (EEread != EEP_Ident)   // check on first start and write EEPROM
  {
      EEPROM.put(0, EEP_Ident);
      EEPROM.put(50, networkAddress);
  }
  else
  {
      EEPROM.get(50, networkAddress);
  }

  if (ether.begin(sizeof Ethernet::buffer, mymac, CS_Pin) == 0)
      Serial.println(F("Failed to access Ethernet controller"));

  //grab the ip from EEPROM
  myip[0] = networkAddress.ipOne;
  myip[1] = networkAddress.ipTwo;
  myip[2] = networkAddress.ipThree;

  gwip[0] = networkAddress.ipOne;
  gwip[1] = networkAddress.ipTwo;
  gwip[2] = networkAddress.ipThree;

  ipDestination[0] = networkAddress.ipOne;
  ipDestination[1] = networkAddress.ipTwo;
  ipDestination[2] = networkAddress.ipThree;

  //set up connection
  ether.staticSetup(myip, gwip, myDNS, mask);
  ether.printIp("_IP_: ", ether.myip);
  ether.printIp("GWay: ", ether.gwip);
  ether.printIp("AgIO: ", ipDestination);

  //register to port 8888
  ether.udpServerListenOnPort(&udpSteerRecv, 8888);

  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
  
  delay(200); //wait for IO chips to get ready
  switchRelaisOff();
} //end of setup

void loop() {
  currentTime = millis();
  if (currentTime - lastTime >= loopTime) {  //start timed loop
    lastTime = currentTime;
    
    //avoid overflow of watchdogTimer:
    if (watchdogTimer++ > 250) watchdogTimer = 12;
    
    //clean out serial buffer to prevent buffer overflow:
    if (serialResetTimer++ > 20) {
      while (Serial.available() > 0) Serial.read();
      serialResetTimer = 0;
    }

    if (TIME_RETURN_NEUTRAL) returnNeutralPosition();
    
    if (watchdogTimer > 20) {
      if (aogConnected && watchdogTimer > 60) {
        aogConnected = false;
        firstConnection = true;
        digitalWrite(PinAogConnected, LOW);
        digitalWrite(PinAogReady, !readyIsActive);
      } else if (watchdogTimer > 240) digitalWrite(PinAogConnected, LOW);
    }
    
    //emergency off:
    if (watchdogTimer > 10) {
      switchRelaisOff();
      for (count = 0; count < NUM_OF_SECTIONS; count++) {
        digitalWrite(LED_PinArray[count], LOW);
      }
    } else {
      for (count = 0; count < NUM_OF_SECTIONS; count++) {
        if (count < 8) {
          setSection(count, bitRead(sectionLo, count)); //Open or Close sectionLo if AOG requests it in auto mode
          digitalWrite(LED_PinArray[count], bitRead(sectionLo, count));
        } else {
          setSection(count, bitRead(sectionHi, count-8)); //Open or Close  le sectionHi if AOG requests it in auto mode
          digitalWrite(LED_PinArray[count], bitRead(sectionHi, count-8));
        }
      }
      
      //Add For control Master swtich
      if (NUM_OF_SECTIONS < 16) {
        setSection(15, (sectionLo || sectionHi));
      }
      
      //Demo Mode
      if (demoMode) {
        if (boucle++ > tableau) boucle = 0;
        uint8_t boom = message[boucle/10];

        AOG[9] = (uint8_t)boom; //onLo;
        AOG[10] = (uint8_t)~boom; //offLo;

        //checksum
        int16_t CK_A = 0;
        for (uint8_t i = 2; i < sizeof(AOG) - 1; i++)
        {
          CK_A = (CK_A + AOG[i]);
        }
        AOG[sizeof(AOG) - 1] = CK_A;
        
        //off to AOG
        ether.sendUdp(AOG, sizeof(AOG), portMy, ipDestination, portDestination);
      } else {
        demoMode = !digitalRead(PinDemoMode);
      }
    }
  }
  delay(1);

  //this must be called for ethercard functions to work. Calls udpSteerRecv() defined way below.
  ether.packetLoop(ether.packetReceive());
}

//callback when received packets
void udpSteerRecv(uint16_t dest_port, uint8_t src_ip[IP_LEN], uint16_t src_port, uint8_t* udpData, uint16_t len)
{
  //The data package
  if (udpData[0] == 0x80 && udpData[1] == 0x81 && udpData[2] == 0x7F) //Data
  {
    if (!aogConnected) {
      watchdogTimer = 12;
      digitalWrite(PinAogConnected, HIGH);
    }

    if (udpData[3] == 239) //machine Data
    {
      sectionLo = udpData[11];          // read relay control from AgOpenGPS
      sectionHi = udpData[12];
      
      //reset watchdog
      watchdogTimer = 0;
  
      if (!aogConnected) {
        digitalWrite(PinAogReady, readyIsActive);
        aogConnected = true;
      }
    }
    else if (udpData[3] == 200) // Hello from AgIO
    {
      helloFromMachine[5] = sectionLo;
      helloFromMachine[6] = sectionHi;
      
      if (udpData[7] == 1)
      {
        sectionLo -= 255;
        sectionHi -= 255;
        watchdogTimer = 0;
      }

      helloFromMachine[5] = sectionLo;
      helloFromMachine[6] = sectionHi;

      ether.sendUdp(helloFromMachine, sizeof(helloFromMachine), portMy, ipDestination, portDestination);
    }
    else if (udpData[3] == 201)
    {
        //make really sure this is the subnet pgn
        if (udpData[4] == 5 && udpData[5] == 201 && udpData[6] == 201)
        {
            networkAddress.ipOne = udpData[7];
            networkAddress.ipTwo = udpData[8];
            networkAddress.ipThree = udpData[9];

            //save in EEPROM and restart
            EEPROM.put(50, networkAddress);
            resetFunc();
        }
    }
    //Scan Reply
    else if (udpData[3] == 202)
    {
        //make really sure this is the subnet pgn
        if (udpData[4] == 3 && udpData[5] == 202 && udpData[6] == 202)
        {
            uint8_t scanReply[] = { 128, 129, 123, 203, 7, 
                networkAddress.ipOne, networkAddress.ipTwo, networkAddress.ipThree, 123,
                src_ip[0], src_ip[1], src_ip[2], 23   };

            //checksum
            int16_t CK_A = 0;
            for (uint8_t i = 2; i < sizeof(scanReply) - 1; i++)
            {
                CK_A = (CK_A + scanReply[i]);
            }
            scanReply[sizeof(scanReply)-1] = CK_A;

            static uint8_t ipDest[] = { 255,255,255,255 };
            uint16_t portDest = 9999; //AOG port that listens

            //off to AOG
            ether.sendUdp(scanReply, sizeof(scanReply), portMy, ipDest, portDest);
        }
    }
  }
}

void switchRelaisOff() {  //that are the relais, switch all off
  for (count = 0; count < NUM_OF_SECTIONS; count++) {
    setSection(count, false);
  }
  
  //Add For control Master swtich
  if(NUM_OF_SECTIONS < 16) {
    setSection(15, false);
  }
}

void setSection(uint8_t section, bool sectionActive) {
  if (sectionActive && !lastPositionMove[section]) {
    setPosition(section, positionOpen[section]);
    lastPositionMove[section] = true;
    lastTimeSectionMove[section] = 0;
  } else if (!sectionActive && lastPositionMove[section]) {
    setPosition(section, positionClosed[section]);
    lastPositionMove[section] = false;
    lastTimeSectionMove[section] = 0;
  }
}

void returnNeutralPosition() {
  uint8_t tmp = 0;
  for (count = 0; count < NUM_OF_SECTIONS; count++) {
    tmp = lastTimeSectionMove[count];
    if (tmp != 255) {
      if (tmp < TIME_RETURN_NEUTRAL) {
        tmp++;
      } else {
        setPosition(count, positionNeutral[count]);
        tmp = 255;
      }
    }
    lastTimeSectionMove[count] = tmp;
  }
}

void setPosition(uint8_t section, uint16_t angle) {
  uint16_t t_position = map(angle, ANGLE_MIN, ANGLE_MAX, SERVO_MIN, SERVO_MAX);
  pwm.setPWM(section, 0, t_position);
}