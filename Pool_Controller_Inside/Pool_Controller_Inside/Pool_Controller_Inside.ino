/*
 Xively Feed: http://xively.com/feeds/65673/workbench
 
 Xbee Shield switch: to upload sketch, slide switch away from edge
 
 
 To do:
 Consider tracking std dev of pressure.  Looking at COSM data, 100 pts is a pretty good sample size
 There is a library for this: http://arduino.cc/playground/Main/Statistics
 SD < 0.3 is nice and stable.  Over .75 it's starting to flucteate, over 1 is getting bad, over 2 you definitely have problems
 See if you can reduce RAM so you can run on Uno or Leonardo
 Reboot if xively feed freezes
 If pump is running at night, send text alert
 Send tweet if lost xbee communication
 See if you can update water added today every minute, not just when it's complete it's cycle
 Send tweet if low battery
 
 
 Figure out how to send commands to outside xbee from inside xbee, then see if you can shut pump off from iphone.
 Turn pump off from iphone/website http://www.yaler.org/
 Instructables for Yaler and Arduino http://www.instructables.com/id/Arduino-Web-LED/step5/Accessing-and-controlling-the-Arduino-from-the-Web/
 
 
 ========================================================================================
 xBee Series 1 Receiver - this is the COORDINATOR
 
 Source: http://code.google.com/p/xbee-arduino/
 Xbee ver 0.3 wont compile unless you change NewSoftSerial.h to SoftwareSerial.h.
 see: http://arduino.cc/forum/index.php?topic=84789.0
 How to configure xBees http://code.google.com/p/xbee-api/wiki/XBeeConfiguration
 xbee.h Documentation http://xbee-arduino.googlecode.com/svn/trunk/docs/api/index.html
 
 Configure this Xbee with X-CTU (values are hex)
 PAN 2323  Personal Area Network ID - all xBees need to be on same PAN
 CH C      Channel, XBees on same network have to use the same channel
 My 250    ID of xBee, all xBees need to be different.
 CE 1      Enable Coordinator - sets this XBeef as the Coordinator
 AP 2      API
 DL 90    decimal Lower Byte Address (not used in 16-bit addressing)
 Firmware 10E8
 
 Hardware
 Arduino Mega (Uno doesn't have enough RAM)
 XBee Series 1 Pro, chip antenna
 Xbee shield from sparkfun http://www.sparkfun.com/products/9588, (switch away from edge to upload program)
 
 
 xively Streams
 http://xively.com/feeds/65673/workbench
 0 Pressure before filter
 1 Pressure after filter
 2 Pressure drop across filter
 3 Temp before heater
 4 Temp after heater
 5 Heater Status
 6 Temp pump housing
 7 Pump Amps
 8 Low pressure counter
 9 Water fill minutes today
 10 Status: Pump off, pump on, water fill on, swithch off, emergency shutdown
 11 successes
 12 failures
 
 Status code numbers from outside controller
 0 - pump off - normal
 1 - pump on
 2 - adding water
 3 - Pump off - manual (via pump switch)
 4 - emergency pump off - low pressure - fluctuations
 5 - emergency pump off - low pressure - continiously low for 5 minutes
 6 - emergency pump off - high amps
 7 - emergency pump off - high pump temp
 8 - emergency pump off - shutdown from web
 
 
 To send data to this particular xbee, the transmitter uses the Receiver's MY address, not the DL address
 See http://www.digi.com/support/kbase/kbaseresultdetl?id=2187
 
 /* XBee packet structure
 0 Temp Pre Heater
 1 Temp Post Heater
 2 Temp Pump houseing
 3 Pump Amps
 4 Pressure pre-filter
 5 Pressure post filter
 6 Pressure Water Fill
 7 Low pressure counter
 8 Controller Status Number
 9 Minutes of water added today
 10 Water fill countdown
 11 Pool time
 12 Water level sensor battery voltage
 13 Low Water Level - calculated
 14 Sensor Input Status Byte
 15 Dicrete I/O status byte
 
 */


// #define WDT          // Watch Dog timer
#define PRINT_DEBUG     // Comment out to turn of serial printing

#include <SPI.h>             // Communicate with SPI devices http://arduino.cc/en/Reference/SPI
#include <Ethernet.h>        // LIbrary for Arduino ethernet shield http://arduino.cc/en/Reference/Ethernet
#include <HttpClient.h>      // https://github.com/amcewen/HttpClient/blob/master/HttpClient.h
#include <Xively.h>          // http://github.com/xively/xively_arduino
#include <Twitter.h>         // http://arduino.cc/playground/Code/TwitterLibrary, get token from token at http://arduino-tweet.appspot.com/
#include <XBee.h>            // http://code.google.com/p/xbee-arduino/     Modified per http://arduino.cc/forum/index.php/topic,111354.0.html
#include <avr/wdt.h>         // Watchdog timer, Example code: http://code.google.com/p/arduwind/source/browse/trunk/ArduWind.ino
#include <Tokens.h>          // Tokens for COSM and twitter
#include "LocalLibrary.h"    // Include application, user and local libraries

#define BAUD_RATE 9600  // Baud for bith Xbee and serial monitor

// Array positions for pool data array poolData[], cosmData[] and pooliinfo[]
#define P_TEMP1               0   // Temperature before heater
#define P_TEMP2               1   // Temperature after
#define P_TEMP_PUMP           2   // Pump housing temperature
#define P_PUMP_AMPS           3   // Amps pump is using
#define P_PRESSURE1           4   // Pressure before filter
#define P_PRESSURE2           5   // Pressure after filter
#define P_PRESSURE3           6   // Pressure of water fill line
#define P_LOW_PRES_CNT        7   // Counts times pressure was low
#define P_CONTROLLER_STATUS   8   // Controller status 0-8
#define P_WATER_FILL_MINUTES  9   // Minutes water fill valve was open today
#define P_WATER_FILL_COUNTDN 10   // Countdown timer for water fill valve
#define P_POOL_TIME          11   // Pool time from RTC, 2:45 PM = 14.75
#define P_WATER_LVL_BATT     12   // Water level battery voltage
#define P_LOW_WATER          13   // Low water sensor: 0 = level ok, 1 = low water, 2 = offline
#define P_SENSORSTATUSBYTE   14   // Sensor Inputs Status Byte: 1 if sensor is working properly, 0 of not
#define P_IOSTATUSBYTE       15   // Discrete I/O status byte: shows on/off state if I/O
#define NUM_POOL_DATA_PTS    16   // Number of data points in pool array xbee packet

byte sensorStatusbyte;          // Each bit determines if sensor is operating properly
byte ioStatusbyte;              // Each bit shows input value of digital I/O

/*
 Sensors working ok Status Byte: 1 if sensor is working properly, 0 of not
 sensorStatusbyte
 0 Pre-heat temperature
 1 Post-heat temperature
 2 Pump temperature
 3 Pre-filter pressure
 4 Post-filter pressure
 5 Water fill pressure
 6 pump amps
 7 Water level sensor
 
 Discrete I/O status byte: shows on/off state if I/O
 ioStatusbyte
 0 Pump on/off relay
 1 Auto-Off-On switch is in Auto Position
 2 Auto-Off-On switch is in On Position
 3 Water fill LED
 4 Water fill pushbutton input
 5 Water fill valve relay
 6 Heater on/off relay output
 7 Water Level Sensor (real time)
 */

// Xively Stream IDs
#define STREAM_PRESSURE1           "0"   // Pressure before filter
#define STREAM_PRESSURE2           "1"   // Pressure after filter
#define STREAM_PRESSURE3           "2"   // Water Fill Pressure
#define STREAM_TEMP1               "3"   // Temp before heater
#define STREAM_TEMP2               "4"   // Temp after heater
#define STREAM_HTR_STATUS          "5"   // Heater On/Off Status
#define STREAM_TEMP_PUMP           "6"   // Pump housing temperature
#define STREAM_PUMP_AMPS           "7"   // Pump amps
#define STREAM_LOW_PRES_CNT        "8"   // Low Pressure Count
#define STREAM_FILL_MINUTES        "9"   // Minutes water fill valve was otpen today
#define STREAM_CTRL_STATUS_TXT    "10"   // Status of controller - text
#define STREAM_SUCCESS            "11"   // Xively upload successes
#define STREAM_FAILURE            "12"   // Xively Network Failures
#define STREAM_CTRL_STATUS_CODE   "13"   // Status of controller - number
#define STREAM_LEVEL_SENSOR       "14"   // Water level sensor
#define STREAM_BATTERY            "15"   // battery volts for water level sensor
#define NUM_XIVELY_STREAMS         16

#define TWEETMAXSIZE              60   // Character array size for twitter message
#define COSM_UPDATE_INTERVAL   15000   // COSM upload interval (mS)
#define COSM_UPDATE_TIMEOUT  1800000   // 30 minute timeout - if there are no successful updates in 30 minutes, reboot
#define FEED_ID 65673                  // Xively Feed ID    https://cosm.com/feeds/65673
// #define FEED_ID 4663  // Test feed
uint32_t cosm_uploadTimout_timer; // Timer to reboot if no successful uploads in 30 minutes
uint32_t cosm_upload_timer;   // Timer for uploading to COSM

const int bufferSize = 30;
char bufferValue[bufferSize]; // enough space to store the string we're going to send

XivelyDatastream datastreams[] =
{
  XivelyDatastream(STREAM_PRESSURE1,        strlen(STREAM_PRESSURE1),        DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_PRESSURE2,        strlen(STREAM_PRESSURE2),        DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_PRESSURE3,        strlen(STREAM_PRESSURE3),        DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_TEMP1,            strlen(STREAM_TEMP1),            DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_TEMP2,            strlen(STREAM_TEMP2),            DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_HTR_STATUS,       strlen(STREAM_HTR_STATUS),       DATASTREAM_INT),
  XivelyDatastream(STREAM_TEMP_PUMP,        strlen(STREAM_TEMP_PUMP),        DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_PUMP_AMPS,        strlen(STREAM_PUMP_AMPS),        DATASTREAM_FLOAT),
  XivelyDatastream(STREAM_LOW_PRES_CNT,     strlen(STREAM_LOW_PRES_CNT),     DATASTREAM_INT),
  XivelyDatastream(STREAM_FILL_MINUTES,     strlen(STREAM_FILL_MINUTES),     DATASTREAM_INT),
  XivelyDatastream(STREAM_CTRL_STATUS_TXT,  strlen(STREAM_CTRL_STATUS_TXT),  DATASTREAM_BUFFER, bufferValue, bufferSize),
  XivelyDatastream(STREAM_SUCCESS,          strlen(STREAM_SUCCESS),          DATASTREAM_INT),
  XivelyDatastream(STREAM_FAILURE,          strlen(STREAM_FAILURE),          DATASTREAM_INT),
  XivelyDatastream(STREAM_CTRL_STATUS_CODE, strlen(STREAM_CTRL_STATUS_CODE), DATASTREAM_INT),
  XivelyDatastream(STREAM_LEVEL_SENSOR,     strlen(STREAM_LEVEL_SENSOR),     DATASTREAM_INT),
  XivelyDatastream(STREAM_BATTERY,          strlen(STREAM_BATTERY),          DATASTREAM_FLOAT)
};

// Wrap the datastreams into a feed
XivelyFeed feed(FEED_ID, datastreams, NUM_XIVELY_STREAMS);

EthernetClient client;
XivelyClient xivelyclient(client);


// Ethernet Setup
byte mac[] = { 0xCC, 0xAC, 0xBE, 0x21, 0x91, 0x43 };
uint8_t successes = 0;    // COSM upload success, will rollover at 255, but that's okay.  This makes is easy to see on COSM is things are running
uint8_t failures = 0;     // COSM upload failures


// Xbee Setup stuff
XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
// create reusable response objects for responses we expect to handle
Rx16Response rx16 = Rx16Response();
uint8_t xbeeErrors;      // XBee Rx errors
bool gotNewData;         // Flag to indicate that sketch has received new data from xbee
uint32_t xbeeTimeout;    // Counts time between successful Xbee data, if it goes too long, it means we've lost our connection to Xbee
bool xBeeTimeoutFlag;   // Flag to indicate no date from Xbee, used to keep warning from going off every 5 minutes


// I/O Setup
#define LED_XBEE_ERROR      6  // LED flashes when XBee error
#define LED_XBEE_SUCCESS    7  // LED flashes when XBee success
#define LED_COSM_ERROR      6  // LED flashes when Xively error
#define LED_COSM_SUCCESS    5  // LED flashes when Xively success


// Declare function prototypes
void PrintPoolData(float *poolData);
void flashLed(int pin, int times, int wait);
void software_Reset();
bool SendDataToXively(float *xivelyData);
bool ReadXBeeData(float *poolData, uint16_t *Tx_Id);
int SendTweet(char msgTweet[], double fpoolTime);
int freeRam(bool PrintRam);
void controllerStatus(char * txtStatus, int poolstatus);

#ifdef WDT
void WatchdogSetup(void);
void WDT_ForceTimeout();
#endif


// Token for Twitter Account, PachubeAlert
Twitter twitter(TWITTER_TOKEN);



//=========================================================================================================
//============================================================================
void setup(void)
{
  delay(1000);
  pinMode(LED_XBEE_ERROR, OUTPUT);
  pinMode(LED_XBEE_SUCCESS, OUTPUT);
  pinMode(LED_COSM_ERROR, OUTPUT);
  pinMode(LED_COSM_SUCCESS, OUTPUT);
  
  
  // disable microSD card interface on Ethernet shield
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  
  Serial.begin(BAUD_RATE);
  
#ifdef PRINT_DEBUG
  Serial.println(F("Setup"));
#endif
  
  // Initialize XBee
  xbee.begin(BAUD_RATE);
  
  // Initialize Ethernet
  Ethernet.begin(mac);
  delay(1000);
  
#ifdef WDT
  WatchdogSetup();// setup Watch Dog Timer to 8 sec
  wdt_reset();
#endif
  
  
  // Setup flashes LEDs so you know Arduino is booting up
  // parameters, Pin#, times to flash, wait time
  flashLed(LED_XBEE_ERROR,   6, 40);
  flashLed(LED_XBEE_SUCCESS, 6, 40);
  flashLed(LED_COSM_ERROR,   6, 40);
  flashLed(LED_COSM_SUCCESS, 6, 40);
  
  // Initialize global variables
  cosm_uploadTimout_timer = millis() + COSM_UPDATE_TIMEOUT;
  cosm_upload_timer       = millis() + COSM_UPDATE_INTERVAL;
  
  
  xbeeTimeout       = millis() + 300000UL; // 5 minutes
  xBeeTimeoutFlag   = false;
  gotNewData        = false;  // Initialize, true when new we receive new data from xbee, is reset when it uploads to COSM
  
#ifdef PRINT_DEBUG
  Serial.print(F("End setup "));
  freeRam(true);
#endif
}  //setup()


//=========================================================================================================
//============================================================================
void loop(void)
{
  
  static float poolData[NUM_POOL_DATA_PTS];  // Array to hold pool data received from outside controller
  
  // Flags so Twitter messages are only sent once
  static bool tf_highPresDrop;    // High pressure across filter
  static bool HighPressureFlag;          // Pressure before filter
  static bool LowPressureFlag;           // Low pressure
  static bool HighAmpsFlag;              // High pump amps
  static bool HighPumpTempFlag;          // High pump temperature
  static bool eShutdownFlag;             // Emergency shutdown flag
  static bool waterFillCntDnFlag;        // Water fill valve is on
//  static bool tf_Pump	OnAtNight;         // Pump on at night
  
  
  
  uint16_t xbeeID;                // ID of transimitting xbee
  char msgTweet[TWEETMAXSIZE];    // Holds text for twitter message.  Should be big enough for message and timestamp
  
  // If Arduino recently restarted (last 10 seconds), set waterFillCntDnFlag to true so it
  // doesn't send a tweet if the water fill is on
  if(millis() < 10000)
  { waterFillCntDnFlag = true; }
  
  
  // Read XBee data
  // Keep tying to read data until successful.  After 30 tries, give up and move on
  bool xbeeStat;
  byte xbeeFailCnt = 0;
  do
  {
    delay(250);
    xbeeStat = ReadXBeeData(poolData, &xbeeID);
    xbeeFailCnt++;
  } while (xbeeStat == false && xbeeFailCnt < 30 );
  
  // Send a Tweet for startup, but this after you read xbee data so you can append pooltime which avoids duplicate tweets
  static bool tweetstartup;
  if (tweetstartup == false && millis() > 20000)
  {
    strcpy(msgTweet, "Pool Arduino has restarted.");
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
    tweetstartup = true;
  }
  
  // Check for Xbee Timeout: 5 minutes
  if(((long)(millis() - xbeeTimeout) >= 0 ) && ( xBeeTimeoutFlag == false))
  {
    xBeeTimeoutFlag = true;
#ifdef PRINT_DEBUG
    Serial.println(F("No Rx data from XBee in 5 min"));
#else
    strcpy(msgTweet, "Lost XBee communication.");
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
#endif
  }
  
#ifdef WDT
  wdt_reset();  // Reset watchdog time
#endif
  
  // Upload data to COSM
  if ((long)(millis() - cosm_upload_timer) >= 0)
  {
    cosm_upload_timer = millis() + COSM_UPDATE_INTERVAL;  // Reset timer
#ifdef WDT
    wdt_reset();  // Reset watchdog timer
#endif
    
    // Send Data to Xively
    SendDataToXively(poolData);
  }
  
  // High Pump Amps
  // Outside arduino will shut down pump if amps > 20
  if(poolData[P_PUMP_AMPS] >= 17 && HighAmpsFlag == false)
  {
    HighAmpsFlag = true;
    sprintf(msgTweet, "High pump amps: %d.", (int) poolData[P_PUMP_AMPS]);
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset high amps flag
  if(poolData[P_PUMP_AMPS] < 10 && HighAmpsFlag == true)
  { HighAmpsFlag = false; }
  
  // High Pump temperature
  // Outside arduino will shut down pump at if temperatrue > 180 F
  if(poolData[P_TEMP_PUMP] >= 175 && HighPumpTempFlag == false)
  {
    HighPumpTempFlag = true;
    sprintf(msgTweet, "High pump temp: %d.", (int) poolData[P_TEMP_PUMP]);
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset high pump temp flag
  if(poolData[P_TEMP_PUMP] < 110 && HighPumpTempFlag == true)
  {
    HighPumpTempFlag = false;
  }
  
  // High Pump pressure
  if(poolData[P_PRESSURE1] >= 40 && HighPressureFlag == false)
  {
    HighPressureFlag = true;
    sprintf(msgTweet, "High pump pressure: %d.", (int) poolData[P_PRESSURE1]);
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset high amps flag
  if(poolData[P_PRESSURE1] < 110 && HighPressureFlag == true)
  {
    HighPressureFlag = false;
  }
  
  // Check pressure drop across filter
  if((poolData[P_PRESSURE1] > 25.0) && ((poolData[P_PRESSURE1] - poolData[P_PRESSURE2]) > 10.0) && (tf_highPresDrop == false))
  {
    tf_highPresDrop = true;
    strcpy(msgTweet, "High filter pressure.");
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset HighPressureFlag
  if(poolData[P_PRESSURE1] < 2 && tf_highPresDrop == true)
  {
    tf_highPresDrop = false;
  }
  
  // Check Low Pressure Counter
  if(poolData[P_LOW_PRES_CNT] >= 17 && LowPressureFlag == false)
  {
    LowPressureFlag = true;
    strcpy(msgTweet, "Low pressure fluctuations at pool pump.");
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset low pressure flag
  if(poolData[P_LOW_PRES_CNT] == 0 && LowPressureFlag == true)
  {
    LowPressureFlag = false;
  }
  
  // Check for emergency shutdown and send tweet
  if(poolData[P_CONTROLLER_STATUS] >= 4 && eShutdownFlag == false)
  {
    char txtShutdown[17+1];
    eShutdownFlag = true;
    strcpy(msgTweet, "Emergency Shutdown - ");
    controllerStatus(txtShutdown,  poolData[P_CONTROLLER_STATUS]);
    strcat(msgTweet, txtShutdown);
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
  }
  
  // Reset emergency shutdown flag
  if(poolData[P_CONTROLLER_STATUS] < 4 && eShutdownFlag == true)
  {
    eShutdownFlag = false;
  }
  
  
  // Send tweet if water fill timer is started
  if(poolData[P_WATER_FILL_COUNTDN] > 1 && waterFillCntDnFlag == false)
  {
    // Wait a couple seconds in ccase water fill button is pressed a couple times, then read XBee data again
    delay(2500);
    ReadXBeeData(poolData, &xbeeID);
    sprintf(msgTweet, "Water fill started for %d minutes.", (int) poolData[P_WATER_FILL_COUNTDN]);
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
    waterFillCntDnFlag = true;  // flag so Tweet is only sent once
  }
  
  // Reset water fill timer flag
  if(poolData[P_WATER_FILL_COUNTDN] == 0 && waterFillCntDnFlag == true)
  {
    waterFillCntDnFlag = false;
  }
  
  // Reboot if no successful updates in 30 minutes
  if((long) (millis() - cosm_uploadTimout_timer) >= 0)
  {
    strcpy(msgTweet, "Reboot: No uploads to COSM in over 30 minutes.");
    SendTweet(msgTweet, poolData[P_POOL_TIME]);
    cosm_uploadTimout_timer = millis() + COSM_UPDATE_TIMEOUT;
    delay(1000);
#ifdef WDT
    WDT_ForceTimeout();
#else
    software_Reset();
#endif
  }
  
  // If PRINT_DEBUG is on, then delay 1000 so you don't fill up the serial monitor too fast
#ifdef PRINT_DEBUG
  delay(1000);
#endif
  
} // loop()


//=========================================================================================================
// Send twitter text, appends the time to the message to avoid twitter blocking duplicate messages
//======================================================================================
int SendTweet(char * txtTweet, double fpoolTime)
{
  
  char cpoolTime[19];   // char arry to hold pool time
  
  if(strlen(txtTweet) <= TWEETMAXSIZE - 20) // Make sure message there is room in character array for the timestamp
  {
    sprintf(cpoolTime, " Pool Time: %01d:%02d", int(floor(fpoolTime)), (int)((fpoolTime - floor(fpoolTime)) * 60));
    strcat(txtTweet, cpoolTime);          // Append pool time decimal format to the message
  }
  
  if (twitter.post(txtTweet))
  {
    // Specify &Serial to output received response to Serial.
    // If no output is required, you can just omit the argument, e.g.
    // int tweetStatus = twitter.wait();
#ifdef PRINT_DEBUG
    int tweetStatus = twitter.wait(&Serial);
#else
    int tweetStatus = twitter.wait();
#endif
    if (tweetStatus == 200)
    {
#ifdef PRINT_DEBUG
      Serial.println(F("Twitter OK."));
#endif
      return 200;
    }
    else
    {
#ifdef PRINT_DEBUG
      Serial.print(F("Twitter failed : code "));
      Serial.println(tweetStatus);
#endif
      return tweetStatus;
    }
  }
  else
  {
#ifdef PRINT_DEBUG
    Serial.println(F("Twitter connection failed."));
#endif
    return 0;
  }
  
#ifdef PRINT_DEBUG
  // Print Tweet to serial monitor
  Serial.print(F("Tweet: "));
  Serial.println(txtTweet);
#endif
  
  cosm_upload_timer = millis() + COSM_UPDATE_INTERVAL; // increase timer for COSM so it doesn't send right after Twitter
  
  
  
} // SendTweet()


//=========================================================================================================
// Send data to Xively
// cosmData array is the same as the poolData array in loop function
// Before sending, check to make sure data is realistic.  If not, don't send
//======================================================================================================================================
bool SendDataToXively(float *xivelyData)
{
  
#ifdef WDT
  wdt_reset();  // Reset watchdog time
#endif
  
#ifdef PRINT_DEBUG
  Serial.println(F("\n\nSend to Xively"));
#endif
  
  char textForCOSM[17+1];  // Used by dtostrf() & controllerStatus()
  
  if (gotNewData == true)
  {
    if(xivelyData[P_PRESSURE1] < 40)  // check for a valid pressure
    { datastreams[0].setFloat(xivelyData[P_PRESSURE1]); }  // 0 - Pre-filter pressure
    
    if(xivelyData[P_PRESSURE2] < 40)  // check for a valid pressure
    { datastreams[1].setFloat(xivelyData[P_PRESSURE2]); }  // 1- Post-Filter pressure
    
    if(xivelyData[P_PRESSURE3] < 100)  // check for a valid pressure
    { datastreams[2].setFloat(xivelyData[P_PRESSURE3]); }  // 2- water fill pressure
    
    if(xivelyData[P_TEMP1] > 40 && xivelyData[P_TEMP1] < 150)
    { datastreams[3].setFloat(xivelyData[P_TEMP1]); }    // 3 - Pre heater temp
    
    if(xivelyData[P_TEMP2] > 40 && xivelyData[P_TEMP2] < 150)
    { datastreams[4].setFloat(xivelyData[P_TEMP2]); }    // 4 - Post heater temp
    
    // Determine if Pool heater is on by comparing temperatures
    if(((xivelyData[P_TEMP2] - xivelyData[P_TEMP1]) > 5.0) && (xivelyData[P_PUMP_AMPS] > 5.0))
    { datastreams[5].setInt(1); }              // 5 - Heater status On/Off
    else
    { datastreams[5].setInt(0); }
    
    if(xivelyData[P_TEMP_PUMP] > 40 && xivelyData[P_TEMP_PUMP] < 300)
    { datastreams[6].setFloat(xivelyData[P_TEMP_PUMP]); } // 6 - Pump temp
    
    if(xivelyData[P_PUMP_AMPS] < 50)
    { datastreams[7].setFloat(xivelyData[P_PUMP_AMPS]); } // 7 - Pump amps
    
    datastreams[8].setInt((int) xivelyData[P_LOW_PRES_CNT]); // 8 - Low pressure counter
    
    datastreams[9].setInt((int) xivelyData[P_WATER_FILL_MINUTES]); // 9 - Mintues water fill valve was on today
    
    datastreams[13].setInt((int) xivelyData[P_CONTROLLER_STATUS]); // 13 - Controller status code number
    
    datastreams[14].setInt(xivelyData[P_LOW_WATER] ); // 14 - water level sensor - Calculated
    
    datastreams[15].setFloat(xivelyData[P_WATER_LVL_BATT]/1000.0); // 15 - battery volts for water level sensor
    
  } // if gotNewData
  
  gotNewData = false; // reset got xbee data flag
  
  if(xBeeTimeoutFlag == true)
  { // No communication with XBEE
    Serial.println(F("No Xbee Comm"));
    datastreams[10].setBuffer("NO XBEE COMM");  // 10 - Controller status - sends text to COSM, not numbers
  }
  else
  {
    controllerStatus(textForCOSM,  xivelyData[P_CONTROLLER_STATUS]);  // Convert controller status number to text
    datastreams[10].setBuffer(textForCOSM);                           // 10 - Controller status - sends text to COSM, not numbers
  }
  
  datastreams[11].setInt(successes); // 11 - network successes
  
  datastreams[12].setInt(failures); // 12 - network failures
  
  
  // Send data to Xively
  int ret = xivelyclient.put(feed, XIVELY_API_KEY);
  switch (ret)
  {
    case 200:
      cosm_uploadTimout_timer = millis() + COSM_UPDATE_TIMEOUT; // Reset upload timout timer
      successes++;
      failures = 0;
#ifdef PRINT_DEBUG
      Serial.println("Successful Xively upload");
#endif
      // Flash twice when we send data to COSM successfully
      flashLed(LED_COSM_SUCCESS, 1, 150);
#ifdef WDT
      wdt_reset();
#endif
      return true;
      break;
    case HTTP_ERROR_CONNECTION_FAILED:
      failures++;
      flashLed(LED_COSM_ERROR, 1, 150);
#ifdef PRINT_DEBUG
      Serial.println(F("\nconnection to api.xively.com has failed. Failures = "));
      Serial.println(failures);
#endif
      return false;
      break;
    case HTTP_ERROR_API:
      failures++;
      flashLed(LED_COSM_ERROR, 1, 150);
#ifdef PRINT_DEBUG
      Serial.println(F("\nA method of HttpClient class was called incorrectly. Failures = "));
      Serial.println(failures);
#endif
      return false;
      break;
    case HTTP_ERROR_TIMED_OUT:
      failures++;
      flashLed(LED_COSM_ERROR, 1, 150);
#ifdef PRINT_DEBUG
      Serial.println(F("\nConnection with api.xively.com has timed-out. Failures = "));
      Serial.println(failures);
#endif
      return false;
      break;
    case HTTP_ERROR_INVALID_RESPONSE:
      failures++;
      flashLed(LED_COSM_ERROR, 1, 150);
#ifdef PRINT_DEBUG
      Serial.println(F("\nInvalid or unexpected response from the server. Failures = "));
      Serial.println(failures);
#endif
      return false;
      break;
    default:
      failures++;
      flashLed(LED_COSM_ERROR, 1, 150);
#ifdef PRINT_DEBUG
      Serial.print(F("\nXively Unknown status: "));
      Serial.print(ret);
      Serial.print(F("  Failures = "));
      Serial.println(failures);
#endif
      return false;
  }
}  // SendDataToXively()



//=========================================================================================================
//==================================================================================================================
bool ReadXBeeData(float *poolData, uint16_t *Tx_Id)
{
  
  // Read XBee data
  xbee.readPacket();
  
  if (xbee.getResponse().isAvailable())
  {
    // got something
    if (xbee.getResponse().getApiId() == RX_16_RESPONSE )
    {
      // got a Rx packet
      xbee.getResponse().getRx16Response(rx16);  // I think this tells XBee to send the data over, not sure
      int dataLength = rx16.getDataLength();     // Get number of bytes of data sent from outside XBee
      uint16_t RxData[dataLength];               // Array to hold raw XBee data
      *Tx_Id = rx16.getRemoteAddress16();        // MY ID of Tx, remember MY is set as a hex number.  Useful if you have multiple transmitters
      
      // Convert data from bytes to integers, except last two bytes in array which are status bytes and don't get converted
      for(int i=0; i < dataLength; i=i+2)
      {
        RxData[i/2]  = rx16.getData(i) << 8;
        RxData[i/2] |= rx16.getData(i+1);
        poolData[i/2] = (float) RxData[i/2] / 10.0; // value from XBee are 10x, convert back to normal size, except last 2 status bytes
      }
      
      // But status byts into byte variables
      sensorStatusbyte = poolData[P_SENSORSTATUSBYTE];    // Each bit determines if sensor is operating properly
      ioStatusbyte = poolData[P_IOSTATUSBYTE];            // I/O state of digital I/O
      
      gotNewData = true;
      xbeeTimeout = millis() + 300000UL; // Reset xbee timeout timer, 5 minutes
      xBeeTimeoutFlag = false;
      
#ifdef PRINT_DEBUG
      //        Serial.print(F("Tx ID\t"));    Serial.println(*Tx_Id, HEX);
      //        Serial.print(F("RSSI\t"));     Serial.println(rx16.getRssi());
      //        Serial.print(F("DataLen\t"));  Serial.println(dataLength);
      PrintPoolData(poolData);
#endif
      return true;
    }
    else // Didn't get a RX_16_RESPONSE
    {
      // Got a Response, but not RX_16_RESPONSE
#ifdef PRINT_DEBUG
      Serial.println(F("Got XBee Response, but not RX_16_RESPONSE"));
#endif
      flashLed(LED_XBEE_ERROR, 1, 150);
      xbeeErrors++;
      return false;
    }
  }
  // XBee is not available
  else if (xbee.getResponse().isError())
  {
    // Got something, but not a packet
    // You get error code 0 when the other Xbee isn't sending anydata over
#ifdef PRINT_DEBUG
    //srg      Serial.print(F("XBee error reading packet. Err code: "));
    //      Serial.println(xbee.getResponse().getErrorCode());
#endif
    flashLed(LED_XBEE_ERROR, 1, 150);
    return false;
  } // End Got Something
  else
  {
    // xbee not available and no error
#ifdef PRINT_DEBUG
    //srg      Serial.print(F("XBee not avail, Err code: "));
    //      Serial.println(xbee.getResponse().getErrorCode());
#endif
    flashLed(LED_XBEE_ERROR, 1, 150);
    return false;
  }
  
} // ReadXbeeData()


//=========================================================================================================
// Print data sent from Pool Xbee
//=========================================================================================================
void PrintPoolData(float *poolinfo)
{
  static byte printheadcounter;
  
  // Print heading every 15 rows
  if (printheadcounter == 0)
  {
    Serial.println(F("\nTemp1\tTemp2\ttemp3\tAmps\tPres1\tPres2\tPres3\tP-Cnt\tWFmin\tbatt\tstat\tsesnsor\tI/O"));
    printheadcounter = 15;
  }
  
  printheadcounter--;
  
  Serial.print(poolinfo[P_TEMP1],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_TEMP2],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_TEMP_PUMP],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_PUMP_AMPS],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_PRESSURE1],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_PRESSURE2],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_PRESSURE3],1);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_LOW_PRES_CNT],0);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_WATER_FILL_MINUTES],0);
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_WATER_LVL_BATT]/1000.0,2);  // battery volts
  Serial.print(F("\t"));
  Serial.print(poolinfo[P_CONTROLLER_STATUS],0);
  Serial.print(F("\t"));
  Serial.print(sensorStatusbyte, BIN);
  Serial.print(F("\t"));
  Serial.print(ioStatusbyte, BIN);
  Serial.println();
  
} // PrintPoolData()


//=========================================================================================================
// Return the text for the pool controller status
//=========================================================================================================
void controllerStatus(char * txtStatus, int poolstatus)
{
  // txtStatus can hold 15 characters
  
  switch(poolstatus)
  {
    case 0:
      strcpy(txtStatus, "Pump Off");
      break;
    case 1:
      strcpy(txtStatus, "Pump On");
      break;
    case 2:
      strcpy(txtStatus, "Swtch Off");
      break;
    case 3:
      strcpy(txtStatus, "Water Filling");
      break;
    case 4:
      strcpy(txtStatus, "LO PRES Fluctuations");
      break;
    case 5:
      strcpy(txtStatus, "LO PRES 5 MIN");
      break;
    case 6:
      strcpy(txtStatus, "HI AMPS!");
      break;
    case 7:
      strcpy(txtStatus, "HI PUMP TEMP!");
      break;
    case 8:
      strcpy(txtStatus, "REMOTE SHUTDN");
      break;
    default:
      sprintf(txtStatus, "UNKNOWN STAT: %d", poolstatus);
      break;
  }
}  //controllerStatus()


//=========================================================================================================
//  Flash LEDs to indicate if status of program
//=========================================================================================================
void flashLed(int pin, int times, int wait)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(pin, HIGH);
    delay(wait);
    digitalWrite(pin, LOW);
    
    if (i + 1 < times)
    { delay(50); }
  }
} // flashLed()



//=========================================================================================================
// Restarts program from beginning but does not reset the peripherals and registers
// Reference: http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1241733710
//=========================================================================================================
void software_Reset(void)
{
  asm volatile ("  jmp 0");
} // End software_Reset()


//=========================================================================================================
// Return the amount of free SRAM
// Parameters - true: Print out RAM, false: Don't print
// http://www.controllerprojects.com/2011/05/23/determining-sram-usage-on-arduino/
//=========================================================================================================
int freeRam(bool PrintRam)
{
  int freeSRAM;
  extern int __heap_start, *__brkval;
  int v;
  
  freeSRAM =  (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
  
  if(PrintRam)
  {
    Serial.print(F("RAM "));
    Serial.println(freeSRAM);
  }
  return freeSRAM;
  
} // freeRam()


