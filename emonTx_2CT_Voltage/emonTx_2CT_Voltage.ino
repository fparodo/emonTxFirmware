/*
                          _____    
                         |_   _|   
  ___ _ __ ___   ___  _ __ | |_  __
 / _ \ '_ ` _ \ / _ \| '_ \| \ \/ /
|  __/ | | | | | (_) | | | | |>  < 
 \___|_| |_| |_|\___/|_| |_\_/_/\_\
 
//--------------------------------------------------------------------------------------
// 2CT + AC-AC Voltage Adapter 

// Based on JeeLabs RF12 library http://jeelabs.org/2009/02/10/rfm12b-library-for-arduino/

// Contributors: Glyn Hudson, Trystan Lea, Michele 
// openenergymonitor.org
// GNU GPL V3

//using CT port 2 and port 3


//--------------------------------------------------------------------------------------
*/

#include "Emon.h"
EnergyMonitor emon1;
EnergyMonitor emon2;  

//JeeLabs libraries 
#include <Ports.h>
#include <RF12.h>
#include <avr/eeprom.h>
#include <util/crc16.h>  

ISR(WDT_vect) { Sleepy::watchdogEvent(); } 	

//---------------------------------------------------------------------------------------------------
// Serial print settings - disable all serial prints if SERIAL 0 - increases long term stability 
//---------------------------------------------------------------------------------------------------
#define SERIAL 1
//---------------------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------------------
// RF12 settings 
//---------------------------------------------------------------------------------------------------
// fixed RF12 settings

#define myNodeID 10         //in the range 1-30
#define network     210      //default network group (can be in the range 1-250). All nodes required to communicate together must be on the same network group
#define freq RF12_433MHZ     //Frequency of RF12B module can be RF12_433MHZ, RF12_868MHZ or RF12_915MHZ. You should use the one matching the module you have.

// set the sync mode to 2 if the fuses are still the Arduino default
// mode 3 (full powerdown) can only be used with 258 CK startup fuses
#define RADIO_SYNC_MODE 2

#define COLLECT 0x20 // collect mode, i.e. pass incoming without sending acks
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
// LED Indicator  
//--------------------------------------------------------------------------------------------------
# define LEDpin 9          //hardwired on emonTx PCB
//--------------------------------------------------------------------------------------------------

//########################################################################################################################
//Data Structure to be sent - should be the same as received on the emonBase or emonGLCD
//######################################################################################################################## 
typedef struct {
  	  int real1;		//real power reading from CT1	
          int real2;		//real power reading from CT2
          int supplyV;         	//emonTx supply voltage - only usefull when powering from batteries 
} Payload;
Payload emontx;
//########################################################################################################################
double apparent1, apparent2;
double vrms1, vrms2, irms1, irms2;


//********************************************************************
//SETUP
//********************************************************************
void setup()
{  
  
  pinMode(LEDpin, OUTPUT);
  digitalWrite(LEDpin, HIGH);    //turn on LED 
  
  Serial.begin(9600);
  Serial.println("emonTx 2 CT AC-AC example");
  Serial.println("openenergymonitor.org");
  
  rf12_initialize(myNodeID,freq,network); 
  rf12_sleep(RF12_SLEEP); 
  delay(10);
  Sleepy::loseSomeTime(3000);
  
  Serial.print("Node: "); 
  Serial.print(myNodeID); 
  Serial.print(" Freq: "); 
   if (freq == RF12_433MHZ) Serial.print("433Mhz");
   if (freq == RF12_868MHZ) Serial.print("868Mhz");
   if (freq == RF12_915MHZ) Serial.print("915Mhz");  
  Serial.print(" Network: "); 
  Serial.println(network);
  
  if (!SERIAL) {
    Serial.println("serial disabled"); 
    Serial.end();
  }
  
  digitalWrite(LEDpin, LOW);              //turn off LED
}

//********************************************************************
//LOOP
//********************************************************************
void loop()
{
  int vcc = readVcc();  //read emontx supply voltage

  //------------------------------------------------
  // MEASURE FROM CT 1
  //------------------------------------------------
  emon1.setPins(2,3); 				//emonTX AC-AC voltage (ADC2), current pin (CT1 - ADC3)
  emon1.calibration(234.89,126.5,1.7);		//voltage calibration , current calibration, power factor calibration
  emon1.calc(20,2000,vcc );			//No.of wavelengths, time-out , emonTx supply voltage 
    
  emontx.real1 = emon1.realPower;
  apparent1 = emon1.apparentPower;
  vrms1 = emon1.Vrms;
  irms1 = emon1.Irms;

  //------------------------------------------------
  // MEASURE FROM CT 2
  //------------------------------------------------
  emon2.setPins(2,0); // CT 2		   //emonTX AC-AC voltage (ADC2), current pin (CT2 - ADC0)
  emon2.calibration(234.89,126.5,1.7);    //voltage calibration , current calibration, power factor calibration 
  emon2.calc(20,2000,vcc );		 //No.of wavelengths, time-out , emonTx supply voltage 

    
  emontx.real2 = emon2.realPower;
  apparent2 = emon2.apparentPower;
  vrms2 = emon2.Vrms;
  irms2 = emon2.Irms;

  //------------------------------------------------
  emontx.supplyV = vcc;
  //------------------------------------------------
  
    //--------------------------------------------------------------------------------------------------
    // 2. Send data via RF 
    //--------------------------------------------------------------------------------------------------
    rfwrite() ;
    //--------------------------------------------------------------------------------------------------    
    
  if (SERIAL) { serial_output();}

  digitalWrite(LEDpin, HIGH);    //flash LED - very quickly 
  delay(2); 
  digitalWrite(LEDpin, LOW); 
  
//sleep controll code  
//Uses JeeLabs power save function: enter low power mode and update Arduino millis 
//only be used with time ranges of 16..65000 milliseconds, and is not as accurate as when running normally.http://jeelabs.org/2010/10/18/tracking-time-in-your-sleep/

if ( (emontx.supplyV) > 3300 ) //if emonTx is powered by 5V usb power supply (going through 3.3V voltage reg) then don't go to sleep
  delay(1000); //10s
else
  if ( (emontx.supplyV) < 2700)  //if battery voltage drops below 2.7V then enter battery conservation mode (sleep for 60s in between readings) (need to fine tune this value) 
    Sleepy::loseSomeTime(60000);
    else
      Sleepy::loseSomeTime(1000); //10s

}
//********************************************************************


//--------------------------------------------------------------------------------------------------
// Send payload data via RF - see http://jeelabs.net/projects/cafe/wiki/RF12 for RF12 library documentation 
//--------------------------------------------------------------------------------------------------
static void rfwrite(){
    rf12_sleep(RF12_WAKEUP);  
    int ec = 0;
    while (!rf12_canSend()) {
      rf12_recvDone();
      ec++;
      if (ec>1000) break;
    }
    //rf12_sendStart(rf12_hdr, &emontx, sizeof emontx, RADIO_SYNC_MODE); - with hdr info 
    rf12_sendStart(0, &emontx, sizeof emontx); 
    rf12_sendWait(2);  
    rf12_sleep(RF12_SLEEP);   
}

//--------------------------------------------------------------------------------------------------
// Read current emonTx battery voltage - not main supplyV!
//--------------------------------------------------------------------------------------------------
long readVcc() {
  long result;
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result;
  return result;
}

//--------------------------------------------------------------------------------------------------
// Serial output
//--------------------------------------------------------------------------------------------------
void serial_output()
{
    Serial.print(emontx.real1);
    Serial.print(' ');
    //Serial.print(apparent1);
    ///Serial.print(' ');
    //Serial.print(rms1);
    //Serial.print(' ');
    //Serial.print(irms1);
    //Serial.print(" | ");
    
    Serial.print(emontx.real2);
    Serial.print(' ');
    //Serial.print(apparent2);
    //Serial.print(' ');
    //Serial.print(vrms2);
    //Serial.print(' ');
    //Serial.print(irms2);
    //Serial.print(" | ");

    Serial.println(emontx.supplyV); 
}
