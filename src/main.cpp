#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <HardwareSerial.h>
#include <PZEM004T.h>
#include <Pushbutton.h>
#include <ArduinoLog.h>
#include "FreeRTOS.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Replace with your network credentials
const char* ssid     = "ParkingEVSE";
const char* password = "120288mu";
// Set web server port number to 80
// Set your Static IP address
IPAddress local_IP(192, 168, 0, 1);
WiFiServer server(80);
// Variable to store the HTTP request
String header;
//////////////////////////////////////////////////////////////////////

// LCD settings ////////////////////////////////////////////////////
#define RS_PIN 25
#define E_PIN 26
#define DB4_PIN 27
#define DB5_PIN 14
#define DB6_PIN 12
#define DB7_PIN 13

LiquidCrystal lcd(RS_PIN, E_PIN, DB4_PIN, DB5_PIN, DB6_PIN, DB7_PIN);
#define MAX_SCREEN_N 2
unsigned char screen_n = 0;
//////////////////////////////////////////////////////////////////////

// Define relay ////////////////////////////////////////
#define RELAY_PIN 33
//////////////////////////////////////////////////////////////////////

// Define button
#define BTN_PIN 32
Pushbutton button1(BTN_PIN);
//////////////////////////////////////////////////////////////////////

// Led settings //////////////////////////////////////////////////////
#define LED_PIN 23
//////////////////////////////////////////////////////////////////////


// Declare functions prototypes //////////////////////////////////////
void taskPZEM( void * parameter );
void taskDisplay( void * parameter );
void taskBtn1Read( void * parameter );
void taskCurWh( void * parameter );
void taskWeb( void * parameter );
void taskOTA( void * parameter );
void reset_pzem(void);
//////////////////////////////////////////////////////////////////////


// Declare global variables /////////////////////////////////////////
enum states {
    NotCharging,
    Charging,
};
enum states State = NotCharging;
// V, A, Wh
float Voltage=0;
float Current=0;
float Wh=0;
float cur_Wh=0;
float old_Wh=0;
//////////////////////////////////////////////////////////////////////

void setup() {
    // Debug serial setup ///////////////////////////////////////////////
	Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin   (LOG_LEVEL_VERBOSE, &Serial);
    Log.notice("###### Start logger ######"CR);
    //////////////////////////////////////////////////////////////////////


    // Setup relay //////////////////////////////////////////////////////
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, false);
    //////////////////////////////////////////////////////////////////////

    // Setup LED /////////////////////////////////////////////////////////
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, false);
    //////////////////////////////////////////////////////////////////////

    // Setup LCD
    lcd.begin(16, 2);
    lcd.print("Starting");
    //////////////////////////////////////////////////////////////////////

    // Setup Wifi AP /////////////////////////////////////////////////////
    Log.notice("Start AP"CR);
    WiFi.softAPConfig(local_IP, local_IP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    lcd.setCursor(0, 0);
    server.begin();
    //////////////////////////////////////////////////////////////////////

    // Setup OTA upgrade
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    // ArduinoOTA.setHostname("myesp32");

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Log.notice("Start updating %s", type);
      })
      .onEnd([]() {
        Log.notice("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Log.notice("Progress: %d%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Log.notice("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Log.error("Auth Failed"CR);
        else if (error == OTA_BEGIN_ERROR) Log.error("Begin Failed"CR);
        else if (error == OTA_CONNECT_ERROR) Log.error("Connect Failed"CR);
        else if (error == OTA_RECEIVE_ERROR) Log.error("Receive Failed"CR);
        else if (error == OTA_END_ERROR) Log.error("End Failed"CR);
      });

    ArduinoOTA.begin();

    xTaskCreate(taskPZEM,
                "TaskPZEMRead",
                10000,
                NULL,
                1,
                NULL);

    xTaskCreate(taskDisplay,
                "TaskDisplay",
                10000,
                NULL,
                1,
                NULL);

    xTaskCreate(taskBtn1Read,     // Task function.
                "TaskBtn1Read",  // String with name of task.
                1000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskCurWh,     // Task function.
                "TaskCurrentWh",  // String with name of task.
                1000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskWeb,     // Task function.
                "TaskWeb",  // String with name of task.
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.

    xTaskCreate(taskOTA,     // Task function.
                "TaskOTA",  // String with name of task.
                10000,           // Stack size in bytes.
                NULL,            // Parameter passed as input of the task
                1,               // Priority of the task.
                NULL);           // Task handle.
}

void loop() {
}

void taskPZEM( void * parameter ){
    const int t_delay = 1000;
    float val=0;

    HardwareSerial PzemSerial2(2);     // Use hwserial UART2 at pins IO-16 (RX2) and IO-17 (TX2)
    PZEM004T pzem(&PzemSerial2);
    IPAddress ip(192,168,1,1);

    Log.notice("pzem_init: Connecting to PZEM..."CR);
    while (true) {
        if(pzem.setAddress(ip)){
            Log.notice("pzem_init: Ok"CR);
            break;
        }
        vTaskDelay(t_delay);
    }

    while(true){
        val = pzem.voltage(ip);
        if(val < 0.0) Voltage = 0.0;
        else Voltage = val;
        Log.verbose("pzem_read: Voltage %F"CR, Voltage);

        val = pzem.current(ip);
        if(val < 0.0) Current = 0.0;
        else Current = val;
        Log.verbose("pzem_read: Current %F"CR, Current);

        val = pzem.energy(ip);
        if (val < 0.0) Wh = 0.0;
        else Wh = (val/1000);
        Log.verbose("pzem_read: Wh %F"CR, Wh);
        vTaskDelay(500);
    }
}

void taskDisplay( void * parameter ){
    while(true){
        lcd.clear();
        if (screen_n == 0){
            lcd.setCursor(0, 0);              // Устанавливаем курсор в начало 1 строки
            lcd.print("V ");
            lcd.print(String(Voltage));
            lcd.setCursor(40, 0);              // Устанавливаем курсор в начало 1 строки
            lcd.print(" A ");
            lcd.print(Current);
        }
        if (screen_n == 1){
            lcd.setCursor(0, 0);              // Устанавливаем курсор в начало 1 строки
            lcd.print(String(cur_Wh));
            lcd.setCursor(40, 0);              // Устанавливаем курсор в начало 1 строки
            lcd.print(String(Wh));
        }
        if (screen_n == MAX_SCREEN_N){
            lcd.setCursor(0, 0);              // Устанавливаем курсор в начало 1 строки
            lcd.print("Cleaning...");
        }
        vTaskDelay(100);

    }
}

void taskBtn1Read( void * parameter ){
    bool btn_state = false;
    bool btn_state_old = btn_state;
    unsigned int startmills = 0;
    unsigned int endmills = 0;
    while(1){
        btn_state = button1.isPressed();
        if (btn_state != btn_state_old){
            btn_state_old = btn_state;
            if (btn_state == true){
                startmills = millis();
                Log.notice("Btn pressed!"CR);
            }
            else{
                endmills = millis();
                if (endmills - startmills < 2000){
                    if (screen_n < MAX_SCREEN_N-1) screen_n++;
                    else screen_n = 0;
                }
                else{
                    reset_pzem();
                }
            }
        }
        vTaskDelay(100);
    }
}

void taskCurWh( void * parameter ){
    enum states old_state = State;

    while (true) {
        if (Current > 0){
            State = Charging;
        }
        else State = NotCharging;

        if (State != old_state){
            old_state = State;
            if (State != Charging){
                digitalWrite(LED_PIN, false);
            }
            else{
                digitalWrite(LED_PIN, true);
                old_Wh = Wh;
            }
        }
        cur_Wh = Wh - old_Wh;
        vTaskDelay(100);
    }
}

void taskWeb( void * paramter ){

    while(true){
    // put your main code here, to run repeatedly:
    WiFiClient client = server.available();   // Listen for incoming clients

    if (client) {                             // If a new client connects,
        Log.notice("New Client.");          // print a message out in the serial port
        String currentLine = "";                // make a String to hold incoming data from the client
        while (client.connected()) {            // loop while the client's connected
          if (client.available()) {             // if there's bytes to read from the client,
            char c = client.read();             // read a byte, then
            header += c;
            if (c == '\n') {                    // if the byte is a newline character
              // if the current line is blank, you got two newline characters in a row.
              // that's the end of the client HTTP request, so send a response:
              if (currentLine.length() == 0) {
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/html");
                client.println("Connection: close");
                client.println();

                // turns the GPIOs on and off
                if (header.indexOf("GET /reset") >= 0) {
                    Log.notice("Reset cmd"CR);
                    reset_pzem();
                    client.print("<HEAD>");
                    client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
                    client.print("</head>");
                }

                // Display the HTML web page
                client.println("<!DOCTYPE html><html>");
                client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                client.println("<link rel=\"icon\" href=\"data:,\">");
                // CSS to style the on/off buttons
                // Feel free to change the background-color and font-size attributes to fit your preferences
                client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
                client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                client.println(".button2 {background-color: #555555;}</style></head>");

                // Web Page Heading
                client.println("<body><h1>Parking EVSE</h1>");

                // Display current state, and ON/OFF buttons for GPIO 26
                client.println("<p>Voltage " + String(Voltage) + "</p>");
                client.println("<p>Current " + String(Current) + "</p>");
                client.println("<p>Current Wh " + String(cur_Wh) + "</p>");
                client.println("<p>Total Wh " + String(Wh) + "</p>");
                // If the output26State is off, it displays the ON button
                client.println("<p><a href=\"/reset\"><button class=\"button\">Reset</button></a></p>");

                client.println("</body></html>");

                // The HTTP response ends with another blank line
                client.println();
                // Break out of the while loop
                break;
              } else { // if you got a newline, then clear currentLine
                currentLine = "";
              }
            } else if (c != '\r') {  // if you got anything else but a carriage return character,
              currentLine += c;      // add it to the end of the currentLine
            }
          }
      }
      // Clear the header variable
      header = "";
      // Close the connection
      client.stop();
    }
    }

}

void taskOTA( void * parameter ){
    while(true){
        ArduinoOTA.handle();
        vTaskDelay(100);
    }
}

void reset_pzem(void){
    screen_n = MAX_SCREEN_N;
    digitalWrite(RELAY_PIN, true);
    vTaskDelay(7000);
    digitalWrite(RELAY_PIN, false);
    vTaskDelay(2000);
    digitalWrite(RELAY_PIN, true);
    vTaskDelay(3000);
    digitalWrite(RELAY_PIN, false);
    screen_n = 0;

}
