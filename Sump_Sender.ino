////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//    Version 1.0        Adding  HTTP over TLS (HTTPS)     09/05/2020 @ 13:48 EDT
//
//                       ESP8266 --Internet Sump Pit Monitor, Datalogger and Dynamic Web Server   05/16/2020 @ 12:45 EDT
//
//                       NTP time routines optimized for speed by schufti  --of ESP8266.com
//
//                       Project uses ESP8266 Developement board: NodeMCU
//
//                       readFiles and readFile functions by martinayotte of ESP8266 Community Forum
//
//                       Previous projects:  by tech500" on https://github.com/tech500
//
//                       Project is Open-Source; requires ESP8266 based developement board.
//
//
//
//                      Note:  Uses esp8266 by ESP8266 Community version 2.7.4 for "Arduino IDE."
//
///////////////////////////////////////////////////////////////////////////////////////////////////////

/*
	Program uses ultrasonic sensing to measure distance of water from top of sump pit.

	Features:

	1.  Ultrasonic distance to water measuring,
	2.  Log file of measurements at 15 minute intervals.
	3.  Web interface display last update of water distance to top of sump pit.
	4.  Graph of distance to top and time of data point.
	5.  FTP for file maintence; should it be needed.
	6.  Automatic deletion of log files.  Can be daily of weekly
	7.  OTA Over-the-air firmware updates.
	8.  Sends email and SMS alert at predetermined height of water to top of sump pit.
*/

// ********************************************************************************
// ********************************************************************************
//
//   See invidual library downloads for each library license.
//
//   Following code was developed with the Adafruit CC300 library, "HTTPServer" example.
//   and by merging library examples, adding logic for Sketch flow.
//
// *********************************************************************************
// *********************************************************************************
#include "Arduino.h"
#include <EMailSender.h>   //https://github.com/xreef/EMailSender
#include <ESP8266WiFi.h>   //Part of ESP8266 Board Manager install __> Used by WiFi to connect to network
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>  //https://github.com/me-no-dev/ESPAsyncWebServer
#include <WiFiClientSecure.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266FtpServer.h>  //https://github.com/nailbuster/esp8266FTPServer  -->Needed for ftp transfers
#include <ESP8266HTTPClient.h>   //Part of ESP8266 Board Manager install --> Used for Domain web interface
#include <WiFiUdp.h>
#include <sys/time.h>  // struct timeval --> Needed to sync time
#include <time.h>   // time() ctime() --> Needed to sync time 
#include <Ticker.h>  //Part of version 1.0.3 ESP32 Board Manager install  -----> Used for watchdog ISR
#include <Wire.h>   //Part of the Arduino IDE software download --> Used for I2C Protocol
//#include <LiquidCrystal_I2C.h>   //https://github.com/esp8266/Basic/tree/master/libraries/LiquidCrystal --> Used for LCD Display
#include "variableInput.h"  //Packaged with project download.  Provides editing options; without having to search 2000+ lines of code.

uint8_t connection_state = 0;
uint16_t reconnect_interval = 10000;

EMailSender emailSend("**********@gmail.com", "**********");  //gmail email address and gmail application password

//How to create application password  https://www.lifewire.com/get-a-password-to-access-gmail-by-pop-imap-2-1171882

uint8_t WiFiConnect(const char* nSSID = nullptr, const char* nPassword = nullptr)
{
    static uint16_t attempt = 0;
    Serial.print("Connecting to ");
    if(nSSID) {
        WiFi.begin(nSSID, nPassword);
        Serial.println(nSSID);
    }

    uint8_t i = 0;
    while(WiFi.status()!= WL_CONNECTED && i++ < 50)
    {
        delay(200);
        Serial.print(".");
    }
    ++attempt;
    Serial.println("");
    if(i == 51) {
        Serial.print("Connection: TIMEOUT on attempt: ");
        Serial.println(attempt);
        if(attempt % 2 == 0)
            Serial.println("Check if access point available or SSID and Password\r\n");
        return false;
    }
    Serial.println("Connection: ESTABLISHED");
    Serial.print("Got IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Port:  ");
    Serial.print(LISTEN_PORT);
    Serial.println("\n");
    return true;
}

WiFiClientSecure client;

void Awaits()
{
    uint32_t ts = millis();
    while(!connection_state)
    {
        delay(50);
        if(millis() > (ts + reconnect_interval) && !connection_state){
            connection_state = WiFiConnect();
            ts = millis();
        }
    }
}


#define SPIFFS LittleFS

#import "index1.h"  //Weather HTML; do not remove

#import "index2.h"  //SdBrowse HTML; do not remove

IPAddress ipREMOTE;

Ticker secondTick;

volatile int watchdogCounter;
int totalwatchdogCounter;

void ISRwatchdog()
{

  watchdogCounter++;

}


WiFiUDP Udp;
unsigned int localUdpPort = 123;  // local port to listen for UDP packets
char incomingPacket[255];
char replyPacket[] = "Hi there! Got the message :-)";

#define NTP1 "us.pool.ntp.org"
#define NTP0 "time.nist.gov"

#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"

int DOW, MONTH, DATE, YEAR, HOUR, MINUTE, SECOND;

char strftime_buf[64];

String dtStamp(strftime_buf);
String lastUpdate;

int lc = 0;
time_t tnow = 0;

int flag = 0;

float temp(NAN), hum(NAN), pres(NAN), millibars, fahrenheit, RHx, T, heat_index, dew, dew_point, atm;

float HeatIndex, DewPoint, temperature, humidity, TempUnit_Fahrenheit;

int count = 0;

int error = 0;

unsigned long delayTime;

int started;   //Used to tell if Server has started

//use I2Cscanner to find LCD display address, in this case 3F   //https://github.com/todbot/arduino-i2c-scanner/
//LiquidCrystal_I2C lcd(0x3F,16,2);  // set the LCD address to 0x3F for a 16 chars and 2 line display

#define BUFSIZE 64  //Size of read buffer for file download  -optimized for CC3000.

//long int id = 1;  //Increments record number

char *filename;
char str[] = {0};

String fileRead;

String MyBuffer[13];

String PATH;

////////////////////////// AsyncWebServer ////////////
AsyncWebServer serverAsync(80);  //PORT
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncEventSource events("/events"); // event source (Server-Sent events)
//////////////////////////////////////////////////////

#define LISTEN_PORT 80

FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial


const int ledPin =  14;   //ESP8266, RobotDyn WiFi D1

int trigPin = 14;   //Orange wire
int echoPin = 12;   //White wire

int pingTravelTime;
float pingTravelDistance;
float distanceToTarget;
int dt = 50;

int reconnect;

int requested = 0;   //Limits SMS text to one text.  Requires manaul reset of Sump Pit Monitor.

void setup(void)
{

Serial.begin(115200);

  /*
  connection_state = WiFiConnect(ssid, password);
  if(!connection_state)  // if not connected to WIFI
  Awaits();          // constantly trying to connect

  EMailSender::EMailMessage message;
  message.subject = "Warning!!!";
  message.message = "  Sump Pump ///////////////Alert high water level warning!";
  
  EMailSender::Response resp = emailSend.send("********@vtext.com", message);  //******* 10 digit phone number
  emailSend.send("*********@gmail.com", message);  //*************** gmail email address
  
  Serial.println("Sending status: ");
  
  Serial.println(resp.status);
  Serial.println(resp.code);
  Serial.println(resp.desc);
*/

while (!Serial) {}

Serial.println("");
Serial.println("");
Serial.print("Starting...");
Serial.println("SumpPit.ino");
Serial.print("\n");

wifiStart();

secondTick.attach(1, ISRwatchdog);  //watchdog  ISR triggers every 1 second

pinMode(online, OUTPUT); //Set pinMode to OUTPUT for online LED

pinMode(4, INPUT_PULLUP); //Set input (SDA) pull-up resistor on GPIO_0 // Change this *! if you don't use a Wemos

pinMode(trigPin, OUTPUT);

pinMode(echoPin, INPUT);

Wire.setClock(2000000);
Wire.begin(SDA, SCL); //Wire.begin(0, 2); //Wire.begin(sda,scl) // Change this *! if you don't use a Wemos

configTime(0, 0, NTP0, NTP1);
setenv("TZ", "EST+5EDT,M3.2.0/2,M11.1.0/2", 3);   // this sets TZ to Indianapolis, Indiana
tzset();

/*    from HTTPS "time"
Serial.println("");
struct tm timeinfo;
gmtime_r(&now, &timeinfo);
Serial.print("Current time: ");
Serial.print(asctime(&timeinfo));
*/

delay(500);

started = 1;   //Server started

delay(500);

LittleFS.begin();

Serial.println("LittleFS opened!");
Serial.println("");
ftpSrv.begin("######", "########");   //username, password for ftp.  

serverAsync.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest * request)
{
PATH = "/FAVICON";
//accessLog();
if (! flag == 1)
{
 request->send(SPIFFS, "/favicon.png", "image/png");

}
//end();
});

serverAsync.on("/", HTTP_GET, [](AsyncWebServerRequest * request)
{

PATH = "/";
accessLog();

ipREMOTE = request->client()->remoteIP();

if (! flag == 1)
{
 request->send_P(200, PSTR("text/html"), HTML1, processor1);
}
end();
});

serverAsync.on("/sump", HTTP_GET, [](AsyncWebServerRequest * request)
{

PATH = "/sump";
accessLog();

ipREMOTE = request->client()->remoteIP();

if (! flag == 1)
{
 request->send_P(200, PSTR("text/html"), HTML1, processor1);
}
end();
});

serverAsync.on("/SdBrowse", HTTP_GET, [](AsyncWebServerRequest * request)
{
PATH = "/SdBrowse";
accessLog();
if (! flag == 1)
{
 request->send_P(200, PSTR("text/html"), HTML2, processor2);

}
end();
});

serverAsync.onNotFound(notFound);

serverAsync.begin();

Serial.println("*** Ready ***");

}



// How big our line buffer should be. 100 is plenty!
#define BUFFER 100


void loop()
{

  int flag;

  delay(1);

  int packetSize = Udp.parsePacket();

  if (packetSize)
  {
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    int len = Udp.read(incomingPacket, 255);
    if (len > 0)
    {
      incomingPacket[len] = 0;
    }
    Serial.printf("UDP packet contents: %s\n", incomingPacket);

  }

  // send back a reply, to the IP address and port we got the packet from
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  Udp.write(replyPacket);
  Udp.endPacket();

  if (WiFi.status() != WL_CONNECTED)
  {

    getDateTime();

    //Open a "WIFI.TXT" for appended writing.   Client access ip address logged.
    File logFile = SPIFFS.open("/WIFI.TXT", "a");

    if (!logFile)
    {
      Serial.println("File: '/WIFI.TXT' failed to open");
    }
    else
    {

      logFile.print("WiFi Disconnected:  ");

      logFile.println(dtStamp);

      logFile.close();

      wifiStart();

      WiFi.waitForConnectResult();

      reconnect = 1;

    }

  }

  if ((WiFi.status() == WL_CONNECTED)  && reconnect == 1)
  {

    watchdogCounter = 0;  //Resets the watchdogCount

    //Open a "WIFI.TXT" for appended writing.   Client access ip address logged.
    File logFile = SPIFFS.open("/WIFI.TXT", "a");

    if (!logFile)
    {
      Serial.println("File: '/WIFI.TXT' failed to open");
    }
    else
    {

      getDateTime();

      logFile.print("WiFi Reconnected:   ");

      logFile.println(dtStamp);

      logFile.close();

      reconnect = 0;

    }

  }





  if (started == 1)
  {

    getDateTime();

    // Open "/SERVER.TXT" for appended writing
    File log = LittleFS.open("/SERVER.TXT", "a");



    if (!log)
    {
      Serial.println("file:  '/SERVER.TXT' open failed");
    }

    log.print("Started Server:  ");
    log.println(dtStamp) + "  ";
    log.close();

  }

  started = 0;   //only time started = 1 is when Server starts in setup

  if (watchdogCounter > 45)
  {

    totalwatchdogCounter++;

    Serial.println("watchdog Triggered");

    Serial.print("Watchdog Event has occurred. Total number: ");
    Serial.println(watchdogCounter / 80);

    // Open a "log.txt" for appended writing
    File log = LittleFS.open("/WATCHDOG.TXT", "a");

    if (!log)
    {
      Serial.println("file 'WATCHDOG.TXT' open failed");
    }

    getDateTime();

    log.print("Watchdog Restart:  ");
    log.print("  ");
    log.print(dtStamp);
    log.println("");
    log.close();

    Serial.println("Watchdog Restart  " + dtStamp);

    WiFi.disconnect();

    ESP.restart();

  }

  watchdogCounter = 0;  //Resets the watchdogCount

  ////////////////////////////////////////////////////// FTP ///////////////////////////////////
  for (int x = 1; x < 5000; x++)
  {
    ftpSrv.handleFTP();

  }
  ///////////////////////////////////////////////////////////////////////////////////////////////

  getDateTime();


  //Executes every 15 seconds routine
  if (SECOND % 30 == 0)
  {

    flag = 1;

    Serial.println("");
    Serial.println("Thirty second routine");
    Serial.println(dtStamp);
    lastUpdate = dtStamp;   //store dtstamp for use on dynamic web page
    ultra();
    logtoSD();   //Output to LittleFS  --Log to LittleFS on 15 minute interval.

    delay(1000);
  }


  flag = 0;

  //Collect  "LOG.TXT" Data for one day; do it early (before 00:00:00) so day of week still equals 6.
  if ((HOUR == 23 )  &&
      (MINUTE == 57) &&
      (SECOND == 0))
  {

    newDay();
  }



}

String processor1(const String& var)
{

  //index1.h

  char dist[10]; // Buffer big enough for 9-character float
  dtostrf(distanceToTarget, 9, 1, dist); // Leave room for too large numbers!
  
  if (var == F("TOP"))
    return dist;

  if (var == F("DATE"))
    return dtStamp;

  if (var == F("CLIENTIP"))
    return ipREMOTE.toString().c_str();

  return String();

}

String processor2(const String& var)
{

	//index2.h

	String str;

	if (!LittleFS.begin())
	{
		Serial.println("LittleFS failed to mount !");
	}
	Dir dir = LittleFS.openDir("/");
	while (dir.next())
	{

		String file_name = dir.fileName();

		if(file_name.startsWith("LOG"))
		{

			str += "<a href=\"";
			str += dir.fileName();
			str += "\">";
			str += dir.fileName();
			str += "</a>";
			str += "    ";
			str += dir.fileSize();
			str += "<br>\r\n";
		}
	}

	client.print(str);

  if (var == F("URLLINK"))
    return  str;

  if (var == F("LINK"))
    return linkAddress;

  if (var == F("FILENAME"))
    return  dir.fileName();

  return String();

}

void accessLog()
{

  digitalWrite(online, HIGH);  //turn on online LED indicator

  getDateTime();

  String ip1String = "10.0.0.146";   //Host ip address
  String ip2String = ipREMOTE.toString().c_str();   //client remote IP address

  Serial.println("");
  Serial.println("Client connected:  " + dtStamp);
  Serial.print("Client IP:  ");
  Serial.println(ip2String);
  Serial.print("Path:  ");
  Serial.println(PATH);
  Serial.println(F("Processing request"));

  //Open a "access.txt" for appended writing.   Client access ip address logged.
  File logFile = SPIFFS.open(Restricted, "a");

  if (!logFile)
  {
    Serial.println("File 'ACCESS.TXT'failed to open");
  }

  if ((ip1String == ip2String) || (ip2String == "0.0.0.0") || (ip2String == "(IP unset)"))
  {

    //Serial.println("HOST IP Address match");
    logFile.close();

  }
  else
  {

    Serial.println("Log client ip address");

    logFile.print("Accessed:  ");
    logFile.print(dtStamp);
    logFile.print(" -- Client IP:  ");
    logFile.print(ip2String);
    logFile.print(" -- ");
    logFile.print("Path:  ");
    logFile.print(PATH);
    logFile.println("");
    logFile.close();

  }

}

void beep(unsigned char delayms)
{

  // wait for a delayms ms
  digitalWrite(sonalert, HIGH);
  delay(3000);
  digitalWrite(sonalert, LOW);

}

void end()
{

  //accessLog();

  // Wait a short period to make sure the response had time to send before
  // the connection is closed .

  delay(1000);

  //Client flush buffers
  client.flush();
  // Close the connection when done.
  client.stop();

  digitalWrite(online, LOW);   //turn-off online LED indicator

  getDateTime();

  Serial.println("Client closed:  " + dtStamp);

  delay(1);   //Delay for changing too quickly to new browser tab.




}

void fileStore()   //If 6th day of week, rename "log.txt" to ("log" + month + day + ".txt") and create new, empty "log.txt"
{

  // Open file for appended writing
  File log = LittleFS.open("/LOG.TXT", "a");

  if (!log)
  {
    Serial.println("file open failed");
  }

  // rename the file "LOG.TXT"
  String logname;
  logname = "/LOG";
  logname += MONTH; ////logname += Clock.getMonth(Century);
  logname += DATE;   ///logname += Clock.getDate();
  logname += ".TXT";
  LittleFS.rename("/LOG.TXT", logname.c_str());
  log.close();

  //For troubleshooting
  //Serial.println(logname.c_str());

}

String getDateTime()
{
  struct tm *ti;

  tnow = time(nullptr) + 1;
  //strftime(strftime_buf, sizeof(strftime_buf), "%c", localtime(&tnow));
  ti = localtime(&tnow);
  DOW = ti->tm_wday;
  YEAR = ti->tm_year + 1900;
  MONTH = ti->tm_mon + 1;
  DATE = ti->tm_mday;
  HOUR  = ti->tm_hour;
  MINUTE  = ti->tm_min;
  SECOND = ti->tm_sec;

  strftime(strftime_buf, sizeof(strftime_buf), "%a , %m/%d/%Y , %H:%M:%S %Z", localtime(&tnow));
  dtStamp = strftime_buf;
  return (dtStamp);

}

void links()
{

  client.println("<br>");
  client.println("<h2><a href='http://' + publicIP + ':' + PORT + '/SdBrowse >File Browser</a></h2><br>'");
  //Show IP Adress on client screen
  client.print("Client IP: ");
  client.print(client.remoteIP().toString().c_str());
  client.println("</body>");
  client.println("</html>");
}

void logtoSD()   //Output to LittleFS every fifthteen minutes
{


  getDateTime();

  int tempy;
  String Date;
  String Month;

  tempy = (DATE);
  if (tempy < 10)
  {
    Date = ("0" + (String)tempy);
  }
  else
  {
    Date = (String)tempy;
  }

  tempy = (MONTH);
  if (tempy < 10)
  {
    Month = ("0" + (String)tempy);
  }
  else
  {
    Month = (String)tempy;
  }

  String logname;
  logname = "/LOG";
  logname += Month;   ///logname += Clock.getDate();
  logname += Date; ////logname += Clock.getMonth(Century);
  logname += ".TXT";

  // Open a "log.txt" for appended writing
  //File log = LittleFS.open(logname.c_str(), FILE_APPEND);
  File log = LittleFS.open(logname.c_str(), "a");

  if (!log)
  {
    Serial.println("file 'LOG.TXT' open failed");
  }

  delay(500);

  //log.print(id);
  //log.print(" , ");


  log.print(distanceToTarget);
  log.print(" inches ");
  log.print(" , ");
  log.print(dtStamp);
  log.println();

  //Increment Record ID number
  //id++;

  Serial.println("");
  Serial.print(distanceToTarget, 1);
  Serial.println(" inches; Data written to  " + logname + "  " + dtStamp);

  log.close();

}

void newDay()   //Collect Data for twenty-four hours; then start a new day
{

  //Do file maintence on 1st day of week at appointed time from RTC.  Assign new name to "log.txt."  Create new "log.txt."
  if ((DOW) == 6)
  {
    fileStore();
  }

  //id = 1;   //Reset id for start of new day
  //Write log Header

  // Open file for appended writing
  File log = LittleFS.open("/LOG.TXT", "a");

  if (!log)
  {
    Serial.println("file open failed");
  }

  connection_state = WiFiConnect(ssid, password);
  if(!connection_state)  // if not connected to WIFI
  Awaits();          // constantly trying to connect

  EMailSender::EMailMessage message;
  message.subject = "Allowed --less secure operstion";
  message.message = "Daily --email to ensure less secure operational status";
  //If rhis gmail account is not used; will revert to more secure operation.

  EMailSender::Response resp = emailSend.send("*********@gmail.com", message);
  //emailSend.send("*********@vtext.com", message);
	
	//List of email to SMS carriers  https://avtech.com/articles/138/list-of-email-to-sms-addresses/
  
  Serial.println("Sending status: ");

  Serial.println(resp.status);
  Serial.println(resp.code);
  Serial.println(resp.desc);

  requested = 0;

}

///////////////////////////////////////////////////////////////
//readFile  --AsyncWebServer version with much help from Pavel
////////////////////////////////////////////////////////////////
void notFound(AsyncWebServerRequest *request) {

  if (request->url().endsWith(F(".TXT")))  {  //.endsWith(F(".txt"))) {
    // here comes some mambo-jambo to extract the filename from request->url()
    int fnsstart = request->url().lastIndexOf('/');
    String fn = request->url().substring(fnsstart);
    // ... and finally
    Serial.println();
    Serial.println("Accessed for Download");
    accessLog();
    request->send(SPIFFS, fn, String(), false);
    end();
  } else {
    request->send_P(404, PSTR("text/plain"), PSTR("Not found"));
  }
}

void sendRequestURL()  //Triggers cellphone SMS alert and email alert.
{

  if(requested == 1)
  {
        
    	connection_state = WiFiConnect(ssid, password);
    	if(!connection_state)  // if not connected to WIFI
    	Awaits();          // constantly trying to connect
    
    	EMailSender::EMailMessage message;
    	message.subject = "Warning High Water!!!";
      message.message = "Sump Pump /////////////// Alert high water level! //////////////////";
    
    	EMailSender::Response resp = emailSend.send("*********@vtext.com", message);  //verizon text domain
    	emailSend.send("********@gmail.com", message);
    
    	Serial.println("Sending status: ");
    
    	Serial.println(resp.status);
    	Serial.println(resp.code);
    	Serial.println(resp.desc);
    
      requested = 0;

  }
    
}

void ultra()    //Get distance in inches from the floor (top of Sump Pit.)
{
  
  delay(10);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(10);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  pingTravelTime = pulseIn(echoPin, HIGH);
  delay(25);
  pingTravelDistance = (pingTravelTime * 765.*5280.*12) / (3600.*1000000);
  distanceToTarget = (pingTravelDistance / 2);
    
  Serial.print("Distance to Target is: ");
  Serial.print(distanceToTarget,1);
  Serial.println(" in.");

  digitalWrite(echoPin, HIGH);

  delay(50);

  
    
  if(distanceToTarget < 3.0)  //Sets distance to top of Sump Pit in whole inches.
  {

      requested = 1;
        
      sendRequestURL();
      
      //Serial.println("Request sent");
      
      delay(200);

  } 
  
}  

void wifiStart()
{

  WiFi.mode(WIFI_STA);

  //wifi_set_sleep_type(NONE_SLEEP_T);

  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);


  //setting the addresses
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;


  WiFi.begin(ssid, password);

  WiFi.config(ip, gateway, subnet, dns);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }

  WiFi.waitForConnectResult();

  Serial.printf("Connection result: %d\n", WiFi.waitForConnectResult());

}
