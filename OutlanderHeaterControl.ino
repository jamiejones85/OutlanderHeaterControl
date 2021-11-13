#include <mcp_can.h>
#include <SPI.h>
#include <TaskScheduler.h>

#define INVERTPOT true
//#define OPENINVERTERCONTACTORS //uncomment to check can bus for Open Inverter opmode for contactors, prevents heating over precharge resistor
#ifdef OPENINVERTERCONTACTORS
unsigned long inverterLastRec;
byte inverterStatus;
#endif

#define MAXTEMP 85
#define MINTEMP 40
unsigned int targetTemperature = 50;
bool enabled = false;
bool hvPresent = false;
bool heating = false;
unsigned int currentTemperature;
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

  if (INVERTPOT) {
      targetTemperature = map(sensorValue, 1023, 100, MINTEMP, MAXTEMP);
  } else {
      targetTemperature = map(sensorValue, 100, 1023, MINTEMP, MAXTEMP);
  }

  digitalWrite(ledPin, enabled);
   
  //send 0x188
  #ifdef OPENINVERTERCONTACTORS
  bool contactorsClosed = inverterStatus == 0x01;
  #else
  bool contactorsClosed = true;
  #endif
  if (contactorsClosed && enabled && currentTemperature < targetTemperature) {
   uint8_t canData[8];
   canData[0] = 0x03;
   canData[1] = 0x50;
   canData[2] = 0xA2;
   canData[3] = 0x4D;
   canData[4] = 0x00;
   canData[5] = 0x00;
   canData[6] = 0x00;
   canData[7] = 0x00;
   CAN.sendMsgBuf(0x188, 0, sizeof(canData), canData);
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
          } else if (buf[5] == 0x20 || buf[5] == 0x40) {
            heating = true;
          }
          //hv status
          if (buf[6] == 0x09) {
            hvPresent = false;
          } else if (buf[6] == 0x00) {
            hvPresent = true;
          }

          //temperatures
          unsigned int temp1 = buf[3] - 50;
          unsigned int temp2 = buf[4] - 50;
          if (temp2 > temp1) {
            currentTemperature = temp2;
          } else {
            currentTemperature = temp1;
          }
        }
        #ifdef OPENINVERTERCONTACTORS
        if (canId == 0x02) {
          inverterLastRec = millis();
          inverterStatus = buf[0];
        }
        #endif

    }

    #ifdef OPENINVERTERCONTACTORS
      if(inverterLastRec + 200 < millis()) {
        inverterStatus = 0;
      }
    #endif
}
