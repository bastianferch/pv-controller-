/*
 The AD8400 is SPI-compatible. To command it you send two bytes. 
 The first byte is the channel number starting at zero: (0)
 The second byte is the resistance value for the channel: (0 - 255).
  * CS - to digital pin 10  (SS pin)
  * SDI - to digital pin 11 (MOSI pin)
  * CLK - to digital pin 13 (SCK pin)
*/

#include <SPI.h>
// set pin 10 as the slave select for the digital pot:
const int SLAVE_SELECT_PIN=10;
const int channel=0;
// const int shutDownPin = 7;
#define REL_IN 2 // Input relay 2, 18
#define REL_OUT 3 // Output relay 3, 19
#define MEAS_ON 4 // Measure on 4,20
#define BAT_U_PIN A6 //  input voltage measurement battery A6
#define PV_U_PIN A7 // input voltage measurement PV A7 
#define PV_U_START_CHARGING 28000 // voltage to start charinging battery
#define BAT_U_MAX 30600 //voltage to charging battery full
#define BAT_U_MAX_H 30200 //voltage to restart charging battery (Hysteresis)
#define BAT_U_MIN 28000 //voltage to stop discharging battery emty
#define BAT_U_MIN_H 29000 //voltage to restart discharing (Hysteresis)
#define BAT_LIN 29700 // battery linear voltage to power 28V until 29,7V
#define INVERTER_START_DIODE_U 260 // voltage charging diode discharging starts
#define INVERTER_START_DIODE_U_H 280 //voltage charging diode discharging stops (Hysteresis)
#define INVERTER_START_U 280 // voltage pv_u discharging starts


unsigned long night_start=0, night_length=0, time_power_check;
word bat_u, pv_u,battery_power;
word last_bat_u, last_pv_u, discharge_current; //voltage in mV, current in mA   
boolean relay_on[]={false,false, false, false}; // {NULL, NULL,REL_IN, REL_OUT}

void setup(){
  clockspeed();
  //Serial.begin(9600); //open the serial port at 9600 bps: Achtung aufgrund der Reduktion der Taktfrequenz die Baudrate am Computer 600
  pinmodes();
  initialize_SPI();
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();

  estimate_bat_power();

  if (pv_u > PV_U_START_CHARGING) {
    if (bat_u<BAT_U_MAX) {
      relay_on[REL_IN]=true;
      do_relay_on(REL_IN,true);
    }        
  } else if (bat_u>BAT_U_MIN_H){
    discharge_current=2000;
    start_discharging();
  }  
}

void loop(){

  last_bat_u = bat_u;
  last_pv_u = pv_u;
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();

  battery_power=calculate_power_in_battery(battery_power);


  if (!relay_on[REL_IN] && (bat_u<BAT_U_MAX_H)) {
    if ((!relay_on[REL_OUT] && (pv_u>PV_U_START_CHARGING)) || (relay_on[REL_OUT] && (pv_u>bat_u))) {
      relay_on[REL_IN]=true;
      do_relay_on(REL_IN,true);
    }
  }
  if (relay_on[REL_IN] && ((pv_u<bat_u) || (bat_u> BAT_U_MAX))) {
    relay_on[REL_IN]=false;
    do_relay_on(REL_IN,false);
  }
  if ( !relay_on[REL_OUT] && (bat_u> BAT_U_MIN_H)) {
    if ((relay_on[REL_IN] && ((pv_u-bat_u)<INVERTER_START_DIODE_U)) || (!relay_on[REL_IN] && ( pv_u <INVERTER_START_U))) {
      Leistungsermittlung_Nacht();
      discharge_current=2000;
      start_discharging();
    }
  }
  if (relay_on[REL_OUT] && ((bat_u<BAT_U_MIN) || (relay_on[REL_IN] && (pv_u-bat_u)>INVERTER_START_DIODE_U_H))) {
    relay_on[REL_OUT]=false;
    do_relay_on(REL_OUT,false);
  }
  Leistungssteuerung();
  check_night_start();
  check_night_end();
  Parameterausgabe_Serieller_Monitor();
  delay(7500); //2 Minuten (Taxtfrequenz reduziert auf 1 Mhz)
}

// ******* CHAPERT SUBROUTINES SETUP
void clockspeed() {
//clock to 1MHz
  CLKPR=0x80,
  CLKPR=0x04;
}

void pinmodes() {
  pinMode(REL_IN, OUTPUT);
  pinMode(REL_OUT, OUTPUT);
  pinMode(MEAS_ON, OUTPUT);
  pinMode(BAT_U_PIN, INPUT);
  pinMode(PV_U_PIN, INPUT);
}

void initialize_SPI(){
  // set the slaveSelectPin as an output
  pinMode (SLAVE_SELECT_PIN, OUTPUT);
  // initialize SPI
  SPI.begin();
  //Set all pots to zero as a starting point
  discharge_current = 0;
  set_power_of_discharge(discharge_current);
}

void estimate_bat_power(){
  if (bat_u<BAT_LIN) {
    battery_power = map(bat_u, BAT_U_MIN, BAT_LIN, 0, 25000); 
  } else {
    battery_power = 30000;        
  }
}

// ******* CHAPERT SUBROUTINES Battery and PV voltage measurement
void start_measure_voltage(){
  digitalWrite(MEAS_ON,1);
  delay(10);
}

word measure_voltage(int analog_pin){
  return map(analogRead(analog_pin),0,1023,23650, 33650); 
}

void stop_measure_voltage(){
  digitalWrite(MEAS_ON,0);
}

// ******* CHAPERT SUBROUTINE Battery Power Calculation
long calculate_power_in_battery(long powermah){
  word milliamps = 0, diode_u = 0;

  diode_u = (pv_u - bat_u + last_pv_u - last_bat_u) / 2;
  if (relay_on[REL_IN] && (diode_u>0)){  
    switch (diode_u) {
      case 0 ... 250: 
        milliamps = map (diode_u, 0, 250, 100, 300); 
        break;
      case 251 ... 300:     
        milliamps = map (diode_u, 251, 300, 400, 1000); 
        break;
      case 301 ... 315:           
        milliamps = map (diode_u, 301, 315, 1000, 2000); 
        break;
      case 316 ... 335:           
        milliamps = map (diode_u, 316, 335, 2000, 3000); 
        break;
      case 336 ... 350:           
        milliamps = map (diode_u, 336, 350, 3000, 4000); 
        break;
      case 351 ... 405:           
        milliamps = map (diode_u, 351, 405, 4000, 9000); 
        break;
      default:           
        milliamps = map (diode_u, 405, 415, 9000, 10000); 
        break;
    }
  }
  if (relay_on[REL_OUT]) {
    milliamps = milliamps - discharge_current;
  }
  powermah = powermah + (millis()-time_power_check)*milliamps/225000;
  time_power_check = millis();
}


// ******* CHAPERT SUBROUTINES IO Management and SPI Management DigitalResistance
void do_relay_on(byte pin, boolean doit){
 if (doit) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }     
}

void start_discharging() { //Battery discharge power control function
  set_power_of_discharge(1500);
    relay_on[REL_OUT]=true;
    do_relay_on(REL_OUT,true);
    delay(15000);
    set_power_of_discharge(discharge_current);
  
}

void set_power_of_discharge(int value) { //milliampere
  value = map(value,0,6500,0,58);
  digitalPotWrite(0, value);
}
 
void digitalPotWrite(int address, int value) {
  // take the SS pin low to select the chip:
  digitalWrite(SLAVE_SELECT_PIN, LOW);
  delay(50);
  //  send in the address and value via SPI:
  SPI.transfer(address);
  SPI.transfer(value);
  // take the SS pin high to de-select the chip:
  delay(50);
  digitalWrite(SLAVE_SELECT_PIN, HIGH); 
}


// ******* CHAPERT SUBROUTINES Night Calulations
void check_night_start(){
  if(last_pv_u > 8000 & pv_u < 8000){
    night_start = millis();
  }
}
void check_night_end(){
  if(last_pv_u < 8000 & pv_u > 8000 & (millis() - night_start) > 675000){
    night_length = millis() - night_start;    
  }
}


// ******* CHAPERT SUBROUTINES Night Power Management
void Leistungsermittlung_Nacht () {
  
}

void Leistungssteuerung () {
  
}

// ******* CHAPERT SUBROUTINE Information on seriell Monitor
void Parameterausgabe_Serieller_Monitor() {
  //Serial.println("Spannung PV(mV):" + String(pv_u) + "Spannug Bat(mV):" + String(bat_u));
  //Serial.println("beide Rel off");
}



void sleep(){
  // TODO
}