#include <mcp_can.h>
#include <SPI.h>
#include <TaskScheduler.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define INVERTPOT true
#define OPENINVERTERCONTACTORS //uncomment to check can bus for Open Inverter opmode for contactors, prevents heating over precharge resistor
#ifdef OPENINVERTERCONTACTORS
unsigned long inverterLastRec;
byte inverterStatus;
#endif
unsigned long temperatureLastRec;


#define MAXTEMP 85
#define MINTEMP 40
unsigned int targetTemperature = 0;
bool enabled = false;
bool hvPresent = false;
bool heating = false;
int power = 20;
int currentTemperature = 0;
const int potPin = A0;
const int ledPin = 3;


const int SPI_CS_PIN = 10;

MCP_CAN CAN(SPI_CS_PIN); 

void ms10Task();
void ms100Task();
void ms1000Task();

Task ms10(10, -1, &ms10Task);
Task ms100(100, -1, &ms100Task);
Task ms1000(1000, -1, &ms1000Task);

Scheduler runner;

void setup() {
    Serial.begin(115200);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    }
    displayOff();
    Serial.println("Outlander Heater Control");
    pinMode(ledPin, OUTPUT);
    
    while (CAN_OK != CAN.begin(CAN_500KBPS, MCP_8MHz))              // init can bus : baudrate = 500k
    {
        Serial.println("CAN BUS Shield init fail");
        Serial.println(" Init CAN BUS Shield again");
        delay(100);
    }
    Serial.println("CAN BUS Shield init ok!");

    runner.init();

    runner.addTask(ms10);
    ms10.enable();

    runner.addTask(ms100);
    ms100.enable();

    runner.addTask(ms1000);
    ms1000.enable();

}

void displayTemperature(void) {
  display.clearDisplay();
  display.setTextSize(3); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 10);
  display.print(currentTemperature);
  display.print("/");
  display.print(targetTemperature);

  if (power == 2) {
      display.fillRect(0, 0, 10, SCREEN_HEIGHT, SSD1306_WHITE);
  } else if (power == 1) {
      display.fillRect(0, SCREEN_HEIGHT/2, 10, SCREEN_HEIGHT, SSD1306_WHITE);
  }

  display.display();      // Show initial text
}

void displayOff(void) {
  display.clearDisplay();
  display.setTextSize(3); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 10);
  display.println(F("Off"));
  display.display();      // Show initial text
}


void ms10Task() {
  //send 0x285
   uint8_t canData[8];
   canData[0] = 0x00;
   canData[1] = 0x00;
   canData[2] = 0x14;
   canData[3] = 0x21;
   canData[4] = 0x90;
   canData[5] = 0xFE;
   canData[6] = 0x0C;
   canData[7] = 0x10;

   CAN.sendMsgBuf(0x285, 0, sizeof(canData), canData);
}

void ms100Task() {
  int sensorValue = analogRead(potPin);

  if (INVERTPOT) {
    if (sensorValue < 923) {
      enabled = true;
    } else {
      enabled = false;
    }
  } else {
    if (sensorValue > 100) {
      enabled = true;
    } else {
      enabled = false;
    }
  }

  //if heater is not sending feedback, disable it, safety and that
  if (millis() - temperatureLastRec > 1000) {
    enabled = false;
    Serial.println("No Temperature recieved");
  }

  if (INVERTPOT) {
      targetTemperature = map(sensorValue, 1023, 100, MINTEMP, MAXTEMP);
  } else {
      targetTemperature = map(sensorValue, 100, 1023, MINTEMP, MAXTEMP);
  }

    //send 0x188
  #ifdef OPENINVERTERCONTACTORS
  bool contactorsClosed = inverterStatus == 0x01;
  if (!contactorsClosed) {
    enabled = false;
  }
  #else
  bool contactorsClosed = true;
  #endif

  digitalWrite(ledPin, enabled);

  if (enabled) {
    displayTemperature();
  } else {
    displayOff();
  }
   

  if (contactorsClosed && enabled && currentTemperature < targetTemperature) {
   uint8_t canData[8];
   canData[0] = 0x03;
   canData[1] = 0x50;
   canData[3] = 0x4D;
   canData[4] = 0x00;
   canData[5] = 0x00;
   canData[6] = 0x00;
   canData[7] = 0x00;

   //switch to lower power when reaching target temperature
   if (currentTemperature < targetTemperature - 10) {
    canData[2] = 0xA2;
    power = 2;
   } else {
    canData[2] = 0x32;
    power = 1;
   }
   
    CAN.sendMsgBuf(0x188, 0, sizeof(canData), canData);
  } else {
    power = 0;
  }

}


void ms1000Task() {
  Serial.println("Heater Status");
  Serial.print("HV Present: ");
  Serial.print(hvPresent);
  Serial.print(" Heater Active: ");
  Serial.print(heating);
  Serial.print(" Water Temperature: ");
  Serial.print(currentTemperature);
  Serial.println("C");
  Serial.println("");
  Serial.println("Settings");
  Serial.print(" Heating: ");
  Serial.print(enabled);
  Serial.print(" Desired Water Temperature: ");
  Serial.print(targetTemperature);
  Serial.print(" Inverter: ");
  Serial.print(inverterStatus);
  Serial.println("");
  Serial.println("");

}

void loop() {
  unsigned char len = 0;
  unsigned char buf[8];
  // put your main code here, to run repeatedly:
  runner.execute();
  if(CAN_MSGAVAIL == CAN.checkReceive())            // check if data coming
  {
        CAN.readMsgBuf(&len, buf);    // read data,  len: data length, buf: data buf

        unsigned int canId = CAN.getCanId();
        if (canId == 0x398) {
          //Heater status
          if (buf[5] == 0x00) {
            heating = false;
            power = 0;
          } else if (buf[5] > 0) {
            heating = true;
          }
          //hv status
          if (buf[6] == 0x09) {
            hvPresent = false;
          } else if (buf[6] == 0x00) {
            hvPresent = true;
          }

          //temperatures
          unsigned int temp1 = buf[3] - 40;
          unsigned int temp2 = buf[4] - 40;
          if (temp2 > temp1) {
            currentTemperature = temp2;
          } else {
            currentTemperature = temp1;
          }
          temperatureLastRec = millis();
        }
        #ifdef OPENINVERTERCONTACTORS
        if (canId == 0x02) {
          inverterLastRec = millis();
          inverterStatus = buf[0];
        }
        #endif

    }

    #ifdef OPENINVERTERCONTACTORS
//      if(inverterLastRec + 500 < millis()) {
//        inverterStatus = 0;
//      }
    #endif

}
