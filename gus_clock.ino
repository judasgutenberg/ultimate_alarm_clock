//CLOCK DESIGNED FOR ATMEGA328, 4 MAX7219s & RTC
//Gus Mueller, Oct 1, 2015
//http://asecular.com
//lots of commands can be issued via serial terminal 
//but this is designed mostly as a clock that can be configured
//with a small keypad (i'm using a cheap Chinese 16 key keypad)
//some of this code comes from
//https://code.google.com/p/arudino-maxmatrix-library/wiki/Example_Display_Scrolling_Text

#include <MaxMatrix.h>
#include <avr/pgmspace.h>
#include <Keypad.h>
 
 

//audio stuff:
int resetPin = 13;  // The pin number of the reset pin.
int clockPin = 14;  // The pin number of the clock pin.
int dataPin = 15;  // The pin number of the data pin.
int busyPin = 16;  // The pin number of the busy pin.
//Wtv020sd16p wtv020sd16p(resetPin,clockPin,dataPin,busyPin);

//serial-related clock code:
#include <Wire.h>
#include <EEPROM.h>
#define DS1307_ADDRESS 0x68 //i actually use a DS3231, whose clock interface is identical
#define SER_IN_BUFFER_SIZE 18
#define PERM_DATA_METHOD 0
#define EEPROM_LOW_MEM_ADDRESS 0x50 //EEPROM on RTC board
#define EEPROM_HIGH_MEM_ADDRESS 0x50
#define PERM_DATA_METHOD 0 //0 is Arduino's built-in EEPROM. couldn't get EEPROM on RTC to work.
#define BASECHAR 16 
#define CURSORCOUNT 5 //number of cursor widths 
#define SPRITECOUNT 1 //number of cursor widths 
#define SCREENCHARS  9 
#define ALARMTOP 10 //number of alarms in EEPROM, starting at locAlarms
#define BASEOPTION 20 //where we come in when we begin programming from button ui
#define COUNTDOWNSET 200
#define COUNTUPSET 201
/*
//salvaged keypad:
const byte ROWS = 4; // Four rows
const byte COLS = 3; // 8 columns
//i'm using a keypad from an old alarm clock, which has a row among the columns:
byte rowPins[ROWS] = { 5, 6, 7, 10};
byte colPins[COLS] = { 8, 9, 11}; 
char keys[ROWS][COLS] = {
  {'L','R','X',},
  {'5','S','7',},
  {'9','U','B',},
  {'X','D','Z', },
}; 
*/

 
//conventional 4 X 4 membrane keypad
PROGMEM const  byte ROWS = 4;  
PROGMEM const  byte COLS = 4;  
byte rowPins[ROWS] = {5,6,7,8};
byte colPins[COLS] = {9, 10, 11, 12}; 
const unsigned char keys[ROWS][COLS] = {
  {'0','1','M', 'U' },
  {'4','3','C', 'D'},
  {'7','8','!', 'X'},
  {'S','0','L', 'R'},
};

const char *weekDays[] ={"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

byte displayMode=0;
bool programMode=false;
int programCursor=1;
bool programEvenThrough=false;
unsigned long timeProgrammingBegan;
unsigned int programmingAllowedTime=4000;
unsigned int inModeAllowedTime=14000;
unsigned int currentAllowedTime=programmingAllowedTime;
bool justEnteredProgramMode=false;
byte editOption=20; //used to be 0, made 20 so i can go the other way from home without negative numbers
//WHERE TO DROP THE CURSOR WHEN SWITCHING BETWEEN "CONFIG PAGES" USING THE BUTTONS
const byte landingCursorPos[]={1,4,4,3,4,4};
unsigned long resetAt=0;
int countingIncrement=0;
unsigned long millisAtCountDownStart;
unsigned long millisAtCountUpStart;
unsigned long baseCountDown;
unsigned long baseCountUp;

Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

//Too complicated to explain -- basically simplifies the parsing of date/time info
//when setting the RTC from the serial terminal:
const byte  clocksettingregime[4][7] = 
{  
  {0,1,2,3,4,5,6},
  {8,8,8,0,8,8,8},
  {2,1,0,8,8,8,8},
  {8,8,8,8,2,1,0},
}; 
char spaceDelimiter=' ';
unsigned long globalTimestamp;
byte serIn[SER_IN_BUFFER_SIZE];
bool handled=false;
char printMode='r';
byte serCount=0;
char tempMessage[10];
bool doDisplayTempMessage=false;
unsigned long timeTempMessageBegin;
unsigned int tempMessageTime=4000;
bool displaySeconds=true;
bool displayMilitary=false;
byte upperTwoYearsDigits=20; //theoretically this thing can be made to work until the year 9999
char timeString[SCREENCHARS];
char dateString[SCREENCHARS];
char yearString[SCREENCHARS];
char programmerString[SCREENCHARS];
char displayString[SCREENCHARS];
char* countDownString;
char* countUpString;

byte intensity;
byte explosionSound;
byte bellSound;
byte chimeOnHour;
byte oldHour=255;
byte oldMinute=255;
byte chimesToDo=0;
byte chimeBegin=9;
byte chimeEnd=22;
byte loudness=20;
unsigned long countDownTime;

byte halfhourChime=6;

unsigned long countDown=0;
unsigned long countUp=0;
unsigned long lastCountDownMillis=0;
unsigned long lastCountUpMillis=0;

byte subMode=0;


//locations in Arduino EEPROM:
PROGMEM const byte locMessageTime=0;
PROGMEM const byte locDisplaySeconds=2; //this & the next item could be all in one byte but there's plenty EEPROM so why bother?
PROGMEM const byte locDisplayMilitary=3;
PROGMEM const byte locUpperTwoYearDigits=4; //points to data that would be 20 in my lifetime but it could be 99
PROGMEM const byte locIntensity=5;
PROGMEM const byte locExplosionSound=6;
PROGMEM const byte locBellSound=7;
PROGMEM const byte locChimeOnHour=8;
PROGMEM const byte locChimeBegin=9;
PROGMEM const byte locChimeEnd=10;
PROGMEM const byte locHalfhourChime=11;
PROGMEM const byte locLoudness=12;
PROGMEM const byte locCountDownTime=16;
PROGMEM const byte locAlarms=100;

byte edgeCasesHigh[]={31,57}; //ascii values of all the highest "numbers" in the character set
byte edgeCasesLow[]={22,48};  //ascii values of all the lowest "numbers" in the character set

PROGMEM  const unsigned char  CH[] = {
//how about some fun sprites:
5, 8, B01100000, B01011000, B01111110, B00100101, B00100100, //  'camel'
 //then some cursors of different widths:
1, 8, B10000000, B00000000, B00000000, B00000000, B00000000, //  'one pixel wide underscore'
2, 8, B10000000, B10000000, B00000000, B00000000, B00000000, //  'two pixel wide underscore' 
3, 8, B10000000, B10000000, B10000000, B00000000, B00000000, //  'three pixel wide underscore' 
4, 8, B10000000, B10000000, B10000000, B10000000, B00000000, //  'four pixel wide underscore'
5, 8, B10000000, B10000000, B10000000, B10000000, B10000000, //  'five pixel wide underscore'
//now some tiny numbers:
2, 8, B01111100, B01111100, B00000000, B00000000, B00000000, //  '0'
2, 8, B01111100, B00000000, B00000000, B00000000, B00000000, //  '1'
2, 8, B01100100, B01011100, B00000000, B00000000, B00000000, //  '2'
2, 8, B01010100, B01101100, B00000000, B00000000, B00000000, //  '3'
2, 8, B00011100, B01111000, B00000000, B00000000, B00000000, //  '4'
2, 8, B01011100, B01100100, B00000000, B00000000, B00000000, //  '5'
2, 8, B01111000, B01100100, B00000000, B00000000, B00000000, //  '6'
2, 8, B00000100, B01111100, B00000000, B00000000, B00000000, //  '7'
2, 8, B01101100, B01111100, B00000000, B00000000, B00000000, //  '8'
2, 8, B01001100, B00111100, B00000000, B00000000, B00000000, //  '9'
//the usual ASCII set:
3, 8, B00000000, B00000000, B00000000, B00000000, B00000000, // space
1, 8, B01011111, B00000000, B00000000, B00000000, B00000000, // !
3, 8, B00000011, B00000000, B00000011, B00000000, B00000000, // "
5, 8, B00010100, B00111110, B00010100, B00111110, B00010100, // #
4, 8, B00100100, B01101010, B00101011, B00010010, B00000000, // $
5, 8, B01100011, B00010011, B00001000, B01100100, B01100011, // %
5, 8, B00110110, B01001001, B01010110, B00100000, B01010000, // &
1, 8, B00000011, B00000000, B00000000, B00000000, B00000000, // '
3, 8, B00011100, B00100010, B01000001, B00000000, B00000000, // (
3, 8, B01000001, B00100010, B00011100, B00000000, B00000000, // )
5, 8, B00101000, B00011000, B00001110, B00011000, B00101000, // *
5, 8, B00001000, B00001000, B00111110, B00001000, B00001000, // +
2, 8, B10110000, B01110000, B00000000, B00000000, B00000000, // ,
4, 8, B00001000, B00001000, B00001000, B00001000, B00000000, // -
2, 8, B01100000, B01100000, B00000000, B00000000, B00000000, // .
4, 8, B01100000, B00011000, B00000110, B00000001, B00000000, // /
4, 8, B00111110, B01000001, B01000001, B00111110, B00000000, // 0
3, 8, B00000000, B01111111, B00000000, B00000000, B00000000, // 1
4, 8, B01100010, B01010001, B01001001, B01000110, B00000000, // 2
4, 8, B00100010, B01000001, B01001001, B00110110, B00000000, // 3
4, 8, B00011111, B00010000, B00010000, B01111111, B00000000, // 4
4, 8, B00100111, B01000101, B01000101, B00111001, B00000000, // 5
4, 8, B00111110, B01001001, B01001001, B00110000, B00000000, // 6
4, 8, B01100001, B00010001, B00001001, B00000111, B00000000, // 7
4, 8, B00110110, B01001001, B01001001, B00110110, B00000000, // 8
4, 8, B00000110, B01001001, B01001001, B00111110, B00000000, // 9
2, 8, B01010000, B00000000, B00000000, B00000000, B00000000, // :
2, 8, B10000000, B01010000, B00000000, B00000000, B00000000, // ;
3, 8, B00010000, B00101000, B01000100, B00000000, B00000000, // <
3, 8, B00010100, B00010100, B00010100, B00000000, B00000000, // =
3, 8, B01000100, B00101000, B00010000, B00000000, B00000000, // >
4, 8, B00000010, B01011001, B00001001, B00000110, B00000000, // ?
5, 8, B00111110, B01001001, B01010101, B01011101, B00001110, // @
4, 8, B01111110, B00010001, B00010001, B01111110, B00000000, // A
4, 8, B01111111, B01001001, B01001001, B00110110, B00000000, // B
4, 8, B00111110, B01000001, B01000001, B00100010, B00000000, // C
4, 8, B01111111, B01000001, B01000001, B00111110, B00000000, // D
4, 8, B01111111, B01001001, B01001001, B01000001, B00000000, // E
4, 8, B01111111, B00001001, B00001001, B00000001, B00000000, // F
4, 8, B00111110, B01000001, B01001001, B01111010, B00000000, // G
4, 8, B01111111, B00001000, B00001000, B01111111, B00000000, // H
3, 8, B01000001, B01111111, B01000001, B00000000, B00000000, // I
4, 8, B00110000, B01000000, B01000001, B00111111, B00000000, // J
4, 8, B01111111, B00001000, B00010100, B01100011, B00000000, // K
4, 8, B01111111, B01000000, B01000000, B01000000, B00000000, // L
5, 8, B01111111, B00000010, B00001100, B00000010, B01111111, // M
5, 8, B01111111, B00000100, B00001000, B00010000, B01111111, // N
4, 8, B00111110, B01000001, B01000001, B00111110, B00000000, // O
4, 8, B01111111, B00001001, B00001001, B00000110, B00000000, // P
4, 8, B00111110, B01000001, B01000001, B10111110, B00000000, // Q
4, 8, B01111111, B00001001, B00001001, B01110110, B00000000, // R
4, 8, B01000110, B01001001, B01001001, B00110010, B00000000, // S
5, 8, B00000001, B00000001, B01111111, B00000001, B00000001, // T
4, 8, B00111111, B01000000, B01000000, B00111111, B00000000, // U
5, 8, B00001111, B00110000, B01000000, B00110000, B00001111, // V
5, 8, B00111111, B01000000, B00111000, B01000000, B00111111, // W
5, 8, B01100011, B00010100, B00001000, B00010100, B01100011, // X
5, 8, B00000111, B00001000, B01110000, B00001000, B00000111, // Y
4, 8, B01100001, B01010001, B01001001, B01000111, B00000000, // Z
2, 8, B01111111, B01000001, B00000000, B00000000, B00000000, // [
4, 8, B00000001, B00000110, B00011000, B01100000, B00000000, // \ backslash
2, 8, B01000001, B01111111, B00000000, B00000000, B00000000, // ]
3, 8, B00000010, B00000001, B00000010, B00000000, B00000000, // hat
4, 8, B01000000, B01000000, B01000000, B01000000, B00000000, // _
2, 8, B00000001, B00000010, B00000000, B00000000, B00000000, // `
4, 8, B00100000, B01010100, B01010100, B01111000, B00000000, // a
4, 8, B01111111, B01000100, B01000100, B00111000, B00000000, // b
4, 8, B00111000, B01000100, B01000100, B00101000, B00000000, // c
4, 8, B00111000, B01000100, B01000100, B01111111, B00000000, // d
4, 8, B00111000, B01010100, B01010100, B00011000, B00000000, // e
3, 8, B00000100, B01111110, B00000101, B00000000, B00000000, // f
4, 8, B10011000, B10100100, B10100100, B01111000, B00000000, // g
4, 8, B01111111, B00000100, B00000100, B01111000, B00000000, // h
3, 8, B01000100, B01111101, B01000000, B00000000, B00000000, // i
4, 8, B01000000, B10000000, B10000100, B01111101, B00000000, // j

4, 8, B01111111, B00010000, B00101000, B01000100, B00000000, // k
3, 8, B01000001, B01111111, B01000000, B00000000, B00000000, // l
5, 8, B01111100, B00000100, B01111100, B00000100, B01111000, // m
4, 8, B01111100, B00000100, B00000100, B01111000, B00000000, // n
4, 8, B00111000, B01000100, B01000100, B00111000, B00000000, // o
4, 8, B11111100, B00100100, B00100100, B00011000, B00000000, // p
4, 8, B00011000, B00100100, B00100100, B11111100, B00000000, // q
4, 8, B01111100, B00001000, B00000100, B00000100, B00000000, // r
4, 8, B01001000, B01010100, B01010100, B00100100, B00000000, // s
3, 8, B00000100, B00111111, B01000100, B00000000, B00000000, // t
4, 8, B00111100, B01000000, B01000000, B01111100, B00000000, // u
5, 8, B00011100, B00100000, B01000000, B00100000, B00011100, // v
5, 8, B00111100, B01000000, B00111100, B01000000, B00111100, // w
5, 8, B01000100, B00101000, B00010000, B00101000, B01000100, // x
4, 8, B10011100, B10100000, B10100000, B01111100, B00000000, // y
3, 8, B01100100, B01010100, B01001100, B00000000, B00000000, // z
3, 8, B00001000, B00110110, B01000001, B00000000, B00000000, // {
1, 8, B01111111, B00000000, B00000000, B00000000, B00000000, // |
3, 8, B01000001, B00110110, B00001000, B00000000, B00000000, // }
4, 8, B00001000, B00000100, B00001000, B00000100, B00000000, // ~

};
 
int data = 2;    // DIN pin of MAX7219 module
int load = 3;    // CS pin of MAX7219 module
int clockPinMax = 4;  // CLK pin of MAX7219 module
int maxInUse = 4;  //how many MAX7219 are connected
MaxMatrix m(data, load, clockPinMax, maxInUse); // define Library
byte buffer[10];
 
 
void setup(){
  //wtv020sd16p.reset();
  Wire.begin();
  m.init(); // module MAX7219
 
  Serial.begin(19200);
  
  tempMessageTime = getint(locMessageTime,PERM_DATA_METHOD);
  displaySeconds = getbyte(locDisplaySeconds,PERM_DATA_METHOD);
  displayMilitary = getbyte(locDisplayMilitary,PERM_DATA_METHOD);
  intensity = getbyte(locIntensity,PERM_DATA_METHOD);
  explosionSound = getbyte(locExplosionSound,PERM_DATA_METHOD);
  bellSound = getbyte(locBellSound,PERM_DATA_METHOD);
  chimeOnHour = getbyte(locChimeOnHour,PERM_DATA_METHOD);
  chimeBegin = getbyte(locChimeBegin,PERM_DATA_METHOD);
  chimeEnd = getbyte(locChimeEnd,PERM_DATA_METHOD);

  halfhourChime = getbyte(locHalfhourChime,PERM_DATA_METHOD);
  loudness = getbyte(locLoudness,PERM_DATA_METHOD);
  setLoudness(loudness);

  countDownTime = getlong(locCountDownTime,PERM_DATA_METHOD);
  
  m.setIntensity(intensity);
  //Serial.print("military?");
  //Serial.print(displayMilitary);
  //Serial.println("");
  
  upperTwoYearsDigits = getbyte(locUpperTwoYearDigits,PERM_DATA_METHOD);
  //Serial.println("first two: " );
  //Serial.println(upperTwoYearsDigits);
  if(upperTwoYearsDigits>99)
  {
    upperTwoYearsDigits=20; //default it to 20 if it's not a valid number
    storebyte(locUpperTwoYearDigits, upperTwoYearsDigits, PERM_DATA_METHOD);
  }
 
}
 
void loop(){
  //Serial.print("loop");
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  if(resetAt<millis() && resetAt>0)
  {
    //wanted to clear the screen of glitches in the future in some cases
    clearScreen();
    resetAt=0;
  }
  
  if(minute!=oldMinute  && second==0)
  {
    if(minute==30)
    {
       if(chimeOnHour && hour>=chimeBegin && hour<=chimeEnd)
       {
          playVoice(halfhourChime);
       }
      
    }
    scanAlarmsAndRingOnHits(year, month, dayOfMonth, hour, minute);
 
    oldMinute=minute;
  }
  if(hour!=oldHour && minute==0  && second==0 )
  {
    clearScreen(); //there can be garbage left over on the screen from hours with more digits
    oldHour=hour;
    /*
    Serial.print("chime on hour: ");
    Serial.print(chimeOnHour);
    Serial.print(chimeBegin);
    Serial.print(" chimeend: ");
    Serial.print(chimeEnd);
    Serial.println("");
    */
    
    if(chimeOnHour && hour>=chimeBegin && hour<=chimeEnd) //assign chimes to do
    {
      //Serial.println("chime time");
      chimesToDo=hour;
      if(chimesToDo>12) //don't chime more than 12 times
      {
        chimesToDo=chimesToDo-12;
      }
      else if(chimesToDo==0) //don't chime 0 times at midnight
      {
        chimesToDo=12;
      }
    }
    
  }
  if(chimesToDo>0  && digitalRead(busyPin)==LOW && chimeOnHour) //do chimes asynchronously and don't interrupt ongoing chimes
  {
    Serial.println(chimesToDo);
    delay(10); //some of the chimes don't happen unless i put a delay in here
    chime();
    chimesToDo--;
  }
  globalTimestamp = currentTimestamp();
  char key = kpd.getKey();
  if(key)  // Check for a valid key.
  {
    if(key=='L'  || key=='R' || key=='U'  || key=='D' || key=='M')
    {
      if(!programMode)
      {
        justEnteredProgramMode=true;
      }
      
      programMode=true;
      if(key=='M' &&  (displayMode==COUNTUPSET ||  displayMode==COUNTDOWNSET))
      {
        //Serial.print("next display mode, currently: ");
        //Serial.print(displayMode);
        //Serial.print("->");
        nextDisplayMode();
        //Serial.print(displayMode);
        //Serial.println("");
        
      }
      timeProgrammingBegan=millis();
      
    }
    //Serial.println(key);
  }
  if(key=='S')
  {
    explosion();
  }
  else if(key=='C')
  {
    countingIncrement=0;
    if(displayMode==COUNTUPSET)
    { 
      countUp=0;
      baseCountUp=countUp;
    }
    else if(countDown>0)
    {
      countDown=countDownTime;
      baseCountDown=countDown;
    }
  }
  else if(key=='!')
  {
    //clearScreen();

    
    if(displayMode==0) //show date if you hit the !
    {
      
      subMode++;
      if(subMode>3)
      {
        subMode=0;
      }
      clearTempMessage();
      if(subMode==0)
      {
        doDisplayTempMessage=false;
        //displayCurrentTime(programMode);
      }
      else if(subMode==1)
      {
        strncpy(tempMessage, dateString, 5); //how you overwrite a string
        doDisplayTempMessage=true;
        timeTempMessageBegin=millis();
      }
     else if(subMode==2)
      {
        //Serial.println("X");
        //Serial.println(dayOfWeek);
        //Serial.println(weekDays[dayOfWeek-1]);
        //Serial.println("Y");
        strncpy(tempMessage, weekDays[dayOfWeek-1], 3); //how you overwrite a string
        doDisplayTempMessage=true;
        timeTempMessageBegin=millis();
      }
      else if(subMode==3)
      {
        strncpy(tempMessage, yearString, 4); //how you overwrite a string
        doDisplayTempMessage=true;
        timeTempMessageBegin=millis();
      }
      
      

    }
    else if(displayMode==COUNTUPSET)
    {
      //saveProgrammedData();
 
      if(countingIncrement==1)
      {
        countingIncrement=0;
        baseCountUp=countUp;
      }
      else
      {
        millisAtCountUpStart=millis();
        baseCountUp=countUp;
        countingIncrement=1;
      }
      //lastCountUpMillis=0;
      //countUp=0;
      programMode=false;
    }
    else if(editOption==COUNTDOWNSET || displayMode==COUNTDOWNSET)
    {
      //saveProgrammedData();
      //countDown=countDownTime;
      programMode=false;
      if(countingIncrement==-1)
      {
        countingIncrement=0;
        baseCountDown=countDown;
      }
      else
      {
        if(editOption==COUNTDOWNSET)
        {
          saveProgrammedData();
          countDown=countDownTime;
        }
        baseCountDown=countDown;
        millisAtCountDownStart=millis();
        countingIncrement=-1;
      }
    }
 
  }
  else if(key=='5')
  {
    clearScreen();
    if(editOption==COUNTDOWNSET)
    {
      saveProgrammedData();
      countDown=countDownTime/10;
      programMode=false;
    }
  }
  else if((byte)key>47 && (byte)key<59  && key!='M')
  {

    playVoice((byte)key-48);
  }

  if(timeProgrammingBegan + currentAllowedTime < millis())
  {
    if(programMode)
    {
      clearScreen();
    }
    programMode=false;
    justEnteredProgramMode=false;
    if(key!='M')
    {
      editOption=20;
    }
  }
  parseSerial();
  /*
  Serial.print(countingIncrement);
  Serial.print("*");
  Serial.print(baseCountDown);
  Serial.print("*");
  Serial.print(countDown);
  Serial.println(" ");
  
  Serial.print(editOption);
  Serial.print("*");
  Serial.print(displayMode);
  Serial.println("");
  */
  if(programMode)
  {
    doProgramMode(key);
  }
  else if(displayMode==COUNTUPSET)
  {
    //if(millis()-lastCountUpMillis>=100)
    {
      //old way, less accurate:
      //countUp=countUp+countingIncrement;
      //new way:
      countUp= baseCountUp + ((millis() - millisAtCountUpStart)* countingIncrement) /100;
      //Serial.print(countingIncrement);
      //Serial.print(":");
      //Serial.print(countUp);
      //Serial.println(" ");
      lastCountUpMillis=millis();
      printStringStatic(deciSecondsToMinutesSeconds(countUp),0);
    }
  }
  else if(displayMode==COUNTDOWNSET)
  {
    if(countDown<2 && countingIncrement!=0)
    {
      explosion();

      countingIncrement=0;
      baseCountDown=0;
      displayMode=0;
    }
    //if(millis()-lastCountDownMillis>=100)
    {
      //Serial.print(countingIncrement);
      //Serial.print("*");
      //Serial.print(countDown);
      //Serial.println(" ");
      //old way, less accurate:
      //countDown=countDown+countingIncrement;
      //new way:
 
      countDown=(baseCountDown - (((millis() - millisAtCountDownStart)* (-1*countingIncrement)) /100));
   
      //Serial.println(countDown);
      lastCountDownMillis=millis();
      printStringStatic(deciSecondsToMinutesSeconds(countDown),0);
    }
    
    //const char * string, char pad, size_t bytes)
    
  }
  else if(doDisplayTempMessage)
  {
    //Serial.println("do display");
    //Serial.print(millis());
    //Serial.print(" ");
    //Serial.print(timeTempMessageBegin + tempMessageTime);
    //Serial.println(" ");
    if(millis()  < timeTempMessageBegin + tempMessageTime)
    {
      //Serial.println("actual display");
      printStringStatic(tempMessage,0);
    }
    else
    {
      doDisplayTempMessage=false;
      clearScreen();
    }
    
  }
  else
  {
 
    displayCurrentTime(programMode);
  }
  int peak =0;
  int thisRead=0;
  //i was experimenting with sound-sensing code and such here to maybe trigger other displays
  /*
  for(int i=0; i<40; i++)
  {
    thisRead=analogRead(6);
    if(thisRead>peak)
    {
      peak=thisRead;
      
    }
  }
  */
  //Serial.println(peak);


}

void initiateCount(int increment)
{ 
  if(increment<0)
  {
    millisAtCountDownStart=millis(); 
    baseCountDown=countDownTime;
    countDown=baseCountDown;

    //baseCountDown=countDown;
    //millisAtCountDownStart=millis();
    //countingIncrement=-1;
        
    displayMode=COUNTDOWNSET;  
  }
  else if(increment>0)
  {
    millisAtCountUpStart=millis();
    baseCountUp=countUp;
    displayMode=COUNTUPSET;
  }
  countingIncrement=increment;
}


void printAlarmSetting(byte alarmNumber)
{
  byte thisChime, thisYear, thisMonth, thisDayOfMonth, thisHour, thisMinute;
  thisChime=getbyte(locAlarms + alarmNumber*6+0,PERM_DATA_METHOD);
  thisYear=getbyte(locAlarms + alarmNumber*6+1,PERM_DATA_METHOD);
  thisMonth=getbyte(locAlarms + alarmNumber*6+2,PERM_DATA_METHOD);
  thisDayOfMonth=getbyte(locAlarms + alarmNumber*6+3,PERM_DATA_METHOD);
  thisHour=getbyte(locAlarms + alarmNumber*6+4,PERM_DATA_METHOD);
  thisMinute=getbyte(locAlarms + alarmNumber*6+5,PERM_DATA_METHOD);
  Serial.print(F("Alarm #"));
  Serial.print((int)alarmNumber);
  Serial.print(F(":"));
  Serial.println("");
  Serial.print(F("voice: "));
  Serial.print((int)thisChime);
  Serial.println("");
  Serial.print(F("yy/mo/dd hh:mi: "));
  Serial.print((int)thisYear);
  Serial.print("/");
  Serial.print((int)thisMonth);
  Serial.print("/");
  Serial.print((int)thisDayOfMonth);
  Serial.print(" ");
  Serial.print((int)thisHour);
  Serial.print(F(":"));
  Serial.print((int)thisMinute);
  Serial.println("");
}

void scanAlarmsAndRingOnHits(byte year, byte month, byte dayOfMonth, byte hour, byte minute)
{
  //data structure for alarms stored in EEPROM:
  //starting at byte locAlarms (100), byte zero is alarm sound, year, month, dayOfMonth, hour, minute 
  //over and over until ALARMTOP
  byte thisChime, thisYear, thisMonth, thisDayOfMonth, thisHour, thisMinute;
  bool failForThisAlarm=false;
  for(byte i=0; i<ALARMTOP; i++)
  {
    failForThisAlarm=false;
    thisChime=getbyte(locAlarms + i*6+0,PERM_DATA_METHOD);
    thisYear=getbyte(locAlarms + i*6+1,PERM_DATA_METHOD);
    thisMonth=getbyte(locAlarms + i*6+2,PERM_DATA_METHOD);
    thisDayOfMonth=getbyte(locAlarms + i*6+3,PERM_DATA_METHOD);
    thisHour=getbyte(locAlarms + i*6+4,PERM_DATA_METHOD);
    thisMinute=getbyte(locAlarms + i*6+5,PERM_DATA_METHOD);
    if(thisChime==255)
    {
      failForThisAlarm=true;
    }
    if(thisYear>0)
    {
      if(thisYear!=year && thisYear<100)
      {
        failForThisAlarm=true;
      }
    }
    if(thisMonth>0  && thisMonth<100)
    {
      if(thisMonth!=month)
      {
        failForThisAlarm=true;
      }
    }
    if(thisDayOfMonth>0 && thisDayOfMonth<100)
    {
      if(thisDayOfMonth!=dayOfMonth)
      {
        failForThisAlarm=true;
      }
    }

    if(thisHour!=hour)
    {
      failForThisAlarm=true;
    }
    if(thisMinute!=minute)
    {
      failForThisAlarm=true;
    }
   if(!failForThisAlarm)
   {
      //Serial.println("Sounding the alarm!");
      playVoice(thisChime);
   }
  }
}


void chime()
{
 
  playVoice(bellSound);
    
}

void clearTempMessage()
{
 
  for(byte i=0; i<10; i++)
  {
    tempMessage[i]= ' ';
  }
}

void clearScreen()
{
  char stringToClear[SCREENCHARS];
  currentAllowedTime=programmingAllowedTime;
  for(byte i=0; i<SCREENCHARS; i++)
  {
    dateString[i]= ' ';
    countDownString[i] = ' ';
    yearString[i]= ' ';
    stringToClear[i]= ' ';
  }
  countDownString[SCREENCHARS-1]=0;
  dateString[SCREENCHARS-1]=0;
  yearString[SCREENCHARS-1]=0;
  stringToClear[SCREENCHARS-1]=0;
  printStringStatic(stringToClear,0);
}

void clearProgrammingScreen()
{
  for(byte i=0; i<SCREENCHARS; i++)
  {
    programmerString[i]=(char)32;
  }
}


void (*resetClock) (void) = 0; 
 

int whichNumericRange(char in)
{
  //depends on globals edgeCasesHigh and edgeCasesLow
  int numericRange=-1; //which numeric range is our char in?
  for(byte i=0; i<sizeof(edgeCasesLow); i++)
  {
    if((byte)in<=edgeCasesHigh[i] && (byte)in>=edgeCasesLow[i])
    {
      numericRange=i;
    }
  }
  return numericRange;
}

byte charInPossibleRangeToNumber(char in)
{
  //byte edgeCasesHigh[]={31,57}; //ascii values of all the highest "numbers" in the character set
  //byte edgeCasesLow[]={22,48};  //ascii values of all the lowest "numbers" in the character set

  byte numericRange=whichNumericRange(in);
  byte lowEnd;
  byte highEnd;
  if(numericRange>-1)
  {
    return (byte)(in-edgeCasesLow[numericRange]); 
  }
  return 32; //basically, NaN
}

byte numberToCharInRange(byte in, byte numericRange)
{
  if(numericRange<sizeof(edgeCasesLow)  && in<10)
  {
    return (byte)(in+edgeCasesLow[numericRange]);
  }
  return 32; //NaN
}

void saveProgrammedData()
{
   
    char thisChar;
    int charRange;
    byte thisDigit;
    bool savedHours=false;
    bool savedMinutes=false;
    bool savedSeconds=false;
    bool savedDays=false;
    bool savedMonths=false;
    bool savedYears=false;
    bool savedFirstTwoYears=false;
    int hours=-1;
    int minutes=-1;
    int seconds=-1;
    int days=-1;
    int months=-1;
    int years=-1;
    int firstTwoYears=-1;
    int digitWhereSavingBegan=-1;
    if(editOption==COUNTDOWNSET)
    {

      countDownTime=parseStringToDeciseconds(programmerString);
      baseCountDown=countDownTime;
      Serial.print(F("CountDown start set to "));
      Serial.print(countDownTime);
      Serial.print(" deciseconds");
      Serial.println();
      storelong(locCountDownTime, countDownTime, PERM_DATA_METHOD);
      //initiateCount(-1);
      return;
    }
 
    
    for(byte i=0; i<SCREENCHARS; i++)
    {
 
      thisChar=programmerString[i];
      thisDigit=charInPossibleRangeToNumber(thisChar);
      charRange=whichNumericRange(thisChar);
      /*
      Serial.print(thisChar);
      Serial.print((int)i);
      Serial.print(" ");
      Serial.print((int)thisChar);
      Serial.print(" ");
      Serial.print((int)charRange);
      Serial.print(" ");
      Serial.print((int)thisDigit);
      Serial.println(" ");
      */

      
        if(thisDigit<10  && charRange>-1)
        {
          if(editOption==20)
          {
            if(!savedHours)
            {
              if(digitWhereSavingBegan==-1)
              {
                digitWhereSavingBegan=i;
              }
              if(hours==-1)
              {
                 if(programmerString[i+1]==':') //look ahead for a colon
                 {
                  hours=thisDigit;
                  savedHours=true;
                 }
                 else
                 {
                  hours=10*thisDigit;
                 }
              }
              else
              {
                hours=hours+thisDigit;
                savedHours=true;
              }
              //for some reason this has to be here or the time doesnt save
              //and delay doesnt work!!
              /*
              Serial.print("hours: ");
              //delay(100);
              Serial.print((int)hours);
              Serial.println("");
              */
              
            }
            else if(!savedMinutes)
            {
     
              if(minutes==-1)
              {
                 minutes=10*thisDigit;
              }
              else
              {
                minutes=minutes+thisDigit;
                savedMinutes=true;
              }
              /*
              Serial.print("Minutes: ");
              Serial.print((int)minutes);
              Serial.println("");
              */
            }
            else if(!savedSeconds)
            {
     
              if(seconds==-1)
              {
                 seconds=10*thisDigit;
              }
              else
              {
                seconds=seconds+thisDigit;
                savedSeconds=true;
                
              }
              /*
              Serial.print("Seconds: ");
              Serial.print((int)seconds);
              Serial.println("");
              */
               
            }
          }
        else if (editOption==22)
        {
            if(!savedMonths)
            {
  
              if(months==-1)
              {
                 if(programmerString[i+1]=='-') //look ahead for a colon
                 {
                  months=thisDigit;
                  savedMonths=true;
                 }
                 else
                 {
                  months=10*thisDigit;
                 }
              }
              else
              {
                months=months+thisDigit;
                savedMonths=true;
              }
              /*
              Serial.print("months: ");
              //delay(100);
              Serial.print((int)months);
              Serial.println("");
              */
            }
            else if(!savedDays)
            {
  
              if(days==-1)
              {
    
                 days=10*thisDigit;
                 
              }
              else
              {
                days=days+thisDigit;
                savedDays=true;
              }
              /*
              Serial.print("days: ");
              //delay(100);
              Serial.print((int)days);
              Serial.println("");
              */
            }
        }
        else if (editOption==23)
        {
            //savedFirstTwoYears
            //firstTwoYears
            if(!savedFirstTwoYears)
            {
  
              if(firstTwoYears==-1)
              {
                  
                firstTwoYears=10*thisDigit;
                  
              }
              else
              {
                firstTwoYears=firstTwoYears+thisDigit;
                savedFirstTwoYears=true;
              }
              /*
              Serial.print("first 2 years: ");
              Serial.print((int)firstTwoYears);
              Serial.println("");
              */
              
            }
            else if(!savedYears)
            {
              if(years==-1)
              {
                years=10*thisDigit;
              }
              else
              {
                years=years+thisDigit;
                savedYears=true;
              }
              /*
              Serial.print("years: ");
              Serial.print((int)years);
              Serial.println("");
              */
              
            }
        }
      }

    }//for
    //Serial.print("saved seconds? ");
    //Serial.print((int) savedSeconds);
    //Serial.println("");
    byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
    getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
    if(savedSeconds)   //valid seconds, so save time if everything else is valid   
    {
      //Serial.println("possible save of time");
     
      if(seconds<60 && minutes < 60 && hours <24  && editOption==20)
      {
        setDateDs1307(seconds, minutes, hours, dayOfWeek, dayOfMonth, month, year);
        //Serial.println("Saved time from device UI");
      }

    }
    else if(savedDays  && days<32 && months < 13 && editOption==22)
    {

      setDateDs1307(second, minute, hour, dayOfWeek, days, months, year);
      //Serial.println("Saved date from device UI");
    }
    else if(savedFirstTwoYears  && savedYears && editOption==23)
    {
       setDateDs1307(second, minute, hour, dayOfWeek, dayOfMonth, month, years);
       storebyte(locUpperTwoYearDigits, firstTwoYears, PERM_DATA_METHOD);
       upperTwoYearsDigits=firstTwoYears;
       //Serial.println("Saved year from device UI");
    }
    
    programMode=false;
    clearScreen();
    editOption=20;
}

void doneWithProgramming()
{
  programMode=false;
  resetAt=millis()+200;
  //clearScreen();
  //displayCurrentTime(false);
}

void nextDisplayMode()
{
    //round-robin through the modes: normal, countDown, countUp
    if(displayMode==0)
    {
      setDisplayMode(COUNTDOWNSET);
    }
    else if(displayMode==COUNTDOWNSET )
    {
      setDisplayMode(COUNTUPSET);
    }
    else if(displayMode==COUNTUPSET )
    {
      setDisplayMode(0);
    }
}

void setDisplayMode(byte mode)
{
    if(mode==COUNTDOWNSET)
    {
      countingIncrement=0;
      displayMode=COUNTDOWNSET;
      editOption=COUNTDOWNSET;
      currentAllowedTime=inModeAllowedTime;
    }
    else if(mode==COUNTUPSET )
    {
      programMode=false;
      countingIncrement=0;
      displayMode=COUNTUPSET;
      //editOption=COUNTUPSET;
      //currentAllowedTime=inModeAllowedTime;
      countDown=0;
    }
    else if(mode==0 )
    {
      countingIncrement=0;
      displayMode=0;
      programMode=false;
      editOption=0;
      clearScreen();
    }
}

void doProgramMode(char key)
{
  //Serial.println(millis());
  char charInQuestion;
  char nextCharInQuestion2 =(char)0;
  int charRange=-1;
  byte digitInQuestion=32;
  byte i;
  //char displayString[SCREENCHARS]; //had to make is 6 chars larger because of defective ram in my atmega328
  //hmm, don't need it any more.  fixing this meant just making sure the global displayString ended with a char(0) like a proper C-string
  char previousChar=(char)0;
  char nextChar=(char)0;
  //displayCurrentTime(true);
  charInQuestion=programmerString[programCursor];
  if(key=='M') //mode button
  {
    nextDisplayMode();
  }

  if(programCursor<SCREENCHARS)
  {
    nextChar=programmerString[programCursor+1];
    
  }
  if(programCursor>0)
  {
    previousChar=programmerString[programCursor-1];
  }
  if(programCursor<SCREENCHARS-2)
  {
    nextCharInQuestion2=programmerString[programCursor+2];
  }
  //make this char 0 if it is a space two chars in front of ':'
  if((nextCharInQuestion2==':' ||  nextCharInQuestion2=='-'  || previousChar=='-') && charInQuestion==' ')
  {
    programmerString[programCursor]='0';
    charInQuestion=programmerString[programCursor];
    
  }
  if(editOption==23  && programCursor>0  && programCursor<5 && charInQuestion==' '  || nextChar=='-' && editOption!=22)
  {
    programmerString[programCursor]='0';
    charInQuestion=programmerString[programCursor];
  }
  //numeric range can be zero or one in this application. -1 means it is outside this range
  charRange = whichNumericRange(charInQuestion);
  digitInQuestion = charInPossibleRangeToNumber(charInQuestion);
  //Serial.println(digitInQuestion);
  //Serial.print((int)editOption);
  //Serial.println("");
  if(editOption==20 || editOption==22 || editOption==23 || editOption==COUNTDOWNSET) //200 is countDown
  {
    if(digitInQuestion>=0  && digitInQuestion<10 )
    {
      //Serial.println(digitInQuestion);
      if(key=='U')
      {
        if(digitInQuestion<10-1)
        {
          programmerString[programCursor]=(char)(numberToCharInRange(digitInQuestion + 1, charRange));
        }
        else
        {
          programmerString[programCursor]=(char)(numberToCharInRange(0, charRange));
          
        }
      }
      else if (key=='D')
      {
        if(digitInQuestion>0)
        {
          programmerString[programCursor]=(char)(numberToCharInRange(digitInQuestion - 1, charRange));
        }
        else
        {
          programmerString[programCursor]=(char)(numberToCharInRange(9, charRange));
 
        } 
      }
    }

    
    if(justEnteredProgramMode)
    {
      displayCurrentTime(true);
      //Serial.print("~");
      for(i=0; i<SCREENCHARS; i++)
      {
        
        if(editOption==20)
        {
          displayString[i]=timeString[i];
          programmerString[i]=timeString[i];
        }
        else if(editOption==COUNTDOWNSET) //countDown timer
        {
          //Serial.println("ya");
          displayString[i]=countDownString[i];
          programmerString[i]=countDownString[i];
        }
        else if(editOption==22)
        {
          
          displayString[i]=dateString[i];
          programmerString[i]=dateString[i];
          //Serial.print(dateString[i]);
          //Serial.print(F(" "));
          //Serial.print(programmerString[i]);
          //Serial.println("");
        }
        else
        {
          displayString[i]=yearString[i];
          programmerString[i]=yearString[i];
        }
        //Serial.println(programmerString[i]);
      }
      //experimental:
      bool switchToSpaces=false;
      for(i=0; i<SCREENCHARS; i++)
      {
        if(displayString[i]==0)
        {
          switchToSpaces=true;
        }
        if(switchToSpaces)
        {
          displayString[i]=32;
          programmerString[i]=32;
        }
      }
      displayString[SCREENCHARS-1]=0;
      programmerString[SCREENCHARS-1]=0;
      //end experimental
      justEnteredProgramMode=false;
    }
    else
    {
      //Serial.print("*");
      for(i=0; i<SCREENCHARS; i++)
      {
        displayString[i]=programmerString[i];
        
      }
      
    }
  }
  else if (editOption==21 || editOption==24  || editOption==25) //Military time or not, the editor
  {
    programCursor=landingCursorPos[editOption-BASEOPTION];
    char label[]= "Mil:";
    byte maxVal=1;
    byte minVal=0;
    char noun[]="  Military";
    byte valWeHad=displayMilitary;
    if(editOption==24)
    {
      strncpy(label, "Bri:", 4); //how you overwrite a string
      maxVal=9;
      minVal=0;
      strncpy(noun, "Brightness", 10); //how you overwrite a string
      valWeHad=intensity; //only need eight steps, not 16
      
    }
    if(editOption==25)
    {
      strncpy(label, "Vol:", 4); //how you overwrite a string
      maxVal=7;
      minVal=0;
      strncpy(noun, "    Volume", 10); //how you overwrite a string
      valWeHad=loudness; //only need seven steps, not 16
      
    }
    //Serial.print("MIL");
    //Serial.print((int)displayMilitary);
    //Serial.println("");
    //return;
    //clearProgrammingScreen();
    //Serial.print("freeMemory()=");
    //Serial.println(freeMemory());
    if(justEnteredProgramMode)
    {
      //Serial.println("Y");
      for(i=0; i<SCREENCHARS; i++)
      {
        //Serial.println(i);
        if(i<sizeof(label))
        {
          programmerString[i]=(char)(label[i]);
          displayString[i]=(char)(label[i]);
        }
        else
        {
          displayString[i]=(char)32;
          programmerString[i]=(char)32;
        }
        
      }
      justEnteredProgramMode=false;
    }
    else
    {
      //Serial.println("X");
      //Serial.print("*");
      //Serial.println(SCREENCHARS);
      for(i=0; i<SCREENCHARS; i++)
      {
        //Serial.println(i);
        displayString[i]=programmerString[i];
        
      }
      
    }
    //Serial.println(sizeof(label));
    programmerString[sizeof(label)-1]=(char)(valWeHad+48);
    displayString[sizeof(label)-1]=(char)(valWeHad+48);

    charInQuestion=programmerString[programCursor];
    charRange = whichNumericRange(charInQuestion);
    digitInQuestion = charInPossibleRangeToNumber(charInQuestion);

    //Serial.println(digitInQuestion);
    
    if(key=='U')
    {
      //Serial.println("UP");
      if(digitInQuestion<maxVal)
      {
        //Serial.println("less than/= max val");
        //Serial.println((char)(numberToCharInRange(digitInQuestion+1, charRange)));
        programmerString[programCursor]=(char)(numberToCharInRange(digitInQuestion+1, charRange));
      }
      else
      {
        //Serial.println("greater than max val");
        programmerString[programCursor]=(char)(numberToCharInRange(minVal, charRange));
        
      }
    }
    else if (key=='D')
    {
      if(digitInQuestion>minVal)
      {
        //Serial.println("greater than min val");
        programmerString[programCursor]=(char)(numberToCharInRange(digitInQuestion-1, charRange));
      }
      else
      {
        //Serial.println("less than min val");
        programmerString[programCursor]=(char)(numberToCharInRange(maxVal, charRange));
        
      }
    }
    //Serial.println("VV");
    byte valToSave=programmerString[programCursor]-48;
    byte locToSave=locDisplayMilitary;
    //Serial.print("val to save:");
    //Serial.print(valToSave);
    //Serial.println("");
    byte processedValueToSave=valToSave;
    if(editOption==21)
    {
      displayMilitary=processedValueToSave;
      locToSave=locDisplayMilitary;
    }
    else if (editOption==24)
    {
      processedValueToSave=valToSave;   //allows us to read the top level, 15, at eight
      intensity=processedValueToSave; //the button interface supports half the available granularity of intensity
      locToSave=locIntensity;
      m.setIntensity(intensity);   
    }
    else if (editOption==25)
    {
      processedValueToSave=valToSave;   //allows us to read the top level, 15, at eight
      loudness=processedValueToSave; //the button interface supports half the available granularity of intensity
      locToSave=locLoudness;
      setLoudness(loudness);   
    }
    if(key=='X')
    {
      Serial.print(noun);
      Serial.print(F(" set as "));
      Serial.print((int)processedValueToSave);
      Serial.println("");
      storebyte(locToSave, processedValueToSave, PERM_DATA_METHOD);
      doneWithProgramming();
    }
    //return;
    //Serial.print("DONG");
    /*
    for(i=0; i<SCREENCHARS; i++)
    {
      Serial.println((char)displayString[i]);
      
    }
    */
    //printStringStatic(displayString,0);
    //return;
  }

  
  //Serial.println("C");
  if(key=='L'  )
  {
    if((nextCharInQuestion2==':' || programCursor==0)  && editOption==20 )
    {
      //various edit options;  0 is time 1 is military/non-military, 2 is date
      editOption=21;
      justEnteredProgramMode=true;
      //Serial.println("OFF TO MIL");
    }
    else if(editOption==21)
    {
      editOption=22;
      justEnteredProgramMode=true;
      
      programCursor=landingCursorPos[editOption-BASEOPTION];
      
      //Serial.println("OFF TO DATE");
    }
    else if(editOption==22 && programCursor==0)
    {
   
      editOption=23;
      justEnteredProgramMode=true;
      
      programCursor=landingCursorPos[editOption-BASEOPTION];
 
      //Serial.println("OFF TO YEAR");
    }
    else if(editOption==23  && programCursor==0)
    {
      //can't go beyond here for now!
      //actually no, we got something now:
      editOption=24;
      justEnteredProgramMode=true;
    }
    else if(editOption==24  )
    {
      //can't go beyond here for now!
      //actually no, we got something now:
      editOption=25;
      justEnteredProgramMode=true;
    }
    else
    {
      programCursor--;
    }
  }
  else if (key=='R'  )
  {
    if(editOption==21)
    {
      editOption=editOption-1;
      //programCursor=0;
      justEnteredProgramMode=true;
      //clearScreen();
      if(editOption==20) //now it could be 0
      {
       
        programCursor=landingCursorPos[editOption-BASEOPTION];
        
        //clearScreen();
        //displayCurrentTime();
      }
      programCursor--; //counteract the impending increment! 
      //return;
    }
    else if(editOption==22)
    {
        if((programCursor==4) )
        {
          
          //various edit options;  0 is time 1 is military/non-military, 2 is date
          editOption=21;
          programCursor=landingCursorPos[editOption-BASEOPTION];
          programCursor--; //counteract the impending increment! 
          justEnteredProgramMode=true;
          //Serial.println("OFF TO MIL");
        }
    }
    else if(editOption==23)
    {
        if((programCursor==3) )
        {
          //various edit options;  0 is time 1 is military/non-military, 2 is date
          editOption=22;
          
          programCursor=landingCursorPos[editOption-BASEOPTION];
          programCursor--; //counteract the impending increment! 
          justEnteredProgramMode=true;
          //Serial.println("OFF TO DATE");
        }
    }
    else if(editOption==24)
    {
      //OFF TO YEAR
      editOption--;
      programCursor=landingCursorPos[editOption-BASEOPTION];
      justEnteredProgramMode=true;
      programCursor--; //counteract the impending increment! 
    }
    else if(editOption==25)
    {
      //OFF TO BRIGHTNESS
      editOption--;
      programCursor=landingCursorPos[editOption-BASEOPTION];
      justEnteredProgramMode=true;
      programCursor--; //counteract the impending increment! 
    }
    else if(editOption==20)
    {
      //no going right of time setting, for now!
      //Serial.println(programCursor);
      if(programCursor>6)
      {
        programCursor--;
      }
      //starting up the alarm setting:
      //editOption--;
    }
    
    else
    {
      
    }
    programCursor++;
  }
  else if (key=='X')
  {
    saveProgrammedData();
    doneWithProgramming();
  }
  //this chunk of code is just to get the width of the char under our "cursor":
  //(set this again in case it got changed by the rat's nest of code above)
  charInQuestion=displayString[programCursor];
  char c = charInQuestion - BASECHAR ;
  char thisChar[7];

  memcpy_P(thisChar, CH + 7*c, 7);
  //Serial.println( c);
  //Serial.print(" size:");
  //Serial.print((int)thisChar[0]);
  //Serial.println("");
  //appropriate cursor for the width is caclulated this way:
  //Serial.println((int)charInQuestion);

  /*
  char tempChar[7];
  char thisByte;
  char thisInnerByte;
  
  for(int charIndex=0; charIndex<8; charIndex++){
    thisByte = thisChar[charIndex];
    for(int charIndex2=0; charIndex2<8; charIndex2++) {
      thisInnerByte = ((thisByte >> charIndex2) &  1);
      tempChar[7-charIndex2]  |= (thisInnerByte << 1);
    }
  }
  for(int charIndex=0; charIndex<8; charIndex++){
    thisChar[charIndex] = tempChar[charIndex];
  }
  */
  char appropriateCursor=(BASECHAR-1+SPRITECOUNT) + (int)thisChar[0];
  //return;
  if(programEvenThrough)
  {
  
    displayString[programCursor]=(char)appropriateCursor;
    programEvenThrough=false;
  }
  else
  {
  
    programEvenThrough=true;
  }
  displayString[SCREENCHARS-1]=0;
  printStringStatic(displayString,0);
}



void displayCurrentTime(bool enteringProgramMode)
{
  
 
  byte stringCursor;
  byte fillerFront=1;
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
  stringCursor=0;
  if(!displaySeconds)
  {
    fillerFront=0;
  }
  for(byte i=0; i<fillerFront; i++)
  {
    timeString[stringCursor]=' ';
    stringCursor++;
  }
  //Serial.println((int)displayMilitary);
  if(!displayMilitary && !enteringProgramMode) //when we're in programMode, everything is military // 
  {
    if(hour>12)
    {
      hour=hour-12;
    }
    if(hour<1)
    {
      hour=12;
    }
  }
  if(hour>9)
  {
    timeString[stringCursor]=(char)(hour / 10 +48 );
    stringCursor++;
    timeString[stringCursor]=(char)((hour % 10)+48);
    stringCursor++;
  }
  else
  {
    timeString[stringCursor]=' ';
    stringCursor++;
    timeString[stringCursor]=(char)(hour+48);
    stringCursor++;
  }
  timeString[stringCursor]=':';
  stringCursor++;

  if(minute>9)
  {
    timeString[stringCursor]=(char)(minute/10+48);
    stringCursor++;
    timeString[stringCursor]=(char)((minute % 10) + 48);
    stringCursor++;
  }
  else
  {
    timeString[stringCursor]='0';
    stringCursor++;
    timeString[stringCursor]=(char)(minute+48);;
    stringCursor++;
  }
  //timeString[stringCursor]=0;
  //stringCursor=0;
  if(displaySeconds)
  {
    if(second>9)
    {
      timeString[stringCursor]=(second/10+BASECHAR + CURSORCOUNT + SPRITECOUNT);
      stringCursor++;
      timeString[stringCursor]=((second % 10) + BASECHAR + CURSORCOUNT + SPRITECOUNT);
      stringCursor++;
       
    }
    else
    {
      timeString[stringCursor]=BASECHAR + CURSORCOUNT + SPRITECOUNT;
      stringCursor++;
      timeString[stringCursor]=(second+BASECHAR + CURSORCOUNT + SPRITECOUNT);
      stringCursor++;
    }
  }
  else
  {
    timeString[stringCursor]=32;
    stringCursor++;
  }
  timeString[stringCursor]=0;
 
  
 
  stringCursor=0;
  if(month>9)
  {
    dateString[stringCursor]=(char)(month / 10 +48 );
    stringCursor++;
    dateString[stringCursor]=(char)((month % 10)+48);
    stringCursor++;
  }
  else
  {
    dateString[stringCursor]='0';
    stringCursor++;
    dateString[stringCursor]=(char)(month+48);
    stringCursor++;
  }
  dateString[stringCursor]='-';
  stringCursor++;
  if(dayOfMonth>9)
  {
    dateString[stringCursor]=(char)(dayOfMonth / 10 +48 );
    stringCursor++;
    dateString[stringCursor]=(char)((dayOfMonth % 10)+48);
    stringCursor++;
  }
  else
  {
    dateString[stringCursor]='0';
    stringCursor++;
    dateString[stringCursor]=(char)(dayOfMonth+48);
    stringCursor++;
  }  
  //dateString[stringCursor]='-';
  
  
  dateString[stringCursor]=0;
  stringCursor=0;
  //Y2K REDUX BUG!!!:
  yearString[stringCursor]=(char)(upperTwoYearsDigits/10 +48);
  stringCursor++;
  yearString[stringCursor]=(char)(upperTwoYearsDigits % 10 +48);
  stringCursor++;
  if(year>9)
  {
    yearString[stringCursor]=(char)(year / 10 +48 );
    stringCursor++;
    yearString[stringCursor]=(char)((year % 10)+48);
    stringCursor++;
  }
  else
  {
    yearString[stringCursor]='0';
    stringCursor++;
    yearString[stringCursor]=(char)(year+48);
    stringCursor++;
  }  
  yearString[stringCursor]=' ';
  stringCursor++;
  yearString[stringCursor]=' ';
  stringCursor++;
  yearString[stringCursor]=0;
  //Serial.print("string cursor:");
  //Serial.print(stringCursor);
  //Serial.println("");

  countDownString=deciSecondsToMinutesSeconds(countDownTime); 
  //Serial.println(countDownString);
  printStringStatic(timeString,0);
  //printStringStatic(timeStringSeconds,1);
}

void explosion()  //happens synchronously for now
{
  //for Ahmed Mohamed!
  playVoice(explosionSound);
  Serial.println(F("BOOM!"));
  m.setIntensity(1);
  clearScreen();
  delay(50);
  printStringStatic("   +  ",0);
  delay(10);
  printStringStatic("   *  ",0);
  Serial.println(explosionSound);
  
  for(byte i=0; i<32; i++)
  {
    m.setIntensity(i);
    delay(10);
  }
  
  delay(50);
  printStringStatic("  ( )  ",0);
  delay(50);
  printStringStatic(" ( @ )  ",0);
  delay(50);
  printStringStatic("{(   )} ",0);
  delay(50);
  m.setIntensity(0);
  printStringStatic("~     ~ ",0);
  clearScreen();
  delay(1000);
  m.setIntensity(intensity);   
}

void parseSerial()
//this reads the serial line (i use 19200 baud) and responds to commands
//for setting the time, showing a temp message, or showing military time, etc.
{
  byte i;
  char firstChar=(char)serIn[0];
  char secondChar=(char)serIn[1];
  char thirdChar=(char)serIn[2];
  char fourthChar=(char)serIn[3];
  bool passThrough=false;
  int thisint=0;
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  char b;
  bool bwlTimeScanDone=false;
  byte dpcursor=0;
  byte whichsettingregime;
  bool valGivenInPass=false;
  unsigned long  paramVal;
  if(!handled)
  {
    //delay(12);
    if(1==2)
    {
      Serial.print(firstChar);
      Serial.print('*');
      Serial.print(secondChar);
      Serial.print('^');
      Serial.print(thirdChar);
      Serial.print('^');
      Serial.print(fourthChar);
      Serial.println(spaceDelimiter);
    }
    
    //how parseSerial works on the client
    //normally serial on the client will only be used for debugging, but it will have a pass-through method
    //to send/receive data up-to/down-from the sensor pod (running gusweather.ino)
    //so basically intercept all the commands that work on the sensor pod and echo them up and back
    
    if(firstChar=='b') 
    {
      //firstChar='p';
      //printMode='b';
    }
    else if(firstChar=='d') 
    {
      firstChar='p';
      printMode='d';
    }
    else if(firstChar=='p') 
    {
      firstChar='p';
      printMode='p';
    }
    serIn[0]=firstChar;
    if(firstChar=='c') //'c' clear
    {
      if(secondChar=='d') //'cd' -- clear count up
      {
        countDown=countDownTime;
        Serial.println(F("CountDown cleared."));
      }
      else if(secondChar=='u') //'cu' -- clear count up
      {
        countUp=0;
        Serial.println(F("CountUp cleared."));
      }
      else if(secondChar=='s') //'cs' -- clear screen
      {
        clearScreen();
        
        Serial.println(F("Screen cleared."));
      }
      handled=true;
    }
    if(firstChar=='v')//'v' voice
    {
      if(secondChar=='v') //'vv' -- voice voice
      {
        if(thirdChar=='b') //'vvb' -- voice voice bell
        {
          paramVal=bellSound;
          
        }
        else if(thirdChar=='h') //'vvh' -- half hour chime
        {
          paramVal=halfhourChime;
          
        }
        if(thirdChar=='a') //'vva' -- alarm chime
        {
          //vva0 plays alarm for alarm 0, etc.
          paramVal=getbyte(locAlarms + fourthChar*6+0,PERM_DATA_METHOD);
 
          
        }
        else if(thirdChar=='e') //'vve' -- voice voice explosion
        {
          paramVal=explosionSound;
          
        }
        else
        {
          paramVal= (unsigned int)makeLong((char*)serIn, 2);

        }
        playVoice(paramVal);
        Serial.print(F("Voice "));
        Serial.print(paramVal);
        Serial.print(F(" played."));
        Serial.println("");
        handled=true; 
      }
    }
    
    if(firstChar=='a')//'a' animate
    {
      if(secondChar=='e') //'ae' -- animate explosion, this one for Ahmed Mohamed
      {
        explosion();
        handled=true; 
      }
    }
    if(firstChar=='b') //'b' blink
    {
      if(secondChar=='s') //'bs' -- blink screen
      {
        clearScreen();
        //this is just clear screen on repeat
        Serial.println(F("Screen blinked!"));
      }
    }
    if(firstChar=='i')
    {
      paramVal= (unsigned int)makeLong((char*)serIn, 1);
      
      if(paramVal<0)
      {
        setDisplayMode(COUNTDOWNSET);
      }
      else
      {
        setDisplayMode(COUNTUPSET);
      }
      initiateCount(paramVal);
      Serial.println(F("Count initiated."));
      handled=true;
    }
    if(firstChar=='s') //'s' set
    {
 
 
      if(secondChar=='s') //'ss' -- set system 
      {
 
        if(thirdChar=='s') //'sss' -- set system print space
        {
          getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
          //sets spaceDelimiter on both pod and client
          spaceDelimiter = makeLong((char*)serIn, 3);

          
          if(spaceDelimiter==0)
          {
            if((byte)serIn[3]==32)
            {
              spaceDelimiter=serIn[4];
            }
            else
            {
              spaceDelimiter=serIn[3];
            }
            
          }
          passThrough=true;
        }
      }
      if(secondChar=='b')  //'sb' -- set bell  
      {
        if(thirdChar=='o')  //'sbo' -- set bell to ring (0 or 1)
        {
          paramVal= (unsigned int)makeLong((char*)serIn, 3);
          chimeOnHour=paramVal;
          storebyte(locChimeOnHour, paramVal, PERM_DATA_METHOD);
          chimesToDo=0;
          Serial.print(F("Chime set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
        else if(thirdChar=='r')  //'sbr' -- set bell range 
        {
          unsigned long secondParameter;
          make2Longs((char*)serIn, 3, &paramVal,  &secondParameter);
          chimeBegin=paramVal;
          chimeEnd=secondParameter;
          storebyte(locChimeBegin, paramVal, PERM_DATA_METHOD);
          storebyte(locChimeEnd, secondParameter, PERM_DATA_METHOD);
          Serial.print(F("Chime range begins at hour "));
          Serial.print(paramVal);
          Serial.print(F(" and ends at hour "));
          Serial.print(secondParameter);
          Serial.println("");
        }
 
      }
 
      if(secondChar=='a')  //'sa' -- set alarm
      {
        unsigned long secondParameter;
        make2Longs((char*)serIn, 4, &paramVal,  &secondParameter);
        if(thirdChar=='t')  //'sat' -- set alarm time
        {
 
          //alarm is picked by the character after sat.  sat0 12:33 sets alarm 0 as 12:33
          storebyte(locAlarms + 6 * (fourthChar-48) + 4, paramVal, PERM_DATA_METHOD); //minutes
          storebyte(locAlarms + 6 * (fourthChar-48) + 5, secondParameter, PERM_DATA_METHOD); //seconds
          Serial.print(F("Alarm # "));
          Serial.print(fourthChar);
          Serial.print(F(" set to "));
          Serial.print(paramVal);
          Serial.print(":");
          Serial.print(secondParameter);
          Serial.println("");
        }
        if(thirdChar=='d')  //'sad' -- set alarm date //only does month/day, not year
        {
 
          //alarm is picked by the character after sat.  sat0 12:33 sets alarm 0 as 12:33
          storebyte(locAlarms + 6 * (fourthChar-48) + 2, paramVal, PERM_DATA_METHOD); //month
          storebyte(locAlarms + 6 * (fourthChar-48) + 3, secondParameter, PERM_DATA_METHOD); //dayofmonth
          Serial.print(F("Alarm # "));
          Serial.print(fourthChar);
          Serial.print(F(" set to "));
          Serial.print(paramVal);
          Serial.print("/");
          Serial.print(secondParameter);
          Serial.println("");
        }
        if(thirdChar=='y')  //'say' -- set alarm year //only does two-digit.  
        //anything else (or zero) means year isnt set
        //and all alarms repeat every year
        {
 
          //alarm is picked by the character after sat.  sat0 12:33 sets alarm 0 as 12:33
          if(paramVal>99)
          {
            paramVal=255;
          }
          storebyte(locAlarms + 6 * (fourthChar-48) + 1, paramVal, PERM_DATA_METHOD); //year
          Serial.print(F("Alarm # "));
          Serial.print(fourthChar);
          Serial.print(F(" year set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
      }
      if(secondChar=='l') //'sl' -- set loudness
      {
        loudness= (unsigned int)makeLong((char*)serIn, 2);
        storebyte(locLoudness, loudness, PERM_DATA_METHOD);
        Serial.print(F("Loudness set to "));
        Serial.print(loudness);
        Serial.println("");
        setLoudness(loudness);
      }
      else if(secondChar=='v')  //'sv' -- set voice
      {
        paramVal= (unsigned int)makeLong((char*)serIn, 3);
        if(thirdChar=='e') //'sve' -- set voice explosion
        {
          
          explosionSound = paramVal;
          storebyte(locExplosionSound, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Explosion voice set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
        else if(thirdChar=='b') //'svb' -- set voice bell
        {
          bellSound = paramVal; 
          storebyte(locBellSound, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Bell voice set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
        else if(thirdChar=='h') //'svh' -- set voice half-hour
        {
          halfhourChime = paramVal; 
          storebyte(locHalfhourChime, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Half-hour chime set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
        else if(thirdChar=='a') //'sva' -- set voice alarm
        {
          //alarm is picked by the character after sva.  sva0 32 sets voice 32 as alarm 0
          paramVal= (unsigned int)makeLong((char*)serIn, 3);
          storebyte(locAlarms + 6 * (fourthChar-48) + 0, paramVal, PERM_DATA_METHOD); //alarm chime
          Serial.print(F("Alarm voice set to "));
          Serial.print(paramVal);
          Serial.println("");
        }
 
      }
      if(secondChar=='i') //'si' set intensity 
      {
        paramVal= (unsigned int)makeLong((char*)serIn, 2);
        storebyte(locIntensity, paramVal, PERM_DATA_METHOD);
        m.setIntensity(paramVal);
        intensity=paramVal;
        Serial.print(F("Intensity set to "));
        Serial.print(paramVal);
        Serial.println("");
      }
      if(secondChar=='t') //'st' set temp  
      {
        if(thirdChar=='t') //'stt' set temp message time
        {
          paramVal= (unsigned int)makeLong((char*)serIn, 3);
          tempMessageTime=paramVal;
          Serial.print(F("Temp message time is now "));
          Serial.print(paramVal);
          Serial.println("");
          storeint(locMessageTime, tempMessageTime, PERM_DATA_METHOD);
 
 

          
        }
        if(thirdChar=='m') //'stm' set temp message
        {
          for(byte i=0; i<SER_IN_BUFFER_SIZE; i++)
          {
            tempMessage[i]=serIn[i+3];
          }
          //tempMessage[i]=0;
          doDisplayTempMessage=true;
          timeTempMessageBegin=millis();
        }
      }
      if(secondChar=='C') //'sC' set countDown top
      { 
        //paramVal= (unsigned int)makeLong((char*)serIn, 2);
        unsigned long secondParameter;
        make2Longs((char*)serIn, 2, &paramVal,  &secondParameter);
        if(secondParameter>999)
        {
          //if the second parameter is very large, it probably wasn't set, so ignore it and set the countdown
          //clock in deciseconds
        }
        else
        {
          //if two params came in this way, then we got a minutes and a seconds, possibly delimited by a ":", so set the time that way
          //make2Longs doesn't parse "." and ignores it, so we can use that to pass in an implied decimal point
          //"54.6" reads as "546" so don't multiply by ten to get deciseconds in cases where the second param is >99
          if(secondParameter>99)
          {
            paramVal=paramVal*600 + secondParameter;
          }
          else
          {
            paramVal=paramVal*600 + secondParameter *10;
          }
        }
        countDownTime=paramVal;
        Serial.print(F("CountDown start set to: "));
        Serial.print(deciSecondsToMinutesSeconds(countDownTime));
        Serial.println("");
        storelong(locCountDownTime, paramVal, PERM_DATA_METHOD);
      }
      
      else if(secondChar=='c') //'sc' set realtime clock
      {
        getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
        //now we use code from the solar sufficiency controller system for setting the RTC
        whichsettingregime=2; //normally sets the clock time
        if(thirdChar=='d') //'scd' set clock date
        {
          whichsettingregime=3;
        }
        if(thirdChar=='w') //'scw' set clock day of week
        {
          whichsettingregime=1;
        }
        for(byte inputCursor=3; inputCursor<30; inputCursor++) //start from 2 since 'scx' is 0 & 1 & 2
        {
          b=serIn[inputCursor];
          //thisint=-1;
          if(b>='0'  && b<='9')
          {
            valGivenInPass=true;
            thisint=thisint*10+(b-48);
          }
        
          if((b==' ' || b==':' || b=='-' || b==',' || b==0  && !bwlTimeScanDone) && inputCursor>3 )
          {
            
            if(dpcursor==clocksettingregime[whichsettingregime][0])
            {
              if(valGivenInPass)
              {
                second=thisint;
                
                Serial.print(spaceDelimiter);
                Serial.print(F("seconds:"));
   
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][1]  )
            {
              if(valGivenInPass)
              {
                minute=thisint;
                
                Serial.print(F(" minutes:"));
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][2]  && thisint!=0)
            {
              if(valGivenInPass)
              {
                hour=thisint;
                
                Serial.print(F(" hours:"));
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][3] && thisint!=0)
            {
              if(valGivenInPass)
              {
                dayOfWeek=thisint;
                
                Serial.print(F(" dayofweek:"));
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][4] && thisint!=0)
            {
              if(valGivenInPass)
              {
                dayOfMonth=thisint;
                
                Serial.print(F(" day:"));
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][5] && thisint!=0)
            {
              if(valGivenInPass)
              {
                month=thisint;
                
                
                Serial.print(F(" month:"));
                Serial.print(thisint);
              }
            }
            else if(dpcursor==clocksettingregime[whichsettingregime][6] && thisint!=0)
            {
              if(valGivenInPass)
              {
                year=thisint;
        
                Serial.print(F(" year:"));
                Serial.print(thisint);
              }
            }
            //Serial.println("clock set?");
            setDateDs1307(second, minute, hour, dayOfWeek, dayOfMonth, month, year);
            thisint=0;
            dpcursor++;
            valGivenInPass=false;
          }

        }
        Serial.println("");
      }
      if(secondChar=='D') //'sD' -- set data 
      {
        unsigned long secondParameter;
        make2Longs((char*)serIn, 4, &paramVal,  &secondParameter);
        Serial.print(F("Set data at location #"));
        Serial.print(paramVal);
        Serial.print(F(" in storage #"));
        Serial.print(fourthChar-48);
        Serial.print(F(" to "));
        Serial.print(secondParameter);
        Serial.println("");
        //paramVal= (unsigned int)makeLong((char*)serIn, 4); //turn everything after char 4 into data
        if(thirdChar=='l')//'sDl' -- set data long
        {
          
          storelong(paramVal, (long)secondParameter, (byte)(fourthChar-48)); //4th char is mode; 2 is rtc memory
        }
        else if(thirdChar=='i')//'sDi' -- set data int
        {
          //Serial.println(paramVal);
          //Serial.println(' ');
          //Serial.println(fourthChar-48);
          //Serial.println(' ');
          storeint(paramVal, (int)secondParameter, (byte)(fourthChar-48)); //4th char is mode; 2 is rtc memory
        }
        else if(thirdChar=='b')//'sDb' -- set data byte
        {
          storebyte(paramVal, (byte)secondParameter, (byte)(fourthChar-48)); //4th char is mode; 2 is rtc memory
        }

        
      }
      if(secondChar=='d') //'sd' -- set display 
      {
        paramVal= (unsigned int)makeLong((char*)serIn, 3);
        //Serial.println(paramVal);
        if(thirdChar=='s') //'sds' -- set display seconds
        {
          displaySeconds=paramVal;
          storebyte(locDisplaySeconds, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Seconds?: "));
          Serial.print(paramVal);
          Serial.println("");
        }
        if(thirdChar=='M') //'sdM' -- set display mode
        {
          Serial.print(F("Display mode set to: "));
          Serial.print(paramVal);
          Serial.println("");
          setDisplayMode(paramVal);
        }
        if(thirdChar=='m') //'sdm' -- set display military
        {
  
          displayMilitary=paramVal;
          storebyte(locDisplayMilitary, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Military time: "));
          Serial.print(paramVal);
          Serial.println("");
        }
        if(thirdChar=='y') //'sdy' -- set display first two digits of year
        {
          paramVal= (unsigned int)makeLong((char*)serIn, 3);
          upperTwoYearsDigits=paramVal;
          storebyte(locUpperTwoYearDigits, paramVal, PERM_DATA_METHOD);
          Serial.print(F("Upper two digits of year is now "));
          Serial.print(paramVal);
          Serial.println("");
        }
      }
      handled=true;
    }

    if(firstChar=='p') //'p' print
    {
      
      if(secondChar=='a') //'pa' print alarm
      {
        printAlarmSetting((int)(thirdChar-48));
      }
      if(secondChar=='c') //'pc' print realtime clock
      {
        printRTCDate();

      }

      
      if(secondChar=='D') //'pD' -- print data
      {
 
        paramVal= (unsigned int)makeLong((char*)serIn, 4); //turn everything after char 4 into data
        
        Serial.print(F("Value at location #"));
        Serial.print(paramVal);
        Serial.print(F(" in storage #"));
        Serial.print(fourthChar-48);
        Serial.print(F(": "));
        
        
        if(thirdChar=='l')//'pDl' -- print data long
        {
          
          Serial.println(getlong(paramVal, (byte)(fourthChar-48))); //4th char is mode; 2 is rtc memory
        }
        else if(thirdChar=='i')//'pDi' -- print data int
        {
          //Serial.println(paramVal);
          //Serial.println(' ');
          //Serial.println(fourthChar-48);
          //Serial.println(' ');
          Serial.println(getint(paramVal, (byte)(fourthChar-48))); //4th char is mode; 2 is rtc memory
        }
        else if(thirdChar=='b')//'pDb' -- print data byte
        {
          Serial.println(getbyte(paramVal, (byte)(fourthChar-48))); //4th char is mode; 2 is rtc memory
        }
      }
      if(secondChar=='T') //'pT' -- print time stamp
      {
        Serial.println(currentTimestamp());
      }
      if(fourthChar!='r')
      {
        handled=true;
      }
    }
  
    if(firstChar=='r') //'r' reset
    {
      resetClock();
      handled=true;
    }
 
    //for some reason i had this happening August 10 2015:
    //passThrough=false;
    //Serial.println("wa!");
    if(firstChar=='x') //debugging, might need on client so do not pass through
    {
      //Serial.println("x!");
      unsigned long ts=currentTimestamp();
      printRTCDate();
      //Serial.print(' ');
      Serial.println(ts);
      //Serial.print(' ');
      getDateFromTimeStamp(ts, 2000);
      Serial.println(' ');
    }
    if(firstChar=='y') //debugging, might need on client so do not pass through
    {

    }
    //handled=true;
    serCount=0;
  }
  //Serial.println("no pass through");
  if(passThrough)
  {
 
    //Serial.println("pass through");
    for(i=0; i<SER_IN_BUFFER_SIZE-1; i++)
    {
      //Serial.write((char)serIn[i]);
      //Serial.write(serIn[i]);
      serIn[i]=spaceDelimiter;
    }
    handled=true;
    
  }
  
  if(Serial.available())
  {
    handled=false;
    for(i=0; i<SER_IN_BUFFER_SIZE-1; i++)
    {
      serIn[i]=' ';//clear old serialinput
    }
    
  }
  while (Serial.available()) 
  {
    // read the most recent byte
    serIn[serCount] = Serial.read() ;
    //Serial.println((char)serIn[serCount]);
    //Serial.println((char)'+');
    if(serCount<SER_IN_BUFFER_SIZE-1)
    {
      serCount++;
    }
    handled=false;
    delay(2);
  }
  
  //Serial.println("dank");
  serCount=0;
}

void playVoice(int voice)
{
  //wtv020sd16p.stopVoice();
  //delay(10);
  //wtv020sd16p.asyncPlayVoice(voice);
}

void setLoudness(byte loudness)
{
  //wtv020sd16p.playVoice(0xfff * 0x10 + loudness);
}

///////////////////////////////////////////
unsigned long powerof(long intin, byte powerin)
//raises intin to the power of powerin
//not using "pow" saves me a lot of Flash memory
{
  unsigned long outdata=1;
  byte k;
  for (k = 1; k <powerin+1; k++)
  {
    outdata=outdata*intin;
  }
  return outdata;
}

//converts an int to a char. 
char* itoa(unsigned long val, int base)
{
  static char buf[32] = {0};
  int i = 30;

  for(; val && i ; --i, val /= base)
  {
    buf[i] = "0123456789abcdef"[val % base];
  }

  return &buf[i+1];
}

static char *padLeftString(char * string, char pad, byte padded_len)
{
  static char buf[12] = {0};
  byte i=0;
  byte lenCount=0;
  for(i=0; i<10; i++)
  {
    if(string[i]==0  && lenCount==0)
    {
      lenCount=i;
    }
  }
  for(i=0; i<(padded_len-lenCount); i++)
  {
    buf[i]=pad;
  }
 
  for(i=0; i<10; i++)
  { 
    buf[i+(padded_len-lenCount)-1]=string[i];
    
  }
  return buf;
}


//converts unsigned int to signed int
int uitosi(unsigned int val)
{
  return val-65537;
}



//converts signed long to unsigned long
unsigned long sltoul(long val)
{
  return val+4294967297;
}

//converts signed int to unsigned int
unsigned int sitoui(int val)
{
  return val+65537;
}



//converts unsigned long to signed long
long ultosl(unsigned long val)
{
  return val-4294967297;
}

void make2Longs(char* arrIn, byte startPos, unsigned long* longOne, unsigned long* longTwo)
//scan an array of char from startPos and parse two signed numeric values
{
    //Serial.println("spaghetti");
    //Serial.println(arrIn);
    long out=0;
    int negativeFactor=1;
    byte outCount=0;
    bool hadAValue=false;
    for(byte i=startPos; i<SER_IN_BUFFER_SIZE-1; i++)//SER_IN_BUFFER_SIZE for now, need something better than that
    {
      
      char thisChar=arrIn[i];
      //Serial.println(thisChar);
      if(thisChar=='-' && out==0)
      {
        negativeFactor=-1;
      }
      else if(thisChar==' '  || thisChar==',' || thisChar==':' || thisChar=='/')
      {
        if(outCount==0 && hadAValue)
        {
          *longOne=negativeFactor * out;
          outCount++;
          hadAValue=false;
          out=0;
        }
        else if(outCount==1 && hadAValue)
        {
          *longTwo=negativeFactor * out;
          outCount++;
          out=0;
        }
      }
      else if((byte)thisChar>47 && (byte)thisChar<58) //is char numeric?
      {
        out=(out *10) + (byte)thisChar-48;
        hadAValue=true;
        
      }
      else if(thisChar==32)
      {
        
      }
 
    }
    //Serial.println(negativeFactor * out);
    if(outCount==0 && hadAValue)
    {
      *longOne=negativeFactor * out;
      outCount++;
      hadAValue=false;
      out=0;
    }
    else if(outCount==1 && hadAValue)
    {
      *longTwo=negativeFactor * out;
      outCount++;
      out=0;
    }
}

unsigned long parseStringToDeciseconds(char* inValue)
//assumes a string in the form MM:SS.T -- anything else, trouble!
{
  for(byte i=0; i<SCREENCHARS; i++) 
  {
    //Serial.print(inValue[i]);
    if(inValue[i]<48  || inValue[i]>58)
    {
      inValue[i]=48;
    }
    //Serial.print(" ");
    //Serial.print(inValue[i]);
    //Serial.println();
  }
  byte minutes=(inValue[0]-48)*10;
  minutes=minutes+(inValue[1]-48);
  byte seconds=(inValue[3]-48)*10;
  seconds=seconds+(inValue[4]-48);
  byte tenths=(inValue[6]-48);
  Serial.print(minutes);
  Serial.print("+");
  Serial.print((long)minutes * 600);
  Serial.print("-");
  Serial.print(seconds);
  Serial.print("-");
  Serial.print(tenths);
  Serial.println();
  return ((long)minutes * 600) + seconds * 10 + tenths;
}


static char *deciSecondsToMinutesSeconds(unsigned long deciSeconds)
//assumes no more than 99 minutes
{
  byte tenths=0;
  byte seconds=0;
  byte minutes=0;
  byte startByte=0;
  unsigned long fullSeconds;
  static char out[9] = {0};
  tenths=deciSeconds % 10;
  fullSeconds=deciSeconds/10;
  seconds = fullSeconds % 60;
  minutes = fullSeconds/60;
  out[0]=' ';
  if(minutes<10)
  {
    out[startByte]='0';
  }
  else
  {
    out[startByte]=(minutes/10)+48;
  }
  out[startByte+1]=(minutes % 10)+48;
  out[startByte+2]=':';
  if(seconds<10)
  {
    out[startByte+3]='0';
  }
  else
  {
    out[startByte+3]=(seconds/10)+48;
  }
  out[startByte+4]=(seconds % 10)+48;
  out[startByte+5]='.';
  out[startByte+6]=tenths + 48;
  out[startByte+7]=0;
  return out;
}

long makeLong(char* arrIn, byte startPos)
//scan an array of char from startPos and parse a signed numeric value 
{
    //Serial.println("spaghetti");
    //Serial.println(arrIn);
    long out=0;
    int negativeFactor=1;
    for(byte i=startPos; i<SER_IN_BUFFER_SIZE-1; i++)//SER_IN_BUFFER_SIZE for now, need something better than that
    {
      
      char thisChar=arrIn[i];
      //Serial.println(thisChar);
      if(thisChar=='-' && out==0)
      {
        negativeFactor=-1;
      }
      else if((byte)thisChar>47 && (byte)thisChar<58) //is char numeric?
      {
        out=(out *10) + (byte)thisChar-48;
        
      }
      else if(thisChar==32)
      {
        
      }
      else //the moment the data is not numeric, return
      {

        return negativeFactor * out;
      }
    }
    //Serial.println(negativeFactor * out);
    return negativeFactor * out;
}

byte decToBcd(byte val)
{
  return ( (val/10*16) + (val%10) );
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
  return ( (val/16*10) + (val%16) );
}

////rtc section////////////



// 1) Sets the date and time on the ds1307
// 2) Starts the clock
// 3) Sets hour mode to 24 hour clock
// Assumes you're passing in valid numbers
void setDateDs1307(byte second,        // 0-59
                   byte minute,        // 0-59
                   byte hour,          // 1-23
                   byte dayOfWeek,     // 1-7
                   byte dayOfMonth,    // 1-28/29/30/31
                   byte month,         // 1-12
                   byte year)          // 0-99
{
  /*
  Serial.print(year);
  Serial.print("-");
  Serial.print(month);
  Serial.print("-");
  Serial.print(dayOfMonth);
  Serial.print(" ");
  
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.print(second);
  Serial.print("/");
  Serial.print(dayOfWeek);
  Serial.println(" ");
   */
   Wire.beginTransmission(DS1307_ADDRESS);
   Wire.write(0);
   Wire.write(decToBcd(second));    // 0 to bit 7 starts the clock
   Wire.write(decToBcd(minute));
   Wire.write(decToBcd(hour));      // If you want 12 hour am/pm you need to set
                                   // bit 6 (also need to change readDateDs1307)
   Wire.write(decToBcd(dayOfWeek));
   Wire.write(decToBcd(dayOfMonth));
   Wire.write(decToBcd(month));
   Wire.write(decToBcd(year));
   Wire.endTransmission();
}

// Gets the date and time from the ds1307
void getDateDs1307(byte *second,
          byte *minute,
          byte *hour,
          byte *dayOfWeek,
          byte *dayOfMonth,
          byte *month,
          byte *year)
{
  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();
  
  Wire.requestFrom(DS1307_ADDRESS, 7);

  // A few of these need masks because certain bits are control bits
  *second     = bcdToDec(Wire.read() & 0x7f);
  *minute     = bcdToDec(Wire.read());
  *hour       = bcdToDec(Wire.read() & 0x3f);  // Need to change this if 12 hour am/pm
  *dayOfWeek  = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month      = bcdToDec(Wire.read());
  *year       = bcdToDec(Wire.read());
}

void WriteDs1307Ram(int address, byte data)
{
  Wire.beginTransmission(DS1307_ADDRESS);   // Select DS1307
  Wire.write(address+8);             // address location starts at 8, 0-6 are date, 7 is control
                          //where address is 0-55 
  Wire.write(data);                // send data
  Wire.endTransmission();
  
  //RTC.writeData(address, data);
}

byte ReadDs1307Ram(byte address)
{
  
  byte out;
  /*
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();
  Wire.requestFrom(DS1307_ADDRESS, 7);
  */
  if(address > 0x3F) 
  { 
    return 0; 
  }
  Wire.beginTransmission(DS1307_ADDRESS);   // Select DS1307
  //Serial.println(address);
  Wire.write((byte)(address+8));
  Wire.endTransmission();
  Wire.requestFrom(DS1307_ADDRESS, 1);
  
            
  // address location starts at 8, 0-6 are date, 7 is control
  //where address is 0-55 
  out=Wire.read();
  return (byte)out;
}

void printRTCDate()
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  Serial.print(year);
  Serial.print("-");
  Serial.print(month);
  Serial.print("-");
  Serial.print(dayOfMonth);
  Serial.print(" ");
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.print(second);
  Serial.println("");
}

#define DAYSPERWEEK (7)
#define DAYSPERNORMYEAR (365U)
#define DAYSPERLEAPYEAR (366U)
#define SECSPERDAY (86400UL) /* == ( 24 * 60 * 60) */
#define SECSPERHOUR (3600UL) /* == ( 60 * 60) */
#define SECSPERMIN (60UL) /* == ( 60) */
#define LEAPYEAR(year)          (!((year) % 4) && (((year) % 100) || !((year) % 400)))

const int _ytab[2][12] = {
{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};


void getDateFromTimeStamp(unsigned long  timestamp, uint16_t epoch)
{
  //algorithm migrated from version in Ruby found here:
  //http://ptspts.blogspot.com/2009/11/how-to-convert-unix-timestamp-to-civil.html
  long s = timestamp%86400; //must be long
  timestamp = timestamp/ 86400;
  byte h = s/3600;
  byte m = s/60%60;
  s = s%60;
  long x = (timestamp*4+102032)/146097+15;
  long b = timestamp+2442113+x-(x/4);
  long c = (b*20-2442)/7305; //must be long
  long d = b-365*c-c/4; //must be long
  byte e = d*1000/30601;
  byte f = d-e*30-e*601/1000;
  int yearCorrection=6685;
  yearCorrection= epoch + 4685;
  if(e < 14)
  {
    Serial.print(c-(yearCorrection+1));
    Serial.print("-");
    Serial.print(e-1);

  }
  else
  {
    Serial.print(c-yearCorrection);
    Serial.print("-");
    Serial.print(e-13);

  }
  Serial.print("-");
  Serial.print(f);
  Serial.print(" ");
  Serial.print(h);
  Serial.print(":");
  Serial.print(m);
  Serial.print(":");
  Serial.print(s);
  //Serial.println("");
}




uint32_t currentTimestamp()
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  getDateDs1307(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
  //Serial.println(dayOfMonth);
  //Serial.println(month);
  //Serial.println(year);
  return getSecsSinceEpoch((uint16_t)2000, (uint8_t)month,  (uint8_t)dayOfMonth,   (uint8_t)year,  (uint8_t)hour,  (uint8_t)minute,  (uint8_t)second);
}



/****************************************************
* Class:Function    : getSecsSinceEpoch
* Input     : uint16_t epoch date (ie, 1970)
* Input     : uint8 ptr to returned month
* Input     : uint8 ptr to returned day
* Input     : uint8 ptr to returned years since Epoch
* Input     : uint8 ptr to returned hour
* Input     : uint8 ptr to returned minute
* Input     : uint8 ptr to returned seconds
* Output        : uint32_t Seconds between Epoch year and timestamp
* Behavior      :
*
* Converts MM/DD/YY HH:MM:SS to actual seconds since epoch.
* Epoch year is assumed at Jan 1, 00:00:01am.
* looks like epoch has to be a date like 1900 or 2000 with two zeros at the end.  1984 will not work!
****************************************************/
uint32_t getSecsSinceEpoch(uint16_t epoch, uint8_t month, uint8_t day, uint8_t years, uint8_t hour, uint8_t minute, uint8_t second)
{
  unsigned long secs = 0;
  int countleap = 0;
  int i;
  int dayspermonth;
  
  secs = years * (SECSPERDAY * 365);
  for (i = 0; i < (years - 1); i++)
  {   
      if (LEAPYEAR((epoch + i)))
        countleap++;
  }
  secs += (countleap * SECSPERDAY);
  
  secs += second;
  secs += (hour * SECSPERHOUR);
  secs += (minute * SECSPERMIN);
  secs += ((day - 1) * SECSPERDAY);
  
  if (month > 1)
  {
      dayspermonth = 0;
  
      if (LEAPYEAR((epoch + years))) // Only counts when we're on leap day or past it
      {
          if (month > 2)
          {
              dayspermonth = 1;
          } else if (month == 2 && day >= 29) {
              dayspermonth = 1;
          }
      }
  
      for (i = 0; i < month - 1; i++)
      {   
          secs += (_ytab[dayspermonth][i] * SECSPERDAY);
      }
  }
  
  return secs;
}

////end rtc section////////////

//other storage funcs



////////////////
//eeprom code
/////////////////

void storelong(unsigned int lowstart, unsigned long datain, byte type)
//stores a long in EEPROM beginning at lowstart (Least Significant Byte first)
//type is as follows: 0: master atmega eeprom, 1: slave atmega eeprom, 2: Ds1307 ram, 3:low EEPROM, 4: high EEPROM
{
  byte j;
  byte thisbyte;
  byte * theByteArray;
  unsigned long thispower;
  int theDeviceAddress;
  if(type==6)
  {
    //fram.WriteLong(0, lowstart, datain);
    return;
  }
  for(j=0; j<4; j++)
  {
    thispower=powerof(256, j);
    thisbyte=datain/thispower;
    if(type==3  || type==4)//had to make these be 13 and 14 instead of 3 and 4 because it wasn't working
    {
      theByteArray[j]=thisbyte;
    }
    else
    {
      storebyte(lowstart+j,thisbyte, type);
    }
    datain=datain-(thisbyte * thispower);
  }
  if (type==3  || type==4)  //had to make these be 13 and 14 instead of 3 and 4 because it wasn't working
  {
    if(type==3)
    {
      theDeviceAddress=(int)EEPROM_LOW_MEM_ADDRESS;
    }
    else
    {
      theDeviceAddress=(int)EEPROM_HIGH_MEM_ADDRESS;
    }
    WireEepromWrite(theDeviceAddress, lowstart, 4, theByteArray) ;
  
  }
  return;
}

unsigned long getlong(unsigned int lowstart, byte type)
//returns the long value stored in EEPROM
//type is as follows: 0: master atmega eeprom, 1: slave atmega eeprom, 2: Ds1307 ram, 3 low eeprom, 4 low eeprom
{
  
  byte i;
  byte thisbyte;
  unsigned long out=0;
  if(type==6)
  {
    //return fram.ReadLong(0, lowstart);
  }
  for(i=0; i<4; i++)
  {
   
    thisbyte=getbyte(lowstart+i, type);
    out=out + (thisbyte * powerof(256, i));
  }
  if(type==1)
  { 
    //not doing this 
  }
  return out;
}

void storeint(unsigned int lowstart, unsigned int datain, byte type)
//stores a long in EEPROM beginning at lowstart (Least Significant Byte first)
//type is as follows: 0: master atmega eeprom, 1: slave atmega eeprom, 2: Ds1307 ram
{
  byte j;
  byte thisbyte;
  unsigned long thispower;
  if(type==6)
  {

    //fram.WriteInt(0, lowstart, datain);
    return;
  }
  for(j=0; j<2; j++)
  {
    thispower=powerof(256, j);
    thisbyte=datain/thispower;
    storebyte(lowstart+j,thisbyte, type);
    datain=datain-(thisbyte * thispower);
  }
  return;
}

unsigned int getint(unsigned int lowstart, byte type)
//returns the long value stored in EEPROM
//type is as follows: 0: master atmega eeprom, 1: slave atmega eeprom, 2: Ds1307 ram
{
  byte i;
  byte thisbyte;
  unsigned long out=0;

  if(type==6)
  {
    //return fram.ReadInt(0, lowstart);
  }
  for(i=0; i<2; i++)
  {
    thisbyte=getbyte(lowstart+i, type);
    out=out + (thisbyte * powerof(256, i));
  }
  return out;
}

void storebyte(unsigned int low,   byte datain, byte type)
{
  if(type==0)
  {
    EEPROM.write(low, datain);
  }
  else if(type==1)
  {
    //not doing this
  }
  else if(type==2)
  {
    WriteDs1307Ram(low,datain);
  }
  else if(type==3)
  {
    WireEepromWriteOneByte(EEPROM_LOW_MEM_ADDRESS, low,  datain);
  }
  else if(type==4)
  {
    WireEepromWriteOneByte(EEPROM_HIGH_MEM_ADDRESS, low,  datain);
  }
  else if(type==6)
  {
    //fram.WriteByte(0, low, datain);
  }
}

byte getbyte(unsigned int low,  byte type)
{
  byte thisbyte, thisotherbye, thisotherotherbyte;
  byte thesevals[4];
  bool bwlFail=true;
  byte count;
  if(type==0)
  {
    thisbyte=EEPROM.read(low);
  }
  else if(type==1)
  {
    //not doing this
  }
  else if(type==2) //RTC memory
  {
    thisbyte =(byte)ReadDs1307Ram(low);
  }
  else if(type==3)
  {
    thisbyte =WireEepromReadByte(EEPROM_LOW_MEM_ADDRESS, low);
  }
  else if(type==4)
  {
    thisbyte = WireEepromReadByte(EEPROM_HIGH_MEM_ADDRESS, low);
  }
  else if(type==6)
  {
    //thisbyte = fram.ReadByte(0,low);
  }
  return thisbyte;
}

byte WireEepromReadByte(int theDeviceAddress, unsigned int theMemoryAddress) 
{
  byte theByteArray[sizeof(byte)];
  WireEepromRead(theDeviceAddress, theMemoryAddress, sizeof(byte), theByteArray);
  return (byte)(((theByteArray[0] << 0)));
}

void WireEepromWrite(int theDeviceAddress, unsigned int theMemoryAddress, int theByteCount, byte* theByteArray) 
{
  for (int theByteIndex = 0; theByteIndex < theByteCount; theByteIndex++) 
  {
    Wire.beginTransmission(theDeviceAddress);
    Wire.write((byte)((theMemoryAddress + theByteIndex) >> 8));
    Wire.write((byte)((theMemoryAddress + theByteIndex) >> 0));
    Wire.write(theByteArray[theByteIndex]);
    Wire.endTransmission();
    delay(5);
  }
}

void WireEepromWriteOneByte(int theDeviceAddress, unsigned int theMemoryAddress, byte theByte) 
{
 
  Wire.beginTransmission(theDeviceAddress);
  Wire.write((byte)((theMemoryAddress ) >> 8));
  Wire.write((byte)((theMemoryAddress) >> 0));
  Wire.write(theByte);
  Wire.endTransmission();
  delay(5);
}

void WireEepromRead(int theDeviceAddress, unsigned int theMemoryAddress, int theByteCount, byte* theByteArray) 
{
  int theByteIndex;
  for (theByteIndex = 0; theByteIndex < theByteCount; theByteIndex++) 
  {
    Wire.beginTransmission(theDeviceAddress);
    Wire.write((byte)((theMemoryAddress + theByteIndex) >> 8));
    Wire.write((byte)((theMemoryAddress + theByteIndex) >> 0));
    Wire.endTransmission();
    //delay(5);
    Wire.requestFrom(theDeviceAddress, sizeof(byte));
    theByteArray[theByteIndex] = Wire.read();
  }
}
//end other storage funcs

//////////////////////////////////////
//FUNCTIONS SPECIFIC TO THE MAX7219:
//////////////////////////////////////
// Put extracted character on Display
void printCharWithShift(char c, int shift_speed)
{
  //if (c < 32) return;
  c -= BASECHAR;
  
  memcpy_P(buffer, CH + 7*c, 7);
  m.writeSprite(maxInUse*8, 0, buffer);
  m.rotateLeft();

  m.setColumn(maxInUse*8 + buffer[0], 0);
  
  for (int i=0; i<buffer[0]+1; i++) 
  {
    delay(shift_speed);
    //m.shiftLeft(false, false);
    m.shiftDown(false);
    //m.rotateLeft();

  }
}
 
// Extract characters from Scrolling text
void printStringWithShift(char* s, int shift_speed)
{
  while (*s != 0){
    printCharWithShift(*s, shift_speed);
    s++;
  }
}

// display unmoving text
void printStringStatic(char* s, byte type)
{
  int col = 0;
  while (*s != 0)
  {
    //if (*s < 32) continue;
    char c = *s - BASECHAR;
    if(type==0) //not really using type but we could for alternative char sets
    {
      memcpy_P(buffer, CH + 7*c, 7);
    }
    else
    {
      memcpy_P(buffer, CH + 7*c, 7);
    }
    m.writeSprite(col, 0, buffer);
    m.rotateLeft();
 
    m.setColumn(col + buffer[0], 0);
    col += buffer[0] + 1;
    s++;
  }
}
