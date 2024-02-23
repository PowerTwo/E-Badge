#include <MicroNMEA.h>

// ESP32 C3 SERIAL1 (second UART)
HardwareSerial mySerial2(1);//Init Hardware Serial to communicate with LC76F module

#define EN_RAK 6 //Pin to enable RAK3172 SiP
#define EN_LC76F 1 //Pin to enable LC76F module
#define VBCKP_LC76F 0 //Pin to use VBCKP of LC76F module 
#define LED 5 //Pin to control LED Blue
#define USER_BUTTON 10 //Pin to control User button from ESP32

//Set RX and TX pin to communicate with RAK3172 SiP
#define rx1Pin 20
#define tx1Pin 21

#define GPS_INTERVAL 60000 //Time to get location, unit is ms

//Set RX and TX pin to communicate with LC76F module
int rx2Pin = 3;
int tx2Pin = 7;

//Create a flag to signal when there is an interrupt from the User button
uint8_t button_flag = 0;

//Create payload of LoRaWAN packet
uint8_t payload[64] = {0};
uint8_t payload_length;

//Create parameters of location
//uint32_t lat = 108675;
//uint32_t lng = 1067938;
uint32_t lat = 0;
uint32_t lng = 0;

// Rain Gauge battery
#define LS_ADC_AREF 3.0f
#define LS_BATVOLT_R1 1.0f 
#define LS_BATVOLT_R2 2.0f
#define LS_BATVOLT_PIN 4

uint16_t voltage_adc;
uint16_t voltage;

//Create parameter to get data from MicroNMEA library
int timezone = 7 ;
int  year; int mon; int day; int hr; int minute; double sec;
String* PRN;
char nmeaBuffer[100];
MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));
String revString;
char revChar[100];
int len = 0;

//Set time to get and save data from LC76F module
uint32_t quectelDelayTime = 500;
unsigned long currentMillis = 0, getSensorDataPrevMillis = 0, getGPSPrevMillis = 0;
bool _flag = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 AT Commands for RAK3172 SiP");

  //Set parameters for RAK3172 SiP
  pinMode(tx1Pin, OUTPUT);
  pinMode(rx1Pin, INPUT);
  pinMode(EN_RAK, OUTPUT);

  //Set interrupt for User button
  pinMode(USER_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(USER_BUTTON), i_button_isr, RISING);

  //set the resolution to 12 bits (0-4096)
  analogReadResolution(12);

  //Set output mode for LED
  pinMode(LED, OUTPUT); // LED

  //Set Serial1 of ESP32 for RAK3172 SiP
  digitalWrite(EN_RAK, HIGH);
  delay(1000);
  Serial1.begin(115200, SERIAL_8N1, rx1Pin, tx1Pin);
  delay(1000);  

  //AT Command for RAK3712 SiP
  Serial1.println("ATE");
  delay(1000);
  
  while (Serial1.available()){
    Serial.write(Serial1.read()); // read it and send it out Serial (USB)
  }

  Serial.println("setup at command");
  Serial1.println("AT+NWM=1");
  delay(500);
  Serial1.println("AT+NJM=0");
  delay(500);
  Serial1.println("AT+CLASS=A");
  delay(500);

  //Server https://eu1.cloud.thethings.network/console
  Serial1.println("AT+BAND=9");//Set frequency band is AS923 Groưp 2
  delay(500);
  Serial1.println("AT+DEVADDR=260BE8E0");
  delay(500);
  Serial1.println("AT+APPSKEY=1B7AAB91AD987CA6AF11081861FA5E49");
  delay(500);
  Serial1.println("AT+NWKSKEY=11BB2F2767E027329F874AED5C28D7E4");
  
  delay(500);
  Serial1.println("AT+JOIN=1:1:10:8");
  delay(1000);
  Serial1.end();

  //Set output mode for LC76F
  pinMode(EN_LC76F, OUTPUT);
  digitalWrite(EN_LC76F, HIGH);
}

void loop()
{
  //When flag of user button is enable
  if (button_flag > 0){
    //Blink LED after Send packet
    digitalWrite(LED, HIGH);   
    delay(200);                    
    digitalWrite(LED, LOW);    
    delay(200);  

    //OFF serial port of LC76F and ON serial port of RAK3172 SiP
    mySerial2.end();
    delay(1000);
    Serial1.begin(115200, SERIAL_8N1, rx1Pin, tx1Pin);
    delay(1000);

    //Get data from LC76F
    GPS_showData();
    delay(1000);

    //Battery variables
    voltage_adc = (uint16_t)analogRead(LS_BATVOLT_PIN);
    voltage = (uint16_t)((LS_ADC_AREF / 4.096) * (LS_BATVOLT_R1 + LS_BATVOLT_R2) / LS_BATVOLT_R2 * (float)voltage_adc);
  
    Serial.print("Voltage: ");
    Serial.println(voltage);
    
    //Send LoRaWAN packet to Gateway
    Uplink_message();
    delay(2000);

    //Check Serial after AT Command for RAK3172 SiP
    if (Serial1.available())
    { 
      while (Serial1.available())
        Serial.write(Serial1.read()); 
    }

    //Assign the value back to the flag
    button_flag=0;
    delay(5000);

    //OFF serial port of RAK3172 SiP and ON serial port of LC76F
    Serial1.end();
    delay(1000);
    mySerial2.begin(9600, SERIAL_8N1, rx2Pin, tx2Pin);
  }

  //Assign currentMillis from millis function
  currentMillis = millis();
  
  // Parse data from Quectel
  if (currentMillis - getGPSPrevMillis > quectelDelayTime)
  {
    getGPSPrevMillis = currentMillis; 
    quectel_getData(revString, revChar, len);
  }
  
  // Print sensor & gps data
  if (currentMillis - getSensorDataPrevMillis > GPS_INTERVAL)
  {
    getSensorDataPrevMillis = currentMillis;
    // GPS
    GPS_showData();
    Serial.println("**********************************************");
  }

  //Set time to sleep
  //Serial.print("Valid wakeup GPIO: ");
  //Serial.println(esp_sleep_is_valid_wakeup_gpio((gpio_num_t)(USER_BUTTON)));
  
  //gpio_wakeup_enable((gpio_num_t)(USER_BUTTON), GPIO_INTR_LOW_LEVEL);
  //esp_sleep_enable_gpio_wakeup();
  //esp_light_sleep_start();
  //delay(1000); 

  //  gpio_hold_en((gpio_num_t)EN_LC76F);
  //  gpio_hold_en((gpio_num_t)EN_RAK);
  //  gpio_deep_sleep_hold_en();

  //wake up 1 second later and then go into deep sleep
  //  esp_sleep_enable_timer_wakeup(100000000); // 10 sec
  //  
  //  esp_deep_sleep_enable_gpio_wakeup(USER_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
  //  esp_deep_sleep_start();
}

//When the button is pressed, the interrupt occurs
void i_button_isr(void) {
    button_flag++; 
}

//Get data from LC76F
void quectel_getData(String _revString, char *_revChar, int _len)
{
  int count = 0;
  while (mySerial2.available() && count < 50)
  {
    _revString = mySerial2.readStringUntil(0x0D);
    //Serial.println(_revString);
    _len = _revString.length() + 1;
    _revString.toCharArray(_revChar, _len);
    for (int i = 0; i < _len; i++)
    {
      // Serial2.print(*(_revChar + i));
      nmea.process(*(_revChar + i));
    }
    //Serial.print(_revString);
    count++;
  }
  quectelDelayTime = 5;
}

unsigned long unixTimestamp(int year, int month, int day, int hour, int min, int sec) {
  const short days_since_beginning_of_year[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int leap_years = ((year - 1) - 1968) / 4
                   - ((year - 1) - 1900) / 100
                   + ((year - 1) - 1600) / 400;
  long days_since_1970 = (year - 1970) * 365 + leap_years
                         + days_since_beginning_of_year[month - 1] + day - 1;
  if ( (month > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) )
    days_since_1970 += 1; /* +leap day, if year is a leap year */
  return sec + 60 * ( min + 60 * (hour + 24 * days_since_1970) );
}

//Get location after get data from LC76F
void GPS_showData(void)
{ 
  Serial.print("Valid fix: ");
  Serial.println(nmea.isValid() ? "yes" : "no");
  if(nmea.isValid()){ 
    Serial.print("Nav. system: ");
    if (nmea.getNavSystem())
      Serial.println(nmea.getNavSystem());
    else
      Serial.println("none");
    double latitude = nmea.getLatitude();
    double longitude = nmea.getLongitude();

    Serial.print("GPS position: ");
    Serial.print(latitude / 1.0e6, 4);
    Serial.print(", ");
    Serial.println(longitude / 1.0e6, 4);

    Serial.print("Sat in view: ");

    uint32_t unixt = unixTimestamp(nmea.getYear(), nmea.getMonth(), nmea.getDay(), nmea.getHour(), nmea.getMinute(), nmea.getSecond());
    Serial.print("  Unix time GPS: ");
    Serial.println(unixt);

    Serial.print("Numbers of Sat: ");
    Serial.println(nmea.getNumSatellites());

    //Get location to send LoRaWAN packet
    lat = (uint32_t) (latitude);
    lng = (uint32_t) (longitude);

    Serial.print("lat: ");
    Serial.println(lat);

    Serial.print("lng: ");
    Serial.println(lng);
  }
}

//Config LoRaWAN packet
bool Uplink_message()
{
    payload_length = 0;
    payload[payload_length++] = (uint8_t) lat;
    payload[payload_length++] = (uint8_t) (lat >> 8);
    payload[payload_length++] = (uint8_t) (lat >> 16);
    payload[payload_length++] = (uint8_t) (lat >> 24);
    payload[payload_length++] = (uint8_t) lng;
    payload[payload_length++] = (uint8_t) (lng >> 8);
    payload[payload_length++] = (uint8_t) (lng >> 16);
    payload[payload_length++] = (uint8_t) (lng >> 24);
    payload[payload_length++] = (uint8_t)(voltage >> 8) & 0xff;
    payload[payload_length++] = (uint8_t)voltage & 0xff;
    
    Serial1.print("AT+SEND=2:");
    for(int i = 0; i < payload_length; i++){
        Serial1.printf("%02X",payload[i]);
    }
    Serial1.printf("%02X\r\n",payload[payload_length]);
    Serial.println("Uplink message");
    delay(1000);
    return true;
}
