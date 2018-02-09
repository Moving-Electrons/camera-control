
/*
This sketch does the following:

- Creates a Wifi network and serves an HTML with several options: Timer, Timelapse and Lightning Mode.
- Once an option is selected, updates the "Status" section of the HTML page being served and 
runs the appropriate function. 
- The program controls a camera (Sony) using the IR LED with a predefined IR code. In order to change/re-learn
a different code, the sketch "IRrecvDumpV2.ino" should be downloaded and run on the Feather Huzzah (all the 
hardware is already installed). The new code form that sketch should be assigned to de "rawDataSonyCtrl"
variable.

*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>

#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>

Adafruit_FeatherOLED oled = Adafruit_FeatherOLED();

/* ID assigned to the TSL2561 sensor below is "12345". */
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

ESP8266WebServer server(80);

IRsend irsend(13);  // IR LED controlled by GPIO pin 13

/* Sony remote control code for shutter activation. */
// SONY B4B8F                 
uint16_t rawDataSonyCtrl[125] = {2388, 568,  1218, 568,  644, 572,  1222, 564,  1220, 
                        566,  646, 570,  1224, 562,  650, 576,  648, 570,  1214, 
                        572,  652, 566,  1218, 568,  1216, 570,  1214, 572,  650, 
                        566,  648, 580,  644, 572,  1222, 566,  1218, 568,  1218, 
                        568,  1214, 11804,  2382, 572,  1222, 564,  648, 568,  
                        1216, 570,  1224, 562,  652, 566,  1218, 568,  646, 580,  
                        652, 564,  1220, 566,  648, 568,  1216, 570,  1224, 562,  
                        1222, 564,  648, 568,  646, 580,  652, 564,  1220, 566,  
                        1218, 568,  1218, 568,  1224, 11792,  2386, 568,  1214, 572,  
                        652, 564,  1220, 566,  1218, 568,  644, 572,  1222, 564,  
                        650, 576,  648, 570,  1214, 572,  652, 564,  1220, 566,  
                        1218, 570,  1214, 570,  652, 564,  648, 578,  644, 572,  
                        1222, 564,  1220, 566,  1218, 568,  1216};                        

/* Set these to the desired WiFi credentials. */
const char *ssid = "Camera_Control";
const char *password = "turf-casa-pico-photo-9";

/* Battery voltage measured at the analog input. */
#define VBATPIN A0

/* Constants and variables for calculating the "rolling average"
on the analog read pin for battery monitoring: 

Although the analog pin reading can be used, it results on jittery
voltage readings on the LED display.*/

/* Number of readings to calculate average (needs to be "const"
since it defines array size). */
const int numReadings = 10;

int readings[numReadings];
int readIndex = 0;
int totalReadings = 0;
float averageReadings = 0.0;

/* Lux threshold: */
float luxLimit = 100.00;


void setup() {
  
	delay(100);
	Serial.begin(115200);

  Serial.println("Initiating IR...");
  irsend.begin();

  Serial.println("Initiating OLED...");
  oled.init();
  oled.setBatteryVisible(true);
  oled.setBatteryIcon(true);
 
	Serial.println("Configuring access point...");
	
	/* Remove the password parameter for AP to be open.
	Defatult IP: http://192.168.4.1  */
	WiFi.softAP(ssid, password);

	IPAddress myIP = WiFi.softAPIP();
	Serial.print("AP IP address: ");
	Serial.println(myIP);

	server.on("/", handleRoot);
	server.begin();
	Serial.println("HTTP server started");

  /* Initializing the array holding battery voltage values: */
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }

  /* Initializing Lux sensor */
  //use tsl.begin() to default to Wire, 
  //tsl.begin(&Wire2) directs api to use Wire2, etc.
  
  if(!tsl.begin())
  {
    /* There was a problem detecting the TSL2561 ... check your connections */
    Serial.print("Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!");
    while(1);
  }
  
  /* Setup the sensor gain and integration time */
  configureLuxSensor();
  

}

void loop() 
{

  /* Calls "handleRoot()" if there is an http request. */
	server.handleClient();

  /* clear the whole screen */
  oled.clearDisplay();

  /* Showing device IP on OLED screen */
  oled.setCursor(0,16);
  oled.print("IP: ");
  oled.println(WiFi.softAPIP());
  oled.print("Waiting...");

  /* Updates battery level and connected clients info
  on OLED display: */
  updateOledTopLine();

  oled.display(); // update display

  delay(100);
  
}

void handleRoot() 
{

  /* Message to be shown on initial state:*/
  String msg = "Initiated";
  int shoot_delay, shot_interval, number_of_shots, clip_duration, event_duration = 0;
  bool selection = false;
  bool lightMode = false;


  /* Arguments sent by including /?arg=value&arg2=value in the URL */
  Serial.print("number of arguments: ");
  Serial.println(server.args());
  Serial.println(server.arg("shot_interval"));
  Serial.println(server.arg("number_of_shots"));
  Serial.println(server.arg("shoot_delay"));
  Serial.println(server.arg("fps"));

  /* Code below tests if arguments were sent with the http request. */
  
  /* If a shot interval was entered, the option "By Interval Length" was chosen. */
  if (server.arg("shot_interval") != "") {
    
    Serial.println("Option selected: By Interval Length");

    shot_interval = server.arg("shot_interval").toInt();
    number_of_shots = server.arg("number_of_shots").toInt();
    shoot_delay = server.arg("shoot_delay").toInt()*60; //converting seconds
    selection = true;
    
    
    // Event duration in hours
    event_duration = (shot_interval * number_of_shots) / 3600;
    // Clip duration in seconds.
    clip_duration = (shot_interval * number_of_shots)/server.arg("fps").toInt(); 

    msg = "Option selected: By Interval Length <br>Event duration (hours): ";
    msg += event_duration;
    msg += ", final clip (seconds): ";
    msg += clip_duration;

  }
  
  /* If a final clip length was entered, the option "By Interval Length" was chosen. */
  else if (server.arg("clip_length") != "") {

    Serial.println("Option selected: By Clip Length");

    number_of_shots = server.arg("clip_length").toInt()*server.arg("fps").toInt();
    shot_interval = float((server.arg("event_duration").toInt() * 60) / number_of_shots);
    
    shoot_delay = server.arg("shoot_delay").toInt()*60; //converting to seconds
    selection = true;

    msg = "Option selected: By Clip Length <br>Number of shots: ";
    msg += number_of_shots;
    msg += ", shot interval (seconds): ";
    msg += shot_interval;
    
  }

  /* If a timer delay was entered, then the Timer option was chosen. */
  else if (server.arg("timer_delay") != "") {

    Serial.println("Option selected: Timer");

    shot_interval = server.arg("timer_interval").toInt();
    number_of_shots = server.arg("timer_shots").toInt();
    shoot_delay = server.arg("timer_delay").toInt();
    selection = true;

    msg = "Option selected: Timer<br>Number of shots: ";
    msg += number_of_shots;
    msg += ", delay (seconds): ";
    msg += shoot_delay;
    
  }

  
  /* If number of shots was entered in the Lightning Mode, then that sectoin was selected. */
  else if (server.arg("light_shots") != "") {

    Serial.println("Option selected: Lightning Mode");

    shot_interval = server.arg("light_interval").toInt();
    number_of_shots = server.arg("light_shots").toInt();
    selection = true;
    lightMode = true;

    msg = "Option selected: Lightning Mode<br>Number of shots: ";
    msg += number_of_shots;
    msg += ", interval (seconds): ";
    msg += shot_interval;
    
  }

  

  String html ="<html> <style> * { font-family: sans-serif; -webkit-appearance: none; -moz-appearance: none; appearance: none; \
  } body { background-color: #000000; } div { border-radius: 5px; background-color: #f2f2f2; padding: 20px; margin: 20px; } \
  h1 { color: white; } input[type=text], select { width: 80px; padding: 12px 20px; margin: 8px 0; display: inline-block; \
  border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; font-size: 16px; } input[type=submit] { width: 150px; \
  font-size: 16px; background-color: #2196F3; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; \
  cursor: pointer; } input[type=submit]:hover { background-color: #45a049; } </style> <body> <h1>Status</h1> <div> <b>";

  html += msg;

  html += "</b> </div> <h1>Timer</h1> <div> <form action=\"/\"> <label for=\"tdelay\">Delay (seconds):</label> <input \
  type=\"text\" id=\"tdelay\" name=\"timer_delay\"><br> <label for=\"tshots\">Number of shots:</label> <input type=\"text\" \
  id=\"tshots\" name=\"timer_shots\" value=2><br> <label for=\"sinterval\">Timer interval (seconds):</label> <input \
  type=\"text\" id=\"tinterval\" name=\"timer_interval\" value=2><br> <input type=\"submit\" value=\"Start\"> </form> </div> \
  <h1>Timelapse</h1> <div> <h2>Option 1: By Interval Length</h2> <form action=\"/\"> <label for=\"stime\">Shooting interval \
  (seconds):</label> <input type=\"text\" id=\"stime\" name=\"shot_interval\"> <label for=\"nshots\">Number of shots:</label> \
  <input type=\"text\" id=\"nshots\" name=\"number_of_shots\"><br> <label for=\"fps\">Frames per second:</label> \
  <input type=\"text\" id=\"fps\" name=\"fps\" value=25><br> <label for=\"delay\">Time delay (minutes):</label> <input \
  type=\"text\" id=\"delay\" name=\"shoot_delay\" value=0><br> <input type=\"submit\" value=\"Start\"> </form> </div> <div> \
  <h2>Option 2: By Clip Length</h2> <form action=\"/\"> <label for=\"clength\">Final clip length (seconds):</label> <input \
  type=\"text\" id=\"clength\" name=\"clip_length\"> <label for=\"edur\">Event duration (minutes):</label> \
  <input type=\"text\" id=\"edur\" name=\"event_duration\"><br> <label for=\"fps\">Frames per second:</label> \
  <input type=\"text\" id=\"fps\" name=\"fps\" value=25><br> <label for=\"delay\">Time delay (minutes):</label> \
  <input type=\"text\" id=\"delay\" name=\"shoot_delay\" value=0><br> <input type=\"submit\" value=\"Start\"> </form> \
  </div> <h1>Lightning Mode</h1> <div> <form action=\"/\"> <label for=\"lshots\">Number of shots:</label> <input \
  type=\"text\" id=\"lshots\" name=\"light_shots\"><br> <label for=\"linterval\">Timer interval (seconds):</label> \
  <input type=\"text\" id=\"linterval\" name=\"light_interval\" value=2><br> <input type=\"submit\" value=\"Start\"> \
  </form> </div> </html>";
  
  server.send(200, "text/html", html);

  /* Code for processing selection:
  Only calls the camera function if a selection was made. */
  if (selection) 
  {
  
   if (lightMode) 
   {
     lightDetectionMode(shot_interval, number_of_shots);
   }
  
   else 
   {   
     handleCamera(shoot_delay, shot_interval, number_of_shots);
   }
   
  }

}


void handleCamera (int shoot_delay, int shot_interval, int number_of_shots) 
{
     
  int percent = 0;
  
  /* Clears area below oled top line. */
  oled.fillRect(0, 8, 128, 32, BLACK);
  
  /* Sets cursor in 3rd line (2nd line left blank) */
  oled.setCursor(0,16); 
  oled.print("Shots:");
  oled.print(number_of_shots);
  oled.print(" Int(s):");
  oled.println(shot_interval);
  oled.print("Processing delay...");
  oled.display();

  delay(shoot_delay*1000);

  for (int i=1; i<=number_of_shots; i++){
    
    /* Sending code to IR LED */
    Serial.println("Click!");
    irsend.sendRaw(rawDataSonyCtrl, 125, 38);  // Send a raw data capture at 38kHz.
    

    /* updates battery icon and connection count */
    updateOledTopLine();
     
    percent = (i*100)/number_of_shots;

    Serial.print(percent);
    Serial.println(" % completed.");
    
    /* Updates last line with progress */
    oled.fillRect(0, 24, 128, 32, BLACK);
    oled.setCursor(0,24);
    oled.print("Progress: ");
    oled.print(percent);
    oled.print("%");
    oled.display();
    
    delay(shot_interval*1000);
  }
}


float getBatteryVoltage() {

    
    /* --- Starts algorithm for calculating rolling average --- */
    
    /* Substracts the previous reading from the total: */
    totalReadings = totalReadings - readings[readIndex];
    
    readings[readIndex] = analogRead(VBATPIN);

    /* Calculating actual Vbat based on the voltage divider (resistors of 1Mohm and 220Kohm).
    Using the voltage divider formula (VR2 = (VBatxR2)/(R1+R2) would result on lower values
    (perhaps because of the slight difference in actual resistors values). So the "map" function
    was used. minimum reading value 539mV corresponds to an actual battery voltage of 3.2V and a
    maximum reading value of 720mV corresponds to an actual battery voltage of 4.27V. */
    readings[readIndex] = map(readings[readIndex], 539, 720, 3200, 4270);
    

    /* Adds new value to the total:*/
    totalReadings = totalReadings + readings[readIndex];

    readIndex += 1;
    if (readIndex >= numReadings) {
      readIndex = 0;
    }

    averageReadings = totalReadings / numReadings;

    /* Converting from read mili Volts to Volts. */
    averageReadings /= 1000;
    
    return averageReadings;
}

void updateOledTopLine() {

  /* get the current voltage of the battery from
  one of the platform specific functions below */
  float battery = getBatteryVoltage();
  
  /* Getting number of clients connected to server */
  int clientNum = WiFi.softAPgetStationNum();
  
  
  /* Clears top line */
  oled.fillRect(0, 0, 128, 8, BLACK);
  /* update the battery icon */
  oled.setBattery(battery);
  oled.renderBattery();
  oled.setCursor(0,0);
  oled.print("Conn. ");
  oled.print(clientNum);
  
}


void configureLuxSensor(void)
{
  /* Gain can also be manually set or enable auto-gain support */
  // tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  // tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  
  /* Changing the integration time gives better sensor resolution (402ms = 16-bit data) */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* 101 medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what it's set above! */  
  Serial.println("Lux sensor configuration:");
  Serial.print  ("Gain:         "); Serial.println("16X");
  Serial.print  ("Timing:       "); Serial.println("13 ms");
}

void lightDetectionMode(int shot_interval, int number_of_shots) {

  /* Clears area below oled top line. */
  oled.fillRect(0, 8, 128, 32, BLACK);
  
  /* Sets cursor in 3rd line (2nd line left blank) */
  oled.setCursor(0,16); 
  oled.print("Shots:");
  oled.print(number_of_shots);
  oled.print(" Int(s):");
  oled.print(shot_interval);
  oled.display();

  while (1)
  {

    /* Clears last line of oled screen. */
    oled.fillRect(0, 24, 128, 32, BLACK);
    oled.setCursor(0,24); 
    oled.print("Waiting for flash...");
    oled.display();
        

    /* Get a new lux sensor event */ 
    sensors_event_t event;
    tsl.getEvent(&event);
    
    /* Display the results (light is measured in lux) */
    if (event.light) 
    {
      
      Serial.print(event.light); Serial.println(" lux");
        
      if (event.light >= luxLimit) 
      {
        
        /* Clears last line of oled screen. */
        oled.fillRect(0, 24, 128, 32, BLACK);
        oled.setCursor(0,24); 
        oled.print("Flash!, taking shots..");      
        Serial.print("Light above threshold. Taking shot..");
        handleCamera(0, shot_interval, number_of_shots);
      }
      
    }
    else
    {
      /* If event.light = 0 lux the sensor is probably saturated
         and no reliable data could be generated! */
      Serial.println("Sensor overload");
    }
    
    delay(150);
    /* updates battery icon and connection count */
    updateOledTopLine();

  }
    
}


