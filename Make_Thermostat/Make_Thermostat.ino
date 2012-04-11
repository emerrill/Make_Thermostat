
//************************************
//
//    
//
//************************************
// include the library code:
#include <LiquidCrystal.h>
#include <Wire.h> 
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>

//#include <MemoryFree.h>

//Time and DS1307RTC are both avalible here: http://www.arduino.cc/playground/uploads/Code/Time.zip
#include <Time.h>
#include <DS1307RTC.h>






//Options
//Setting DEBUG to 0 should gain enough code size to run DHCP on a Duemilanove
#define DEBUG 0
#define SERIAL_BAUD 57600

#define USE_RGB_SCREEN 1
#define SCREEN_SLEEP 0

//If you have a arduino older than an Uno, or you otherwise get code size complilation errors,
//disabling DHCP by setting USE_DHCP to 0 will significantly decrease code size. 
//Setting DEBUG to 0 but USE_DHCP to 1 *should* work on a Duemilanove
#define USE_DHCP 1
#define CALIBRATION_TEMP -8.0
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192,168,1, 85);

#define USE_REMOTE_SERVER 1

#define REMOTE_SERVER "example.com"
#define REMOTE_URI "/make_remote.php"

//Match to $arduinokey in make_remote.php
#define REMOTE_PASSWORD "akey"

//End Options













//Pins
#define SHT11_CLK 17
#define SHT11_DAT 16

#define BUTTON0_PIN 2
#define BUTTON1_PIN 3

#define LCD_RS 4
#define LCD_ENABLE 5
#define LCD_D4 6
#define LCD_D5 7
#define LCD_D6 8
#define LCD_D7 9








//Variables
long buttonUp = 0;
int buttonUpRep = 0;
long buttonDown = 0;
int buttonDownRep = 0;
int showIP = 0;
#define BUTTON_DEBOUNCE_TIME 150
#define BUTTON_REPEAT_TIME 500

byte ioState = 0xA0;
byte currentIoState = 0xA0;

#define TIME_SLOTS 4
byte times[TIME_SLOTS] = {6, 9, 17, 22};
byte temps[TIME_SLOTS] = {68, 60, 68, 64};

byte targetTemp = 68;
byte bandWidth = 5; //In tenths of degrees
byte HVACMode = 1; //0 = off/1 = heat/2 = cool


time_t tempTime = 0;
byte tempHold = 0; //0 = no temp, 1 = temp time, 2 = temp hold
#define TEMP_TIME 14400


float temperature = 68;
float humidity = 50;
byte lastTempHumCheckSeconds = 99;
long tempCheckPending = 0;
long humCheckPending = 0;


//Network Vars
EthernetServer server(80);
EthernetClient remoteConnection;
EthernetClient incomingClient;

byte remoteSendTemp = 0;

#define STRING_BUF_SIZE 255
//char buffer[STRING_BUF_SIZE];
String bufferString = String();

#define VAL_BUF_SIZE 32
char valBuffer[VAL_BUF_SIZE];

int bufferIndex = 0;

byte remoteRequestPending = 0;
byte remoteSendProgramChange = 0;

byte lastRemoteCheckMinutes = 99;

byte getData = 0;



String pageName = String("m");


//Constants
#define EXPANDER_ADDY 0x20
#define RTC_ADDY 0x68
#define REGISTER_COMMAND B00000110
#define REGISTER_VAL     B00000001
//#define LCD_R_PIN B01000000
//#define LCD_G_PIN B00100000
//#define LCD_B_PIN B10000000
#define LCD_R_PIN B10000000
#define LCD_G_PIN B01000000
#define LCD_B_PIN B00100000
#define HVAC_COOL_PIN B00000001
#define HVAC_HEAT_PIN B00000010

//Setups

//Make a degree character
byte degree[8] = {
  B01100,
  B10010,
  B10010,
  B01100,
  B00000,
  B00000,
  B00000,
};

//Setup the LCD
LiquidCrystal lcd(LCD_RS, LCD_ENABLE, LCD_D4, LCD_D5, LCD_D6, LCD_D7);




void setup() {
  byte val;
  
  if (DEBUG) {
    Serial.begin(SERIAL_BAUD);
  }
  setSyncProvider(RTC.get);
  
  

  val = EEPROM.read(0);
  if (val > 23) {
    saveTimesEEPROM();
  } else {
    loadTimesEEPROM();
  }
  
  //SHT
  resetSHT();
  sendCommandSHT(REGISTER_COMMAND); 
  sendByteSHT(REGISTER_VAL);
  
  //Setup pins
  pinMode(BUTTON0_PIN, INPUT);
  pinMode(BUTTON1_PIN, INPUT);
  digitalWrite(BUTTON0_PIN, HIGH);
  digitalWrite(BUTTON1_PIN, HIGH);
  
  //Setup Screen
  lcd.createChar(0, degree);
  // set up the LCD's number of rows and columns: 
  lcd.begin(16, 2);

  //Setup I2C Expander
  Wire.begin();
  Wire.beginTransmission(EXPANDER_ADDY);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  changeLCDColor('G');
  updateExpander();

  
  //Setup/get EEPROM vars

  
  

  //Setup Ethernet
  if (USE_DHCP) {
    Ethernet.begin(mac);
  } else {
    Ethernet.begin(mac, ip);
  }
  
  //Local Server Setup
  server.begin();

  //Serial.println(freeMemory());
  
  updateTemperature();
}


void loadTimesEEPROM() {
  byte i, addy = 0;
  
  for (i = 0; i < TIME_SLOTS; i++) {
    times[i] = EEPROM.read(addy);
    addy++;
  }
  for (i = 0; i < TIME_SLOTS; i++) {
    temps[i] = EEPROM.read(addy);
    addy++;
  }
}

void saveTimesEEPROM() {
  byte i, addy = 0;
  
  for (i = 0; i < TIME_SLOTS; i++) {
    EEPROM.write(addy, times[i]);
    addy++;
  }
  for (i = 0; i < TIME_SLOTS; i++) {
    EEPROM.write(addy, temps[i]);
    addy++;
  }
}

 
void loop() {

  
  incomingClient = server.available();;
  
  if (incomingClient) {
    processIncomingRequest();
  }
  
  
  checkButtons();
  

  
  
  //Some stuff we really don't need to do evey cycle
  if ((millis() % 50) == 0) {
    //Only check every 10 seconds
    if (((second() % 10) == 0) && (second() != lastTempHumCheckSeconds) && (!tempCheckPending) && (!humCheckPending)) {
      lastTempHumCheckSeconds = second();
      updateTemperature();
    }
    
    //Only check every minute
    if (USE_REMOTE_SERVER && ((second() % 60) == 0) && (minute() != lastRemoteCheckMinutes) && (!remoteRequestPending)) {
      lastRemoteCheckMinutes = minute();
      makeRemoteRequest();
    }
    
    //Check if we have an incoming response
    if (remoteRequestPending && remoteConnection.available()) {
      processRemoteResponse();
    }
    
    
    //If we are waiting for a temp update
    if (tempCheckPending) {
      long diff;
      if (checkStatusSHT()) {
        SHTgetTemp();
        tempCheckPending = 0;
        //After we get a temp, get the humiidity.
        updateHumidity();
      } else {
        diff = millis() - tempCheckPending;
        if (abs(diff) > 500) {
          tempCheckPending = 0;
          resetSHT();
        }
      }
    }
    
    //If we are waiting for a humidity update
    if (humCheckPending) {
      long diff2;
      if (checkStatusSHT()) {
        SHTgetHum();
        humCheckPending = 0;
      } else {
        diff2 = millis() - humCheckPending;
        if (abs(diff2) > 500) {
          humCheckPending = 0;
          resetSHT();
        }
      }
    }
    
    
    updateDisplay();

  }
  
  
  //Run periodically
  if ((millis() % 500) == 20) {
    updateTargetTemp();
    updateHVAC();
    if (currentIoState != ioState) {
      updateExpander();
    }
    updateDisplay();
  }

  
}
 
 

void updateExpander() {
  Wire.beginTransmission(EXPANDER_ADDY);
  Wire.write((uint8_t)0x12);
  Wire.write((uint8_t)ioState);
  Wire.endTransmission();
  
  currentIoState = ioState;
}


void checkButtons() {
  long stateUp = !digitalRead(BUTTON0_PIN);
  long stateDown = !digitalRead(BUTTON1_PIN);
  long milTime = millis();
  
  if (stateUp) {
    if (!buttonUp) {
      buttonUp = milTime;
      stateUp = 0;
    } else {
      stateUp = milTime - buttonUp - BUTTON_DEBOUNCE_TIME - buttonUpRep;
    }
  } else {
    buttonUp = 0;
    buttonUpRep = 0;
    showIP = 0;
  }
  
  if (stateDown) {
    if (!buttonDown) {
      buttonDown = milTime;
      stateDown = 0;
    } else {
      stateDown = milTime - buttonDown - BUTTON_DEBOUNCE_TIME - buttonDownRep;
    }
  } else {
    buttonDown = 0;
    buttonDownRep = 0;
    showIP = 0;
  }
  
  if ((buttonUp > 0) && (buttonDown > 0)) {
    showIP = 1;
    
    buttonUp = milTime;
    buttonDown = milTime;
  } else if (stateDown > 0) {
    buttonDownRep += BUTTON_REPEAT_TIME;
    tempTemperatureDown();
  } else if (stateUp > 0) {
    buttonUpRep += BUTTON_REPEAT_TIME;
    tempTemperatureUp();
  }
  
}



//************************************
//
//    Temperature/HVAC Functions
//
//************************************
void updateTargetTemp() {
  targetTemp = computeTargetTemp();
}

byte computeTargetTemp() {
  if (tempHold == 2) { //Permanant Hold
    return targetTemp;
  } else if (tempHold == 1) { //Temparary Hold
    long rem = tempTime - now();
    if ((rem <= 0)) { //Are we at (or pass) the end time
      if (DEBUG) {
        Serial.println(F("Clear Temp"));
      }
      tempHold = 0; //If so, clear the hold
      tempTime = 0;
    } else {
      return targetTemp; //We are still in the hold time.
    }
  }
  
  int i = 0;
  
  if ((hour() >= 0) && (hour() < times[i])) {
    i = TIME_SLOTS - 1;
  } else {
    while (hour() > times[i+1]) {
      i++;
      if (i == (TIME_SLOTS - 1)) {
        break;
      }
    }
  }
  
  targetTemp = temps[i];
  
  return temps[i];
  
}

void setHVACMode(int val) {
  HVACMode = val;
  
  if (HVACMode == 0) {
    changeHeat(0);
    changeCool(0);
  }
  
  updateHVAC();
}

void updateHVAC() {
  float bandMin = ((float)targetTemp - ((float)bandWidth / 10.0));
  float bandMax = ((float)targetTemp + ((float)bandWidth / 10.0));

  if (HVACMode == 1) {
    if (temperature > bandMax) {
      changeHeat(0);
    } else if (temperature < bandMin) {
      changeHeat(1);
    }
  } else if (HVACMode == 2) {
    if (temperature > bandMax) {
      changeCool(1);
    } else if (temperature < bandMin) {
      changeCool(0);
    }
  }
}

void changeHeat(byte state) {
  if (state) { //On
    ioState = ioState | HVAC_HEAT_PIN;
    changeLCDColor('R');
  } else { //Off
    ioState = ioState & ~HVAC_HEAT_PIN;
    changeLCDColor('G');
  }
  
}

void changeCool(byte state) {
  if (state) { //On
    ioState = ioState | HVAC_COOL_PIN;
    changeLCDColor('B');
  } else { //Off
    ioState = ioState & ~HVAC_COOL_PIN;
    changeLCDColor('G');
  }
}



void returnToProgramMode() {
  tempHold = 0;
  tempTime = 0;
}

void setTempTargetTemp(byte temp, byte hold = 1) {
  targetTemp = temp;
  tempTime = now() + TEMP_TIME;
  if (hold == 0) {
    tempHold = 1;
  } else {
    tempHold = hold;
  }
  
}

void tempTemperatureUp() {
  setTempTargetTemp(targetTemp + 1);
}

void tempTemperatureDown() {
  setTempTargetTemp(targetTemp - 1);
}


//************************************
//
//    LCD Functions
//
//************************************
void updateDisplay() {
  lcd.setCursor(0,0);
  if (showIP) {
    lcd.print(F("IP: "));
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      // print the value of each byte of the IP address:
      lcd.print(Ethernet.localIP()[thisByte], DEC);
      lcd.print(F(".")); 
    }
    lcd.println();
  } else {
    //lcd.print("");
    lcd.print(targetTemp);
    lcd.write((uint8_t)0);
    lcd.print(F("       "));
    lcd.print(temperature);
    lcd.write((uint8_t)0);

  
    lcd.setCursor(0,1);
  


    lcdPrintTime();
    
    lcd.print(F("  "));
    lcd.print(humidity);
    lcd.print(F("%"));
    
    

  }
  
}


void changeLCDColor(byte color) {
  switch (color) {
    case 'R':
      ioState = ioState & ~LCD_R_PIN; //Turn on R by taking it low
      ioState = ioState | LCD_G_PIN;
      ioState = ioState | LCD_B_PIN;
      break;
    case 'B':
      ioState = ioState | LCD_R_PIN; //Turn on R by taking it low
      ioState = ioState | LCD_G_PIN;
      ioState = ioState & ~LCD_B_PIN;
      break;
    
    case 'G':
    default:
      ioState = ioState | LCD_R_PIN; //Turn on R by taking it low
      ioState = ioState & ~LCD_G_PIN;
      ioState = ioState | LCD_B_PIN;

  }
  

}


void lcdPrintTime() {
  if (hour() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(hour());
  lcd.print(F(":"));
  if (minute() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(minute());
  lcd.print(F(":"));
  if (second() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(second());
}



//************************************
//
//    Shared Server Functions
//
//************************************
void loadLineToBuffer(EthernetClient client) {
  char c;
  
  bufferString = String();
  
  bufferIndex = 0;
  
  bufferString += (char)client.read();
  //buffer[1] = client.read();
  bufferString += (char)client.read();

  bufferIndex = 2;
  
  while (bufferString[bufferIndex-2] != '\r' && bufferString[bufferIndex-1] != '\n'  && client.available()) { // read full row and save it in buffer
    c = client.read();
    
    //If we are filling up the line buffer, end the line and trash the rest.
    if (bufferIndex == (STRING_BUF_SIZE-3)) {
      char prev = bufferString[bufferIndex-1];
      
      bufferIndex = STRING_BUF_SIZE-1;
      bufferString += '\r';
      bufferString += '\n';
      
      //Trash the rest of the line
      while(prev != '\r' && c != '\n') {
        prev = c;
        c = client.read();
      }
      
      break;
    }
    bufferString += c;
    bufferIndex++;
  }
}

void parseBufferStringParams(byte remote) {
  //int light = 0;
  String var;
  String val;
  String tmp;
  byte importSlots = 0;
  
  int index1 = 0;
  int index2 = 0;
  int i = 0;
  int intVal = 0;
  int endOfLine = 0;

  
  index1 = bufferString.indexOf('?');
  while (!endOfLine && (index1 > -1)) {
    getData = 1;
    if (index1 > -1) {
      index2 = bufferString.indexOf("=",index1);
      
      var = bufferString.substring((index1+1), (index2));
      
      index1 = bufferString.indexOf("&", index2);
      
      if (index1 == -1) {
        index1 = bufferString.indexOf(" ", index2);
        endOfLine = 1;
        //index1 = bufferIndex;
      }
      
      val = bufferString.substring((index2+1), (index1));
      if (DEBUG) {
        Serial.println(var);
        Serial.println(val);
      }
      if (importSlots) {
        if (var.indexOf("times") == 0) {
          
          for (i = 0; i < TIME_SLOTS; i++) {
            tmp = String("times");
            tmp.concat(String(i));
            if (var.equals(tmp)) {
              val.toCharArray(valBuffer, VAL_BUF_SIZE);;
              intVal = atoi(valBuffer);
              times[i] = intVal;
            }
          }
        }
        
        if (var.indexOf("temps") == 0) {
          for (i = 0; i < TIME_SLOTS; i++) {
            tmp = String("temps");
            tmp.concat(String(i));
            if (var.equals(tmp)) {
              val.toCharArray(valBuffer, VAL_BUF_SIZE);;
              intVal = atoi(valBuffer);
              temps[i] = intVal;
            }
          }
        }
      }
      
      if (var.equals("ts")) {
        if (val.equals(String(TIME_SLOTS))) {
          importSlots = 1;
          if (!remote) {
            remoteSendProgramChange = 1;
          }
        }
      } else if (var.equals("setTime")) {
        updateTime(stringToTime(val));
      } else if (var.equals("mode")) {
        val.toCharArray(valBuffer, VAL_BUF_SIZE);
        intVal = atoi(valBuffer);
        setHVACMode(intVal);
      } else if (var.equals("hold")) {
        val.toCharArray(valBuffer, VAL_BUF_SIZE);
        intVal = atoi(valBuffer);
        if (intVal == 0) {
          returnToProgramMode();
        } else {
          setTempTargetTemp(targetTemp, intVal);
        }
      } else if (var.equals("ta")) {
        val.toCharArray(valBuffer, VAL_BUF_SIZE);
        intVal = atoi(valBuffer);
        setTempTargetTemp(intVal, tempHold);
      }
      

      
      if (bufferString.charAt(index1+1) == ' ') {
        break;
      }
      
    }
  }
  

  
  if (importSlots) {
    saveTimesEEPROM();
  }
}


//************************************
//
//    Local Server Commands
//
//************************************
void processIncomingRequest() {
  char c;
  if (DEBUG) {
    Serial.println(F("Incoming"));
  }
  
  getData = 0;
  
    // an http request ends with a blank line
  boolean currentLineIsBlank = true;
  while (incomingClient.connected()) {
    if (incomingClient.available()) {
      
      loadLineToBuffer(incomingClient);

      if (DEBUG) {
        Serial.println(bufferString);
      }
      


      //End of incomming - blank line:
      if (bufferString.length() == 2) {
        //Redirect to strip GET params from history
        if (getData) {
          incomingClient.println(F("HTTP/1.1 302 REDIRECT"));
          //TODO get the real URI
          incomingClient.print(F("Location: /"));
          incomingClient.println(pageName);
          incomingClient.println();
          break;
        }
        
        // send a standard http response header
        if (DEBUG) {
          Serial.println("sending");
        }
        
        sendPage(pageName, incomingClient);
        
        
        break;
        
      } else {
        
        
        if (bufferString.indexOf("GET") != -1) {
          int i = 0;
          int i2 = 0;
          i = (bufferString.indexOf(' '))+1;
          i2 = bufferString.indexOf('?', i);
          if (i2 == -1) {
            i2 = bufferString.indexOf(' ', i);
          }

          String tmp = bufferString.substring((i+1), i2);
          
          if (tmp.length() == 1) {
            pageName = tmp;
          } else {
            pageName = "m";
            
          }
          
          //String val;
          if (DEBUG) {
            Serial.println(F("GET!"));
            Serial.println(pageName);
          }
          
          
          parseBufferStringParams(0);
          
          

        }
        

      }
      
      
      
    }
  }
  // give the web browser time to receive the data
  delay(1);
  if (DEBUG) {
    Serial.println(F("stopping"));
  }
  // close the connection:
  incomingClient.stop();

}


void sendPage(String &pageName, EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println();
  
  client.print(F("<html><head><style type=\"text/css\">a:visited{color:blue}</style></head><form method=GET>"));
  client.print(F("<a href=\"/m\">Main</a> <a href=\"/t\">Time</a> <a href=\"/s\">Time Slots</a><br>"));
  
  if (pageName.equals("s")) {
    client.print(F("<input type=hidden name=ts value="));
    client.print(TIME_SLOTS);
    client.print(F(">"));
    for (int i = 0; i < TIME_SLOTS; i++) {
      client.print(F("Time:<input type=text name=times"));
      client.print(i);
      client.print(F(" value="));
      client.print(times[i]);
      client.print(F("> Temp:<input type=text name=temps"));
      client.print(i);
      client.print(F(" value="));
      client.print(temps[i]);
      client.print(F("><br>"));
      

    }
  } else if (pageName.equals("t")) {
    client.print(F("Time:<br>"));
    client.print(hour());
    client.print(F(":"));
    if (minute() < 10) {
      client.print(F("0"));
    }
    client.print(minute());
    client.print(F(":"));
    if (second() < 10) {
      client.print(F("0"));
    }
    client.print(second());
    client.print(F(" "));
    client.print(month());
    client.print(F("/"));
    client.print(day());
    client.print(F("0"));
    client.print(year());
    client.print(F("<br>"));
    client.print(now());
    
    client.print(F("<br><br>Time in unix format.<br>See <a href=\"http://unixtimestamp.com\">UnixTimeStamp.com</a> for info.<br>"));
    client.print(F("Unix Time: <input type=text name=setTime><br>"));
  } else {
    
    
    client.print(F("Temperature: <font size=\"+1\"<b>"));
    client.print(temperature);
    client.print(F("</b>&deg;F</font><br>Relative Humidity: "));
    client.print(humidity);
    client.print(F("%<br><br>HVAC Mode: <select name=mode><option value=0 "));
    
    if (HVACMode == 0) {
        client.print(F("selected"));
    }
    client.print(F(">Off</option><option value=1 "));
    if (HVACMode == 1) {
        client.print(F("selected"));
    }
    client.print(F(">Heat</option><option value=2 "));
    if (HVACMode == 2) {
        client.print(F("selected"));
    }
    client.print(F(">Cool</option></select><br><br>"));
    
    
    client.print(F("Temperature Target: <input type=text name=ta value=\""));
    client.print(targetTemp);
    client.print(F("\">&deg;F<br>Temperature Mode: <select name=hold><option value=0 "));
    if (tempHold == 0) {
        client.print(F("selected"));
    }
    client.print(F(">Program</option><option value=1 "));
    if (tempHold == 1) {
        client.print(F("selected"));
    }
    client.print(F(">Temporary Hold</option><option value=2 "));
    if (tempHold == 2) {
        client.print(F("selected"));
    }
    client.print(F(">Perminant Hold</option></select><br><br>"));
  
    
    
    
  }
  
  client.print(F("<input type=submit>"));
  client.println(F("</form></html>"));
}


//************************************
//
//    Remote Server Commands
//
//************************************
void makeRemoteRequest() {
  if (DEBUG) {
    Serial.print(F("Connecting to: "));
    Serial.println(REMOTE_SERVER);
  }
  if (remoteConnection.connect(REMOTE_SERVER, 80)) {
   //Serial.println(F("Connected"));
    // Make a HTTP request:
    remoteConnection.print(F("GET "));
    remoteConnection.print(REMOTE_URI);
    
    //Send password
    remoteConnection.print(F("?p="));         //
    remoteConnection.print(REMOTE_PASSWORD);
    
    //Send Stats
    remoteConnection.print(F("&temp="));      //
    remoteConnection.print(temperature);
    remoteConnection.print(F("&hum="));       //
    remoteConnection.print(humidity);
    remoteConnection.print(F("&time="));      //
    remoteConnection.print(now());         
    remoteConnection.print(F("&ta="));    //
    remoteConnection.print(targetTemp);
    remoteConnection.print(F("&mode="));    //
    remoteConnection.print(HVACMode);
    
    //Temporary temperature
    remoteConnection.print(F("&hold="));      //
    remoteConnection.print(tempHold);
    if (tempHold) {
      remoteConnection.print(F("&tt="));//
      remoteConnection.print(tempTime);
    }
    
    remoteConnection.print(F("&ts="));  //
    remoteConnection.print(TIME_SLOTS);
    remoteConnection.print(F("&tc="));
    remoteConnection.print(remoteSendProgramChange);
    remoteSendProgramChange = 0;
    
    for (int i = 0; i < TIME_SLOTS; i++) {//
      remoteConnection.print(F("&times"));
      remoteConnection.print(i);
      remoteConnection.print(F("="));
      remoteConnection.print(times[i]);
      remoteConnection.print(F("&temps"));
      remoteConnection.print(i);
      remoteConnection.print(F("="));
      remoteConnection.print(temps[i]);
    }
    
    
  
    
    remoteConnection.println(F(" HTTP/1.0"));
    remoteConnection.println(F("User-Agent:Arduino"));
    remoteConnection.println();
    remoteRequestPending = 1;
  } else {
    if (DEBUG) {
      Serial.println(F("Connection Failed"));
    }
    remoteRequestPending = 0;
  }
}




void processRemoteResponse() {

  while (remoteConnection.available()) {
    loadLineToBuffer(remoteConnection);
    
    if (DEBUG) {
      Serial.println(bufferString);
    }
    
    if (bufferString.indexOf("!!RES!!?") != -1) {
      parseBufferStringParams(1);
    }
    
    bufferString = String();
    bufferIndex = 0;
  }


  if (!remoteConnection.connected()) {
    if (DEBUG) {
      Serial.println();
      Serial.println(F("disconnecting."));
    }
    remoteConnection.stop();

    remoteRequestPending = 0;
  }
}





//************************************
//
//    SHT11 Commands
//
//************************************
void updateTemperature() {
  SHTreqTemp();
}

void updateHumidity() {
  SHTreqHum();
}



//************************************
//
//    SHT11 Code, derived from http://code.google.com/p/sht11/
//
//************************************
#define TEMP_COMMAND     B00000011
#define HUMID_COMMAND    B00000101
#define RESET_COMMAND    B11111111


void resetSHT () {
  shiftOut(SHT11_DAT, SHT11_CLK, MSBFIRST, RESET_COMMAND);
  shiftOut(SHT11_DAT, SHT11_CLK, MSBFIRST, RESET_COMMAND);
}


void sendByteSHT(int val) {
  int ack;
  
  pinMode(SHT11_DAT, OUTPUT); 
  pinMode(SHT11_CLK, OUTPUT); 
  shiftOut(SHT11_DAT, SHT11_CLK, MSBFIRST, val);
  pinMode(SHT11_DAT, INPUT);      // prepare to read ack bit
  digitalWrite(SHT11_DAT,HIGH);   // engage pull-up resistors
  digitalWrite(SHT11_CLK, HIGH); // send 9th clock bit for ack 
  ack = digitalRead(SHT11_DAT);   // expect pull down by SHT11  
  if ((ack != LOW) && (DEBUG)) 
   Serial.println(F("ACK error 0")); 
  digitalWrite(SHT11_CLK, LOW); 
  ack = digitalRead(SHT11_DAT); 
  if ((ack != HIGH) && (DEBUG))
   Serial.println(F("ACK error 1")); 
}

// send a command to the SHTx sensor 
void sendCommandSHT(int command) { 
  int ack; 
  // transmission start 
  pinMode(SHT11_DAT, OUTPUT); 
  pinMode(SHT11_CLK, OUTPUT); 
  digitalWrite(SHT11_DAT, HIGH); 
  digitalWrite(SHT11_CLK, HIGH); 
  digitalWrite(SHT11_DAT, LOW); 
  digitalWrite(SHT11_CLK, LOW); 
  digitalWrite(SHT11_CLK, HIGH);
  digitalWrite(SHT11_DAT, HIGH); 
  digitalWrite(SHT11_CLK, LOW); 
  // shift out the command (the 3 MSB are address and must be 000, the last 5 bits are the command) 
  shiftOut(SHT11_DAT, SHT11_CLK, MSBFIRST, command); 
  // verify we get the right ACK
  pinMode(SHT11_DAT, INPUT);      // prepare to read ack bit
  digitalWrite(SHT11_DAT,HIGH);   // engage pull-up resistors
  digitalWrite(SHT11_CLK, HIGH); // send 9th clock bit for ack 
  ack = digitalRead(SHT11_DAT);   // expect pull down by SHT11  
  if ((ack != LOW) && (DEBUG))
   Serial.println(F("ACK error 0")); 
  digitalWrite(SHT11_CLK, LOW); 
  ack = digitalRead(SHT11_DAT); 
  if ((ack != HIGH) && (DEBUG))
   Serial.println(F("ACK error 1")); 
} 
 
// wait for the SHTx answer 
void waitForResultSHT() { 
  int ack;  
  pinMode(SHT11_DAT, INPUT); 
  for(int i=0; i<100; ++i) { 
    delay(10); 
    ack = digitalRead(SHT11_DAT); 
    if (ack == LOW) 
      break; 
  } 
  if ((ack == HIGH) && (DEBUG))
   Serial.println(F("ACK error Timeout")); 
} 

byte checkStatusSHT() {
  int ack;
  
  pinMode(SHT11_DAT, INPUT); 
  ack = digitalRead(SHT11_DAT);
  
  if (ack == LOW) {
    return 1;
  } else {
    return 0;
  }
}
 
// get data from the SHTx sensor 
int getData16SHT() {
  int val;
  
  // get the MSB (most significant bits) 
  pinMode(SHT11_DAT, INPUT); 
  pinMode(SHT11_CLK, OUTPUT); 
  val = shiftIn(SHT11_DAT, SHT11_CLK, MSBFIRST); 
  val *= 256; // this is equivalent to val << 8; 
  // send the required ACK 
  pinMode(SHT11_DAT, OUTPUT); 
  digitalWrite(SHT11_DAT, HIGH); 
  digitalWrite(SHT11_DAT, LOW); 
  digitalWrite(SHT11_CLK, HIGH); 
  digitalWrite(SHT11_CLK, LOW); 
  // get the LSB (less significant bits) 
  pinMode(SHT11_DAT, INPUT); 
  val |= shiftIn(SHT11_DAT, SHT11_CLK, MSBFIRST); 
  // send the required ACK 
  pinMode(SHT11_DAT, OUTPUT); 
  digitalWrite(SHT11_DAT, HIGH); 
  //digitalWrite(SHT11_DAT, LOW); 
  digitalWrite(SHT11_CLK, HIGH); 
  digitalWrite(SHT11_CLK, LOW); 
  
  return val;
}



void SHTreqTemp() {
  sendCommandSHT(TEMP_COMMAND);
  tempCheckPending = millis();
}

void SHTgetTemp() {
  int val;
  
  val = getData16SHT();

  temperature = ((float)val * 0.072 - 40.2) + CALIBRATION_TEMP; // 
}

void SHTreqHum() {
  sendCommandSHT(HUMID_COMMAND); 
  humCheckPending = millis();
}

void SHTgetHum() {
  int val;

  val = getData16SHT(); 

  humidity = -2.0468 + 0.5872 * val + -0.00040845 * val * val; // optimised V4 sensor table
  humidity = (FtoC(temperature) -25) * (0.01 + 0.00128 * val) + humidity; // temperature correction
}


float CtoF(float c) {

  return ((c * (9.0/5.0)) + 32);
}

float FtoC(float f) {

  return ((f - 32.0) * (5.0/9.0));
}


//***
//
//  Time Functions
//
//***
void updateTime(time_t time) {
  if(time > 0) {
    RTC.set(time);
    setTime(time);
  }
}

time_t stringToTime(String &time) {
  byte len = time.length();
  time_t timeVal = 0;
  char c = 0;
  
  if ((len < 10) || (len > 12)) { //This isn't a correctly formatted string if it's too long or too short
    return 0;
  }
  
  for (int i = 0; i < len; i++) {
    c = time[i];
    if( c >= '0' && c <= '9'){   
      timeVal = (10 * timeVal) + (c - '0') ; // convert digits to a number
    }
  }
  
  return timeVal;
}











