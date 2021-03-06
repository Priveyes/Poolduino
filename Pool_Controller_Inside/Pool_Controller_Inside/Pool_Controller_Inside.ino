/*
 Xively Feed: http://xively.com/feeds/65673/workbench
 
 Xbee Shield switch: to upload sketch, slide switch away from edge

SD Card: 
http://arduino.cc/en/Reference/SD
http://arduino.cc/en/Reference/SDCardNotes
http://arduino.cc/en/Tutorial/Datalogger - log data to file
http://arduino.cc/en/Tutorial/Files - create a new file
http://code.google.com/p/sdfatlib/
http://code.google.com/p/fat16lib/ - minimal implementation of the FAT16 file
http://www.ladyada.net/learn/arduino/ethfiles.html - tutorial using ethernet and SD Card together, doesn't seem applicable to me
http://learn.adafruit.com/adafruit-data-logger-shield/for-the-mega-and-leonardo



Format FAT 16
File name format: 8.3
Data is not saved until you use flush() or close()
Etherenet shield uses pin 4 for SD Card
SD.begin(4); // initialize SD Card

turn off ethernet when using SD Card:
digitalWrite(53, HIGH); 


The Arduino SPI pins are:
SPI  Uno  Mega
SS    10   53
MOSI  11   51
MISO  12   50
SCK   13   52

 
*/

#define PRINT_DEBUG     // Comment out to turn off serial printing

#include <SPI.h>             // Communicate with SPI devices http://arduino.cc/en/Reference/SPI
#include <Ethernet.h>        // LIbrary for Arduino ethernet shield http://arduino.cc/en/Reference/Ethernet
#include <HttpClient.h>      // http://github.com/amcewen/HttpClient/blob/master/HttpClient.h
#include <Time.h>            // http://playground.arduino.cc/Code/time
#include <SPI.h>             // http://arduino.cc/en/Reference/SPI
#include <SD.h>              // Micro SD Card. http://arduino.cc/en/Reference/SD
#include <Xively.h>          // http://github.com/xively/xively_arduino
#include <Twitter.h>         // http://arduino.cc/playground/Code/TwitterLibrary, get token from token at http://arduino-tweet.appspot.com/
#include <XBee.h>            // http://code.google.com/p/xbee-arduino/     Modified per http://arduino.cc/forum/index.php/topic,111354.0.html
#include <Tokens.h>          // Tokens for Xively and twitter
#include "LocalLibrary.h"    // Include application, user and local libraries


// Array positions for pool data array PoolData[]
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
#define NUM_XIVELY_STREAMS  16

const byte TWEETMAXSIZE =                   60;   // Character array size for twitter message
const uint32_t XIVELY_UPDATE_INTERVAL =  15000;   // Xively upload interval (mS)
const uint32_t XIVELY_UPDATE_TIMEOUT = 1800000;   // 30 minute timeout - if there are no successful updates in 30 minutes, reboot
#define FEED_ID                  65673   // Xively Feed ID http://xively.com/feeds/65673/workbench
// #define FEED_ID 4663  // Test feed
uint32_t xively_uploadTimout_timer; // Timer to reboot if no successful uploads in 30 minutes
uint32_t xively_Upload_Timer;       // Timer for uploading to Xively

const int bufferSize = 30;
char bufferValue[bufferSize]; // enough space to store the string we're going to send

const byte statusBufLen = 26;  // character buffer length for controller status text

// format is XivelyDatastream(stream name text, lenght of text, variable type: DATASTREAM_FLOAT, DATASTREAM_INT)
// for text format is XivelyDatastream(stream name text, lenght of text, variable type: DATASTREAM_BUFFER, bufferValue, bufferSize )
XivelyDatastream datastreams[] =
{
  XivelyDatastream( "0", 1, DATASTREAM_FLOAT),  // Pressure before filter
  XivelyDatastream( "1", 1, DATASTREAM_FLOAT),  // Pressure after filter
  XivelyDatastream( "2", 1, DATASTREAM_FLOAT),  // Water Fill Pressure
  XivelyDatastream( "3", 1, DATASTREAM_FLOAT),  // Temp before heater
  XivelyDatastream( "4", 1, DATASTREAM_FLOAT),  // Temp after heater
  XivelyDatastream( "5", 1, DATASTREAM_INT),    // Heater On/Off Status
  XivelyDatastream( "6", 1, DATASTREAM_FLOAT),  // Pump housing temperature
  XivelyDatastream( "7", 1, DATASTREAM_FLOAT),  // Pump amps
  XivelyDatastream( "8", 1, DATASTREAM_INT),    // Low Pressure Count
  XivelyDatastream( "9", 1, DATASTREAM_INT),    // Minutes water fill valve was otpen today
  XivelyDatastream("10", 2, DATASTREAM_BUFFER, bufferValue, bufferSize), // Status of controller - text
  XivelyDatastream("11", 2, DATASTREAM_INT),    // Xively upload successes
  XivelyDatastream("12", 2, DATASTREAM_INT),    // Xively Network Failures
  XivelyDatastream("13", 2, DATASTREAM_INT),    // Status of controller - number
  XivelyDatastream("14", 2, DATASTREAM_INT),    // Water level sensor
  XivelyDatastream("15", 2, DATASTREAM_FLOAT)   // battery volts for water level sensor
};

// Wrap the datastreams into a feed
XivelyFeed feed(FEED_ID, datastreams, NUM_XIVELY_STREAMS);


// Ethernet Setup
EthernetClient client;
XivelyClient xivelyclient(client);
byte mac[] = { 0xCC, 0xAC, 0xBE, 0x21, 0x91, 0x43 };
uint8_t successes = 0;    // Xively upload success, will rollover at 255, but that's okay.  This makes is easy to see on Xively is things are running
uint8_t failures =  0;    // Xively upload failures


// NPT Time setup
IPAddress timeServer(132, 163, 4, 101); // time-a.timefreq.bldrdoc.gov
const int timeZone = -4;  // Eastern Standard Time (USA)
EthernetUDP Udp;
const int NTP_PACKET_SIZE = 48;     // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets


// Xbee Setup stuff
XBee xbee = XBee();
XBeeResponse response = XBeeResponse();
// create reusable response objects for responses we expect to handle
Rx16Response rx16 = Rx16Response();
uint8_t xbeeErrors;      // XBee Rx errors
bool gotNewData;         // Flag to indicate that sketch has received new data from xbee
uint32_t xbeeTimeout;    // Counts time between successful Xbee data, if it goes too long, it means we've lost our connection to Xbee
bool xBeeTimeoutFlag;    // Flag to indicate no date from Xbee, used to keep warning from going off every 5 minutes

const int chipSelect = 4;  // Micro SD Card

// Twitter setup
Twitter twitter(TWITTER_TOKEN);



// Array to hold pool data received from outside controller
float PoolData[NUM_POOL_DATA_PTS];  


// Declare function prototypes
void PrintPoolData();
void software_Reset();
bool SendDataToXively();
bool ReadXBeeData(uint16_t *Tx_Id);
bool logDataToSdCard();
void sendAlarmMessage();
int SendTweet(char msgTweet[], double fpoolTime);
int freeRam(bool PrintRam);
void controllerStatus(char * txtStatus, int poolstatus);
void sendNTPpacket(IPAddress &address);
time_t getNtpTime();



//============================================================================
//============================================================================
void setup(void)
{
  delay(1000);


  Serial.begin(9600);
  
  #ifdef PRINT_DEBUG
    Serial.println(F("\nSetup pool controller inside"));
  #endif
  
  // Initialize XBee
  xbee.begin(9600);


  //pinMode(53, OUTPUT);
 // digitalWrite(53, HIGH);

  // Initialize Ethernet
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);  // disable microSD card interface while ethernet is starting
  Ethernet.begin(mac);
  delay(1000);


  unsigned int localPort = 8888;    // local port to listen for UDP packets
  Serial.println(Ethernet.localIP());
  Udp.begin(localPort);

  // Setup NTP time
  setSyncProvider(getNtpTime);  // tells time library to call getNtpTime() when it wants to sync arduino with NTP time
  // Get time from UDP server
  if ( getNtpTime() == 0 )
  {
    // Couldn't get time from UDP Server, try one more tiem
    delay(500);
    getNtpTime();
  }
  //  setTime(hour(t),minute(t),second(t),month(t),day(t),year(t));
  char timebuf[16];
  sprintf(timebuf, "Time: %02d:%02d:%02d", hour(),minute(),second());
  Serial.println(timebuf);

  	
  // Initialize SD Card
  if ( SD.begin(chipSelect) )
  { Serial.println("card initialized."); }
  else
  { Serial.println("Card failed, or not present"); }



  // Initialize global variables
  xively_uploadTimout_timer = millis() + XIVELY_UPDATE_TIMEOUT;
  xively_Upload_Timer       = millis() + XIVELY_UPDATE_INTERVAL;
  
  
  xbeeTimeout       = millis() + 300000UL; // 5 minutes
  xBeeTimeoutFlag   = false;
  gotNewData        = false;  // Initialize, true when new we receive new data from xbee, is reset when it uploads to Xively
  
  #ifdef PRINT_DEBUG
    Serial.println(F("End setup"));
    freeRam(true);
  #endif
  
} // setup()


//=========================================================================================================
//============================================================================
void loop(void)
{
  uint16_t xbeeID;                // ID of transimitting xbee
  char msgTweet[TWEETMAXSIZE];    // Holds text for twitter message.  Should be big enough for message and timestamp
  
  // Read XBee data
  // Keep tying to read data until successful.  After 30 tries, give up and move on
  bool xbeeStat;
  byte xbeeFailCnt = 0;
  do
  {
    delay(250);
    xbeeStat = ReadXBeeData(&xbeeID);
    xbeeFailCnt++;
  } while (xbeeStat == false && xbeeFailCnt < 30 );
  
  // Send a Tweet for startup, do this after you read xbee data so you can append pooltime which avoids duplicate tweets
  static bool tweetstartup;
  if (tweetstartup == false && millis() > 20000UL)
  {
    strcpy(msgTweet, "Inside pool monitor has restarted.");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
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
  
  // Upload data to Xively
  if ((long)(millis() - xively_Upload_Timer) >= 0)
  {
    xively_Upload_Timer = millis() + XIVELY_UPDATE_INTERVAL;  // Reset timer
    SendDataToXively(); // Send Data to Xively
    
    // Log data to SD Card, if we are receiving data from outside controller
    if ( xBeeTimeoutFlag == false)
    { logDataToSdCard(); }
  }
  
  // Check inputs and send message (Tweet) if anything is wrong
  sendAlarmMessage();
  
  
  // Reboot if no successful updates in 30 minutes
  if((long) (millis() - xively_uploadTimout_timer) >= 0)
  {
    strcpy(msgTweet, "Reboot: can't upload to Xively.");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
    xively_uploadTimout_timer = millis() + XIVELY_UPDATE_TIMEOUT;
    delay(1000);
    software_Reset();
  }
  
  // If PRINT_DEBUG is on, then delay 1000 so you don't fill up the serial monitor too fast
#ifdef PRINT_DEBUG
  delay(1000);
#endif
  
} // loop()


// Check inputs and send out a tweet if anything is wrong
void sendAlarmMessage()
{

  // Twitter Flags (tf_) used so Twitter messages are only sent once
  static bool tf_highPresDrop;       // High pressure across filter
  static bool tf_highPressure;       // Pressure before filter
  static bool tf_lowPressure;        // Low pressure
  static bool tf_highAmps;           // High pump amps
  static bool tf_highPumpTemp;       // High pump temperature
  static bool tf_emergencyShutdown;  // Emergency shutdown flag
  static bool tf_pumpOnAtNight;      // Pump on at night
  static bool tf_waterFillOn;        // Water fill valve is on
  static bool tf_heaterIsOn;         // Heater is on
  static bool tf_Xbee_Comm;          // Xbee communication, send alert if no comm
  // flags for sensor status.
  static bool tf_preHeatTemperatureSensor;
  static bool tf_postHeatTemperatureSensor;
  static bool tf_pumpTemperatureSensor;
  static bool tf_preFilterPresureSensor;
  static bool tf_postFilterPressureSensor;
  static bool tf_waterFillPressureSensor;
  static bool tf_pumpAmpsSensor;
  static bool tf_waterLevelSensor;
  
  char msgTweet[TWEETMAXSIZE];    // Holds text for twitter message.  Should be big enough for message and timestamp

  // If Arduino recently restarted (last 10 seconds), set waterFillCntDnFlag to true so it
  // doesn't send a tweet if the water fill is on
  if(millis() < 10000UL)
  { tf_waterFillOn = true; }
  
  
  // High pump amps
  if(PoolData[P_PUMP_AMPS] >= 17 && tf_highAmps == false)
  {
    tf_highAmps = true;
    sprintf(msgTweet, "High pump amps: %d.", (int) PoolData[P_PUMP_AMPS]);
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // High pump temperature
  if(PoolData[P_TEMP_PUMP] >= 175 && tf_highPumpTemp == false)
  {
    tf_highPumpTemp = true;
    sprintf(msgTweet, "High pump temp: %d.", (int) PoolData[P_TEMP_PUMP]);
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // High pressure
  if(PoolData[P_PRESSURE1] >= 40 && tf_highPressure == false)
  {
    tf_highPressure = true;
    sprintf(msgTweet, "High pump pressure: %d.", (int) PoolData[P_PRESSURE1]);
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // Check pressure drop across filter
  if((PoolData[P_PRESSURE1] > 25.0) && ((PoolData[P_PRESSURE1] - PoolData[P_PRESSURE2]) > 10.0) && (tf_highPresDrop == false))
  {
    tf_highPresDrop = true;
    strcpy(msgTweet, "High filter pressure.");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // Check low pressure Counter
  if(PoolData[P_LOW_PRES_CNT] >= 17 && tf_lowPressure == false)
  {
    tf_lowPressure = true;
    strcpy(msgTweet, "Low pressure fluctuations at pool pump.");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // Check for emergency shutdown and send tweet
  if(PoolData[P_CONTROLLER_STATUS] >= 4 && tf_emergencyShutdown == false)
  {
    char txtStatus[statusBufLen];
    tf_emergencyShutdown = true;
    strcpy(msgTweet, "Emergency Shutdown - ");
    controllerStatus(txtStatus,  PoolData[P_CONTROLLER_STATUS]);
    strcat(msgTweet, txtStatus);
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
    #ifdef PRINT_DEBUG
      Serial.println(msgTweet);
    #endif
  }
  
  // Send Tweet if water fill timer is opened
  if(PoolData[P_WATER_FILL_COUNTDN] > 1 && tf_waterFillOn == false)
  {
    // Wait a couple seconds in case water fill button is pressed a couple times, then read XBee data again
    delay(2500);
    uint16_t xbeeID;                // ID of transimitting xbee
    ReadXBeeData(&xbeeID);
    sprintf(msgTweet, "Water fill started for %d minutes.", (int) PoolData[P_WATER_FILL_COUNTDN]);
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
    tf_waterFillOn = true;  // flag so Tweet is only sent once
  }
  
  // Send Tweet of pump is running at night
  if(PoolData[P_PUMP_AMPS] > 6 && PoolData[P_POOL_TIME] >= 21 && tf_pumpOnAtNight == false)
  {
    tf_pumpOnAtNight = true;
    strcpy(msgTweet, "Dude, turn off the pool pump");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  // Send tweet when heater comes on.  Don't want this every time, just once a day, so reset at night
  // Determine if Pool heater is on by comparing temperatures
  if(((PoolData[P_TEMP2] - PoolData[P_TEMP1]) > 5.0) && (PoolData[P_PUMP_AMPS] > 5.0) && tf_heaterIsOn == false)
  {
    tf_heaterIsOn = true;
    strcpy(msgTweet, "Pool heater is on");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }

  // Send alert if xbee communication is lost
  if(xBeeTimeoutFlag && tf_Xbee_Comm == false)
  {
    tf_Xbee_Comm = true;
    strcpy(msgTweet, "Lost xBee communication");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }


  // Send tweet if any sensors are having trouble
  if ((sensorStatusbyte >> 0) & 1 == 0 && tf_preHeatTemperatureSensor == false )
  {
    tf_preHeatTemperatureSensor = true;
    strcpy(msgTweet, "Pool pre-heat temperature sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }

  if ((sensorStatusbyte >> 1) & 1 == 0 && tf_postHeatTemperatureSensor == false )
  {
    tf_postHeatTemperatureSensor = true;
    strcpy(msgTweet, "Pool post-heat temperature sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 2) & 1 == 0 && tf_pumpTemperatureSensor == false )
  {
    tf_pumpTemperatureSensor = true;
    strcpy(msgTweet, "Pool pump temperature sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 3) & 1 == 0 && tf_preFilterPresureSensor == false )
  {
    tf_preFilterPresureSensor = true;
    strcpy(msgTweet, "Pool pre-filter pressure sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 4) & 1 == 0 && tf_postFilterPressureSensor == false )
  {
    tf_postFilterPressureSensor = true;
    strcpy(msgTweet, "Pool post-filter pressure sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 5) & 1 == 0 && tf_waterFillPressureSensor == false )
  {
    tf_waterFillPressureSensor = true;
    strcpy(msgTweet, "Pool water fill pressure sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 6) & 1 == 0 && tf_pumpAmpsSensor == false )
  {
    tf_pumpAmpsSensor = true;
    strcpy(msgTweet, "Pool pump amps sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  if ((sensorStatusbyte >> 7) & 1 == 0 && tf_waterLevelSensor == false )
  {
    tf_waterLevelSensor = true;
    strcpy(msgTweet, "Pool water level sensor trouble");
    SendTweet(msgTweet, PoolData[P_POOL_TIME]);
  }
  
  
  // Reset one-shot message flags
  if(PoolData[P_PUMP_AMPS] < 10 && tf_highAmps == true)  // Reset high amps flag
  { tf_highAmps = false; }
  
  if(PoolData[P_TEMP_PUMP] < 110 && tf_highPumpTemp == true)  // Reset high pump temp flag
  { tf_highPumpTemp = false; }
  
  if(PoolData[P_PRESSURE1] < 110 && tf_highPressure == true)  // Reset high pressure flag
  { tf_highPressure = false; }
  
  if(PoolData[P_PRESSURE1] < 2 && tf_highPresDrop == true)  // Reset high pressure drop Flag
  { tf_highPresDrop = false; }
  
  if(PoolData[P_LOW_PRES_CNT] == 0 && tf_lowPressure == true) // Reset low pressure counter flag
  { tf_lowPressure = false; }
  
  if(PoolData[P_CONTROLLER_STATUS] < 4 && tf_emergencyShutdown == true)  // Reset emergency shutdown flag
  { tf_emergencyShutdown = false; }
  
  if(PoolData[P_WATER_FILL_COUNTDN] == 0 && tf_waterFillOn == true)  // Reset water fill timer flag
  { tf_waterFillOn = false; }

  if(PoolData[P_POOL_TIME] < 21 && tf_pumpOnAtNight == true)  // Reset pump running at night flag
  { tf_pumpOnAtNight = false; }

  if(PoolData[P_POOL_TIME] < 21 && tf_heaterIsOn == true)  // Reset heater on flag
  { tf_heaterIsOn = false; }

  
} // sendAlarmMessage


//=========================================================================================================
// Send twitter text, appends the time to the message to avoid twitter blocking duplicate messages
//=========================================================================================================
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
  
  xively_Upload_Timer = millis() + XIVELY_UPDATE_INTERVAL; // increase timer for Xively so it doesn't send right after Twitter
  
  
} // SendTweet()


//=========================================================================================================
// Send data to Xively
// xivelyData array is the same as the poolData array in loop function
// Before sending, check to make sure data is realistic.  If not, don't send
//======================================================================================================================================
bool SendDataToXively()
{
  
  
  if (gotNewData == true)
  {
    if(PoolData[P_PRESSURE1] < 40)  // check for a valid pressure
    { datastreams[0].setFloat(PoolData[P_PRESSURE1]); }  // 0 - Pre-filter pressure
    
    if(PoolData[P_PRESSURE2] < 40)  // check for a valid pressure
    { datastreams[1].setFloat(PoolData[P_PRESSURE2]); }  // 1- Post-Filter pressure
    
    if(PoolData[P_PRESSURE3] < 100)  // check for a valid pressure
    { datastreams[2].setFloat(PoolData[P_PRESSURE3]); }  // 2- water fill pressure
    
    if(PoolData[P_TEMP1] > 40 && PoolData[P_TEMP1] < 150)
    { datastreams[3].setFloat(PoolData[P_TEMP1]); }    // 3 - Pre heater temp
    
    if(PoolData[P_TEMP2] > 40 && PoolData[P_TEMP2] < 150)
    { datastreams[4].setFloat(PoolData[P_TEMP2]); }    // 4 - Post heater temp
    
    // Determine if Pool heater is on by comparing temperatures
    if(((PoolData[P_TEMP2] - PoolData[P_TEMP1]) > 5.0) && (PoolData[P_PUMP_AMPS] > 5.0))
    { datastreams[5].setInt(1); }              // 5 - Heater status On/Off
    else
    { datastreams[5].setInt(0); }
    
    if(PoolData[P_TEMP_PUMP] > 40 && PoolData[P_TEMP_PUMP] < 300)
    { datastreams[6].setFloat(PoolData[P_TEMP_PUMP]); } // 6 - Pump temp
    
    if(PoolData[P_PUMP_AMPS] < 50)
    { datastreams[7].setFloat(PoolData[P_PUMP_AMPS]); } // 7 - Pump amps
    
    datastreams[8].setInt((int) PoolData[P_LOW_PRES_CNT]); // 8 - Low pressure counter
    
    datastreams[9].setInt((int) PoolData[P_WATER_FILL_MINUTES]); // 9 - Mintues water fill valve was on today
    
    datastreams[13].setInt((int) PoolData[P_CONTROLLER_STATUS]); // 13 - Controller status code number
    
    datastreams[14].setInt(PoolData[P_LOW_WATER] ); // 14 - water level sensor - Calculated
    
    datastreams[15].setFloat(PoolData[P_WATER_LVL_BATT]/1000.0); // 15 - battery volts for water level sensor
    
  } // if gotNewData
  
  gotNewData = false; // reset got xbee data flag
  
  if(xBeeTimeoutFlag == true)
  { // No communication with XBEE
    Serial.println(F("No Xbee Comm"));
    datastreams[10].setBuffer("NO XBEE COMM");  // 10 - Controller status - sends text to Xively, not numbers
  }
  else
  {
    char txtStatus[statusBufLen];  // text buffer for controller status
    controllerStatus(txtStatus,  PoolData[P_CONTROLLER_STATUS]);  // Convert controller status number to text
    datastreams[10].setBuffer(txtStatus);                           // 10 - Controller status - sends text to Xively, not numbers
  }
  
  datastreams[11].setInt(successes); // 11 - network successes
  
  datastreams[12].setInt(failures);  // 12 - network failures
  
  
  // Send data to Xively
  #ifdef PRINT_DEBUG
    Serial.println(F("\n\nSend to Xively"));
  #endif
  
  int ret = xivelyclient.put(feed, XIVELY_API_KEY);
  switch (ret)
  {
    case 200:
      xively_uploadTimout_timer = millis() + XIVELY_UPDATE_TIMEOUT; // Reset upload timout timer
      successes++;
      failures = 0;
      #ifdef PRINT_DEBUG
        Serial.println(F("Successful Xively upload"));
      #endif
      // Flash twice when we send data to Xively successfully

      return true;
      break;
    case HTTP_ERROR_CONNECTION_FAILED:
      failures++;
      #ifdef PRINT_DEBUG
        Serial.println(F("\nconnection to api.xively.com has failed. Failures = "));
        Serial.println(failures);
      #endif
      return false;
      break;
    case HTTP_ERROR_API:
      failures++;
      #ifdef PRINT_DEBUG
        Serial.println(F("\nA method of HttpClient class was called incorrectly. Failures = "));
        Serial.println(failures);
      #endif
      return false;
      break;
    case HTTP_ERROR_TIMED_OUT:
      failures++;
      #ifdef PRINT_DEBUG
        Serial.println(F("\nConnection with api.xively.com has timed-out. Failures = "));
        Serial.println(failures);
      #endif
      return false;
      break;
    case HTTP_ERROR_INVALID_RESPONSE:
      failures++;
      #ifdef PRINT_DEBUG
        Serial.println(F("\nInvalid or unexpected response from the server. Failures = "));
        Serial.println(failures);
      #endif
      return false;
      break;
    default:
      failures++;
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
bool ReadXBeeData(uint16_t *Tx_Id)
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
        PoolData[i/2] = (float) RxData[i/2] / 10.0; // value from XBee are 10x, convert back to normal size
      }
      
      // Put status bytes into byte variables
      sensorStatusbyte = PoolData[P_SENSORSTATUSBYTE];    // Each bit determines if sensor is operating properly
      ioStatusbyte = PoolData[P_IOSTATUSBYTE];            // I/O state of digital I/O
      
      gotNewData = true;
      xbeeTimeout = millis() + 300000UL; // Reset xbee timeout timer, 5 minutes
      xBeeTimeoutFlag = false;
     
      #ifdef PRINT_DEBUG
        PrintPoolData();
      #endif
      return true;
    }
    else // Didn't get a RX_16_RESPONSE
    {
      // Got a Response, but not RX_16_RESPONSE
      #ifdef PRINT_DEBUG
        Serial.println(F("Got XBee Response, but not RX_16_RESPONSE"));
      #endif
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
    return false;
  } // End Got Something
  else
  {
    // xbee not available and no error
    #ifdef PRINT_DEBUG
      //srg      Serial.print(F("XBee not avail, Err code: "));
      //      Serial.println(xbee.getResponse().getErrorCode());
    #endif
    return false;
  }
  
} // ReadXbeeData()



//=========================================================================================================
//=========================================================================================================
bool logDataToSdCard()
{
  File myFile;

  // Build log file name: YYYYMMDD.log
  char logfile[13];
  sprintf(logfile, "%d%02d%02d.log", year(), month(), day());

  // Check to see if the file exists:
  if (!SD.exists(logfile))
  {
    // log file doesn't exist, create it
    myFile = SD.open(logfile, FILE_WRITE);
    myFile.close();
  }


  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open(logfile, FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile)
  {
    char timebuf[24];
    sprintf(timebuf, " %02d/%02d/%d %02d:%02d:%02d", month(),day(),year(),hour(),minute(),second());
    dataFile.println(timebuf);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_POOL_TIME],2);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_TEMP1],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_TEMP2],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_TEMP_PUMP],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_PUMP_AMPS],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_PRESSURE1],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_PRESSURE2],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_PRESSURE3],1);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_LOW_PRES_CNT],0);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_WATER_FILL_MINUTES],0);
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_WATER_LVL_BATT]/1000.0,2);  // battery volts
    dataFile.print(F("\t"));
    dataFile.print(PoolData[P_CONTROLLER_STATUS],0);
    dataFile.print(F("\t"));
    dataFile.print(sensorStatusbyte, BIN);
    dataFile.print(F("\t"));
    dataFile.print(ioStatusbyte, BIN);
    dataFile.println();

    dataFile.close();

    // print to the serial port too:
    Serial.print(F("Saved data to SD Card - "));
    Serial.println(logfile);
    return true;
  }
  // if the file isn't open, pop up an error:
  else
  {
    Serial.print("error opening ");
    Serial.println(logfile);
    return false;
  } 
  
}  // logDataToSdCard

//=========================================================================================================
// Print data sent from Pool Xbee
//=========================================================================================================
void PrintPoolData()
{
  static byte printheadcounter;
  
  // Print heading every 15 rows
  if (printheadcounter == 0)
  {
    Serial.println(F("\nTemp1\tTemp2\ttemp3\tAmps\tPres1\tPres2\tPres3\tP-Cnt\tWFmin\tbatt\tstat\tsesnsor\tI/O"));
    printheadcounter = 15;
  }
  
  printheadcounter--;
  
  Serial.print(PoolData[P_TEMP1],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_TEMP2],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_TEMP_PUMP],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_PUMP_AMPS],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_PRESSURE1],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_PRESSURE2],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_PRESSURE3],1);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_LOW_PRES_CNT],0);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_WATER_FILL_MINUTES],0);
  Serial.print(F("\t"));
  Serial.print(PoolData[P_WATER_LVL_BATT]/1000.0,2);  // battery volts
  Serial.print(F("\t"));
  Serial.print(PoolData[P_CONTROLLER_STATUS],0);
  Serial.print(F("\t"));
  Serial.print(sensorStatusbyte, BIN);
  Serial.print(F("\t"));
  Serial.print(ioStatusbyte, BIN);
  Serial.println();
  
} // PrintPoolData()


//=========================================================================================================
// Return the text for the pool controller status
// Longest text: 20 characters
//=========================================================================================================
void controllerStatus(char * txtStatus, int poolstatus)
{
  // txtStatus can hold 25 characters
  
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


// -------- NTP code ----------
time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println(F("Transmit NTP Request"));
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}  // getNtpTime()

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}  // sendNTPpacket()


