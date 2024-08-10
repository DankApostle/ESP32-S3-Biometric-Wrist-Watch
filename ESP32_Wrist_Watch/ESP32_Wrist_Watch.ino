#include <Wire.h>
#include "MAX30105.h"
#include "ADXL362.h"
#include <DS3231.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "spo2_algorithm.h"
#include <driver/rtc_io.h>
#include "Button2.h"

//Defining OLED settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET -1

//Defining accelerometer pins
#define SPI_SLAVE_SELECT_PIN GPIO_NUM_44
#define SPI_INT1_PIN GPIO_NUM_43

//Defining battery voltage sense pin
#define VBAT GPIO_NUM_1

//Defining user button pins
#define B1 GPIO_NUM_2
#define B2 GPIO_NUM_3
#define B3 GPIO_NUM_4

//Defining the button debounce time in milliseconds
#define DEBOUNCE_TIME 30

//Initializing sensors and the OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, I2C_SPEED_FAST, I2C_SPEED_FAST);
DS3231 myRTC;
MAX30105 particleSensor;
ADXL362 acc(SPI_SLAVE_SELECT_PIN);

Button2 but1, but2, but3;

//Heart rate sensor variables
uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid

//User interface variables
uint8_t MenuCounter = 0;
uint8_t B1Counter = 0;
uint8_t B2Counter = 0;
uint8_t B3Counter = 0;

//RTC variables
bool century = false;
bool h12Flag;
bool pmFlag;

//Used to track battery voltage
uint8_t Bat;

//Declaring HR history array as RTC memory so it doesn't dissapear after sleep
RTC_DATA_ATTR uint8_t HRhistory[20][8] = {0};

void setup() {
  particleSensor.begin(Wire, I2C_SPEED_FAST);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false);

  but1.setDebounceTime(DEBOUNCE_TIME);
  but2.setDebounceTime(DEBOUNCE_TIME);
  but3.setDebounceTime(DEBOUNCE_TIME);

  //Use internal pull-up resistors for button inputs and INT1 of accelerometer
  pinMode(B1, INPUT_PULLUP);
  pinMode(B2, INPUT_PULLUP);
  pinMode(B3, INPUT_PULLUP);

  but1.begin(B1);
  but1.setClickHandler(click);

  but2.begin(B2);
  but2.setClickHandler(click);

  but3.begin(B3);
  but3.setClickHandler(click);

  pinMode(SPI_INT1_PIN, INPUT_PULLUP);

  acc.init();

  //Keep Button 1 settings on during sleep
  rtc_gpio_hold_en(B1);

  //Adjusting MAX30102 settings
  byte ledBrightness = 30; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  particleSensor.shutDown();

  acc.activateAutonomousMotionSwitch(1500,500,60000,false,false);

  //Set up deep sleep mode
  esp_sleep_enable_ext0_wakeup(B1,LOW);

  //Read battery voltage every time the watch wakes up from sleep
  Bat = BatteryMeasurement();

}

//Used for timing the sleep
unsigned long startMillis = millis();

//Variables for keeping track of menus
bool HRmeasuring = false;
bool HRview = false;
int8_t Timeset = 0;

void loop(){
  but1.loop();
  but2.loop();
  but3.loop();

  //Read the current date and time
  int hours = myRTC.getHour(h12Flag, pmFlag);
  int minutes = myRTC.getMinute();
  int seconds = myRTC.getSecond();
  int date = myRTC.getDate();
  int month = myRTC.getMonth(century);
  int year = myRTC.getYear();
  
  //Show time for 3 seconds
  if((millis() - startMillis) <= 5000){
    //Print time and date
    display.clearDisplay();
    display.setTextSize(2);             
    display.setTextColor(SSD1306_WHITE);        
    display.setCursor(16,16);
    //Add zero if the hours are a 1 digit number
    if(hours < 10){
      display.print("0");
    }             
    display.print(hours);
    display.print(":");
    //Add zero if the minutes are a 1 digit number
    if(minutes < 10){
      display.print("0");
    }        
    display.print(minutes);
    display.print(":");
    //Add zero if the seconds are a 1 digit number
    if(seconds < 10){
      display.print("0");
    }        
    display.print(seconds);
    display.setCursor(6,32);
    //Add zero if the date is a 1 digit number  
    if(date < 10){
      display.print("0");
    }        
    display.print(date);
    display.print(".");
    //Add zero if the month is a 1 digit number 
    if(month < 10){
      display.print("0");
    }        
    display.print(month);
    display.print(".");
    display.print("20");
    display.print(year);

    //Print the battery percentage
    display.setCursor(75, 0);
    display.setTextSize(1);
    display.print("Bat: ");
    display.print(Bat);
    display.print("%");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    //Display warning if battery percentage is under 20%
    if(Bat <= 20){
      display.setCursor(35,53);
      display.print("Low Battery!");
    }

    display.display();
  }

  //If button 2 is pressed measure the heart rate and oxygen levels, then show the result until button 1 is pressed
  if(B2Counter == 1){
    HRmeasuring = true;
    
    display.clearDisplay();
    display.setCursor(0,16); 
    display.setTextSize(1); 
    display.print("Measuring heart rate:");

    //Print the battery percentage
    display.setCursor(70, 0);
    display.setTextSize(1);
    display.print("Bat: ");
    display.print(Bat);
    display.print("%");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.display();
    
    //Measure the heart rate and blood oxygen level
    PulseMeasure();

    //Turn the sensor off to conserve power
    particleSensor.shutDown();
    
    //Show the results until button 1 is pressed
    while(HRmeasuring == true){
      but1.loop();

      //Print the measured heart rate and oxygen levels
      display.clearDisplay();
      display.setCursor(16,20); 
      display.setTextSize(1); 
      display.print("Pulse: ");

      //If heart rate is -999 BPM display an error, otherwise display the measured value
      if(heartRate == (-999)){
        display.print("Error");
        heartRate = 0;
      }
      else{
        display.print(heartRate);
        display.print(" BPM");
      }

      display.setCursor(16,28);  
      display.print("Oxygen: ");
      //If oxygen level is -999% display an error, otherwise display the measured value
      if(spo2 == (-999)){
        display.print("Error");
        spo2 = 0;
      }
      else{
        display.print(spo2);
        display.print("%");
      }

      //Print the battery percentage
      display.setCursor(70, 0);
      display.setTextSize(1);
      display.print("Bat: ");
      display.print(Bat);
      display.print("%");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      //Check if the user was active
      if(digitalRead(SPI_INT1_PIN) == LOW){

        //If user wasn't active and heart rate is over 100 display a warning
        if(heartRate >= 100){
          display.setCursor(16, 40);
          display.print("High heart rate!");
        }
      }

      //If blood oxygen level is under 90 display a warning
      if(spo2 <= 90){
        display.setCursor(16, 48);
        display.print("Low oxygen level!");
      }

      display.display();
      
      //If button 1 is pressed turn the display off and go to deep sleep
      if(B1Counter == 1){
        HRmeasuring = false;
        B2Counter = 0;
        B1Counter = 0;
        display.fillScreen(SSD1306_BLACK);
        display.display();
        esp_deep_sleep_start(); 
      }
      
    }

    //Save previous measurements by moving them down 1 spot
      for(int i=19; i>0; i--){
        for(int j=0; j<8; j++){
          HRhistory[i][j] = HRhistory[i-1][j];
        }
      }

      //Save the new measurement on 1st spot
      
      HRhistory[0][0] = heartRate;
      HRhistory[0][1] = spo2;
      HRhistory[0][2] = hours;
      HRhistory[0][3] = minutes;
      HRhistory[0][4] = seconds;
      HRhistory[0][5] = date;
      HRhistory[0][6] = month;
      HRhistory[0][7] = year;

  }

  if(B3Counter == 1){
    HRview = true;
    int8_t x = 0;

    //Show past 25 measurements
    while(HRview == true){
      B1Counter = 0;
      B2Counter = 0;
      B3Counter = 0;

      but1.loop();
      but2.loop();
      but3.loop();

      display.clearDisplay();
      display.setCursor(0,0);
      display.setTextSize(1);
      display.print("Measurement history:");
      display.drawLine(0, 10, 116, 10, SSD1306_WHITE);

      for(int i=3; i<63; i+=3){
        display.drawRect(126, i, 2, 2, SSD1306_WHITE);
      }
      
      //If button 2 is pressed increment the counter variable
      x = x + B2Counter - B3Counter;

      if(x > 19){
        x = 0;
      }

      if(x < 0){
        x = 19;
      }

      //Used to show which of the 20 readings we're on
      display.drawRect(122, ((x+1)*3), 2, 2, SSD1306_WHITE);

      //Print the date of the measurement
      display.setCursor(0,16);
      display.print("Date: ");
      //Add zero if the date is a 1 digit number
      if(HRhistory[x][5] < 10){
        display.print("0");
      } 
      display.print(HRhistory[x][5]);
      display.print(".");
      //Add 0 if the month is a 1 digit number
      if(HRhistory[x][6] < 10){
        display.print("0");
      } 
      display.print(HRhistory[x][6]);
      display.print(".20");
      display.print(HRhistory[x][7]);

      //Print the time of the measurement
      display.setCursor(0,24);
      display.print("Time: ");
      //Add zero if the hours are a 1 digit number
      if(HRhistory[x][2] < 10){
        display.print("0");
      } 
      display.print(HRhistory[x][2]);
      display.print(":");
      //Add zero if the minutes are a 1 digit number
      if(HRhistory[x][3] < 10){
        display.print("0");
      } 
      display.print(HRhistory[x][3]);
      display.print(":");
      //Add zero if the seconds are a 1 digit number
      if(HRhistory[x][4] < 10){
        display.print("0");
      } 
      display.print(HRhistory[x][4]);
      
      //Print the heart rate
      display.setCursor(0,32);
      display.print("Heart rate: ");

      //If heart rate was 0 display an error, otherwise display the actual measurement
      if(HRhistory[x][0] == 0){
        display.print("Error");
      }
      else{
        display.print(HRhistory[x][0]);
        display.print(" BPM ");
      }
      

      //Print oxygen levels
      display.setCursor(0,40);
      display.print("Oxygen level: ");

      //If blood oxygen level was 0 display an error, otherwise display the actual measurement
      if(HRhistory[x][1] == 0){
        display.print("Error");
      }
      else{
        display.print(HRhistory[x][1]);
        display.print("%");
      }

      display.display();
      
      //If button 1 is pressed again return from the menu and go to sleep
      if(B1Counter == 1){
        HRview = false;
        B1Counter = 0;
        B2Counter = 0;
        B3Counter = 0;
        display.fillScreen(SSD1306_BLACK);
        display.display();
        esp_deep_sleep_start(); 
      }

    }
  }

  //If button 1 is pressed enter the time setting menu
  //Set the Date
  while(B1Counter == 1){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    date = date + B2Counter - B3Counter;

    if(date > 31){
      date = 1;
    }
    if(date < 1){
      date = 31;
    }

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.print("Date:");
    display.setCursor(54,20);
    display.print(date);
    display.display();
  }

  //Set the month
  while(B1Counter == 2){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    month = month + B2Counter - B3Counter;

    if(month > 12){
      month = 1;
    }

    if(month < 1){
      month = 12;
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Month: ");
    display.setCursor(54,20);
    display.print(month);
    display.display();
  }

  //Set the year
  while(B1Counter == 3){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    year = year + B2Counter - B3Counter;

    //Just in case the year is under 2024 set it back
    if(year < 24){
      year = 24;
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Year: ");
    display.setCursor(44,20);
    display.print("20");
    display.print(year);
    display.display();
  }

  //Set the hours
  while(B1Counter == 4){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    hours = hours + B2Counter - B3Counter;

    if(hours > 23){
      hours = 0;
    }

    if(hours < 0){
      hours = 23;
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Hours: ");
    display.setCursor(54,20);
    display.print(hours);
    display.display();
  }

  //Set the minutes
  while(B1Counter == 5){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    minutes = minutes + B2Counter - B3Counter;

    if(minutes > 59){
      minutes = 0;
    }

    if(minutes < 0){
      minutes = 59;
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Minutes: ");
    display.setCursor(54,20);
    display.print(minutes);
    display.display();
  }

  //Set the seconds
  while(B1Counter == 6){
    B2Counter = 0;
    B3Counter = 0;

    but1.loop();
    but2.loop();
    but3.loop();

    seconds = seconds + B2Counter - B3Counter;

    if(seconds > 59){
      seconds = 0;
    }

    if(seconds < 0){
      seconds = 59;
    }

    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Seconds: ");
    display.setCursor(54,20);
    display.print(seconds);
    display.display();

  }
  //Save the new values and exit time setting menu
  if(B1Counter == 7){
    myRTC.setClockMode(false);
    myRTC.setYear(year);
    myRTC.setMonth(month);
    myRTC.setDate(date);
    myRTC.setHour(hours);
    myRTC.setMinute(minutes);
    myRTC.setSecond(seconds);
    B1Counter = 0;
    B2Counter = 0;
    B3Counter = 0;
  }

  //Turn off the display and enter deep sleep
  if(millis() - startMillis > 5000){
    display.fillScreen(SSD1306_BLACK);
    display.display();
    esp_deep_sleep_start(); 
  }
  
}

//Function for measuring the heart rate
void PulseMeasure(){
  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  particleSensor.wakeUp();

  delay(500);

  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

}

//Function to read the battery voltage level and return it as battery percentage
uint8_t BatteryMeasurement(){
  uint8_t BatPercent;

  //Read the voltage on battery pin in millivolts
  uint16_t BatValue = analogReadMilliVolts(VBAT);

  //Crude linear approximation of battery percentage
  BatPercent = map(BatValue, 1700, 2100, 0, 100);

  //Just in case we read under or over given values make sure it ends up within range of percentage
  if(BatValue < 1700){
    BatPercent = 0;
  }
  if(BatValue > 2100){
    BatPercent = 100;
  }

  return BatPercent;
}

//Function to track button presses
void click(Button2& btn) {
  if (btn == but1) {
    B1Counter++;
  } 
  if (btn == but2) {
    B2Counter++;
  }
  if (btn == but3) {
    B3Counter++;
  }
}