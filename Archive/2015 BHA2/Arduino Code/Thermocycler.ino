/*
	This file is part of Waag Society's BioHack Academy Code.

	Waag Society's BioHack Academy Code is free software: you can 
	redistribute it and/or modify it under the terms of the GNU 
	General Public License as published by the Free Software 
	Foundation, either version 3 of the License, or (at your option) 
	any later version.

	Waag Society's BioHack Academy Code is distributed in the hope 
	that it will be useful, but WITHOUT ANY WARRANTY; without even 
	the implied warranty of MERCHANTABILITY or FITNESS FOR A 
	PARTICULAR PURPOSE.  See the GNU General Public License for more 
	details.

	You should have received a copy of the GNU General Public License
	along with Waag Society's BioHack Academy Code. If not, see 
	<http://www.gnu.org/licenses/>.
*/

/* *******************************************************
/  Libraries
*/

#include <math.h>    // loads a library with more advanced math functions
#include <Wire.h> // Needed for I2C connection
#include "LiquidCrystal_I2C.h" // Needed for operating the LCD screen
/* *******************************************************
*/

/* *******************************************************
/  LCD
*/
// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27,16,2);
/* *******************************************************
*/

/* *******************************************************
/  Thermocycler Settings
*/
int tempSettings[3] = { 0, 0, 0}; // Temperatures
int timeSettings[3] = { 0, 0, 0}; // Times
String phaseNames[3] = { "Denat", "Anneal", "Elon" }; // Names of Phases
#define fanPin 10     // The mosfet that drives the 80mm fan is connected to pin 10
#define heatPin 11   // Pin for the relay that controls the heat pad
double temp;            //Variable to hold a temperature value
int val;                //Create an integer variable
/* *******************************************************
*/

/* *******************************************************
/  Machine User Interface
*/
boolean buttonState = 0; // Start button
int ledstate = false; // Blinking indicator LED
// set pin numbers:
const int buttonPin = 7;     // the number of the pushbutton pin
const int ledPin =  13;      // the number of Arduino's onboard LED pin
/* *******************************************************
*/

/* *******************************************************
/  Set the initial state of the machine
/  In this code we will switch operation modes, from programming time, to programming speed, to spinning, to stopping/slowing down
*/
#define STATE_TIMEPROG 1
#define STATE_TEMPPROG 2
#define STATE_CYCLING 3
#define STATE_STOP 4

byte state = STATE_TIMEPROG;
/* *******************************************************
*/

/* *******************************************************
/  Variables needed for keeping track of time
*/
uint32_t lastTick = 0; // Global Clock
uint32_t stateStartTime = 0; // Start state Clock
uint32_t StateDt; // Time within a state
uint32_t PhaseStartTime = 0;
int LCDTime = 0;

/* Useful Constants */
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24L)
 
/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY)  
/* *******************************************************
*/

/* *******************************************************
/  Rotary Encoder
*/
//these pins can not be changed 2/3 are special interrupt pins
int encoderPin1 = 2;
int encoderPin2 = 3;

volatile int lastEncoded = 0;
volatile long encoderValue = 0;

long lastencoderValue = 0;

int lastMSB = 0;
int lastLSB = 0;
/* *******************************************************
*/

/* *******************************************************
/  Setup, this code is only executed once
*/
void setup() {
  // Update clock
  lastTick = millis();

  // Initialize I2C
  Wire.begin();

  // Open serial connection and print a message
  Serial.begin(9600);
  Serial.println(F("BioHack Academy Thermocycler"));

  // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);
  // initialize the pushbutton pin as an input:
  pinMode(buttonPin, INPUT);
  
  // rotary encoder
  pinMode(encoderPin1, INPUT); 
  pinMode(encoderPin2, INPUT);
  digitalWrite(encoderPin1, HIGH); //turn pullup resistor on
  digitalWrite(encoderPin2, HIGH); //turn pullup resistor on
  //call updateEncoder() when any high/low changed seen
  //on interrupt 0 (pin 2), or interrupt 1 (pin 3) 
  attachInterrupt(0, updateEncoder, CHANGE); 
  attachInterrupt(1, updateEncoder, CHANGE);  
  
  // Initialize the LCD and print a message
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("BioHack Academy"));
  lcd.setCursor(0,1);
  lcd.print(F("Thermocycler"));
  delay(1000);
  lcd.clear();
}
/* *******************************************************
*/

/* *******************************************************
/  Thermistor function converts the raw signal into a temperature
*/
double Thermister(int RawADC) {  //Function to perform the fancy math of the Steinhart-Hart equation
  double Temp;
  Temp = log(((10240000/RawADC) - 10000));
  Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * Temp * Temp ))* Temp );
  Temp = Temp - 273.15;              // Convert Kelvin to Celsius
  //Temp = (Temp * 9.0)/ 5.0 + 32.0; // Celsius to Fahrenheit - comment out this line if you need Celsius
  return Temp;
}
/* *******************************************************
*/

/* *******************************************************
/  Loop, this code is constantly repeated
*/
void loop() {
  // Update clock
  uint32_t time = millis(); // current time since start of sketch
  uint16_t dt = time-lastTick; // difference between current and previous time
  lastTick = time;

  // Read temperature
  val=analogRead(0);      //Read the analog port 0 and store the value in val
  temp=Thermister(val);   //Runs the fancy math on the raw analog value
  
  // Print temperature
  Serial.print("Temperature: ");
  Serial.println(temp);

  // Button updates
  buttonState = digitalRead(buttonPin);
  
  // Blink the LED, indicating that the Arduino is working
  if (ledstate == false) {
    // turn LED on:
    digitalWrite(ledPin, HIGH);
    ledstate = true;
  }
  else {
    // turn LED off:
    digitalWrite(ledPin, LOW);
    ledstate = false;
  }
  
  // Do machine logic
  machineUpdate(dt);
  
  // Reset button state
  buttonState = 0;

  // Wait 200 microsconds
  delay(200);
}
/* *******************************************************
*/

/* *******************************************************
/  machineUpdate, this function checks in which state the device is and executes the code that belongs to that state
*/
void machineUpdate(uint16_t dt) {

  // StateTimeProgramming is the first state in which the user can set the time that each phase should last
  if(state == STATE_TIMEPROG) {
    
    // Sanitize the values of the Rotary encoder, no less than 0, no more than 100
    if(encoderValue < 0) encoderValue = 0;
    if(encoderValue > 120) encoderValue = 120;
  
    // Convert encoder value to seconds  
    timeSettings[0] = encoderValue;

    // Display time setting on the LCD
    lcd.setCursor(0,0);
    lcd.print(F("Time"));
    lcd.setCursor(6,0);
    lcd.print(time(timeSettings[0]));    

    // In case the button is pressed, continue to next state
    if(buttonState > 0) {
      stateChange(STATE_TEMPPROG);
      encoderValue = 0;
    }  
  } 
 
  // STATE_TEMPPROG is similar to STATE_TIMEPROG, but now the user can set the temperature of each phase
  if(state == STATE_TEMPPROG) {
    
    // Sanity check
    if(encoderValue < 0) encoderValue = 0;
    if(encoderValue > 100) encoderValue = 100;
  
    tempSettings[0] = encoderValue;

    // Display the settings on the LCD
    lcd.setCursor(0,1);
    lcd.print(F("Temp"));
    lcd.setCursor(5,1);
    lcd.print(encoderValue);

    // Continue to next state if the button is pressed
    if(buttonState > 0) {
      stateChange(STATE_CYCLING);
      encoderValue = 0;
    }     
  } 
 
  // State Cyling is the state in which the thermocycler is running
  if(state == STATE_CYCLING) {

        
    // When done
    if(1 == 2) {
      stateChange(STATE_STOP);
    }
    
    // Change state if the user presses the button
    if(buttonState > 0) {
      stateChange(STATE_STOP);
    }     
  }

  // StateStop stops the cycling
  if(state == STATE_STOP) {

    // Reset the variables
    
    // Go back to the first state
    stateChange(STATE_TIMEPROG);
  }
}
/* *******************************************************
*/

/* *******************************************************
/  stateChange switches the machine logic from one state to another
*/
void stateChange(byte newstate) {
  // set new state
  state = newstate;
  
  // reset starting time of state
  PhaseStartTime = millis();  
  
  // reset button
  buttonState = 0;
}
/* *******************************************************
*/

/* *******************************************************
/  time converts seconds to minutes:seconds format
*/
String time(int val){  
  // calculate number of days, hours, minutes and seconds
  int days = elapsedDays(val);
  int hours = numberOfHours(val);
  int minutes = numberOfMinutes(val);
  int seconds = numberOfSeconds(val);
            
  String returnval = "";
            
  // digital clock display of current time 
  returnval = printDigits(minutes) + ":" + printDigits(seconds) + "   ";
  
  // return value      
  return returnval;
}
/* *******************************************************
*/

/* *******************************************************
/  printDigits adds an extra 0 if the integer is below 10
*/
String printDigits(int digits){
  // utility function for digital clock display: prints colon and leading 0
  String returnval = "";
  if(digits < 10)
    returnval += "0";
  returnval += digits; 
         
  return returnval; 
}
/* *******************************************************
*/

/* *******************************************************
/  updateEncoder is the function that reacts to the rotary encoder interrupts
*/
void updateEncoder(){
  int MSB = digitalRead(encoderPin1); //MSB = most significant bit
  int LSB = digitalRead(encoderPin2); //LSB = least significant bit

  int encoded = (MSB << 1) |LSB; //converting the 2 pin value to single number
  int sum  = (lastEncoded << 2) | encoded; //adding it to the previous encoded value

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderValue --;
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderValue ++;

  lastEncoded = encoded; //store this value for next time
}
/* *******************************************************
*/
