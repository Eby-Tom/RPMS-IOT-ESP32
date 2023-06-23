#include <WiFi.h>
#include <Wire.h>
#include <string.h>
#include<FirebaseESP32.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "DHT.h"
#include "time.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

const char * ntpServer = "time.google.com";

#define REPORTING_PERIOD_MS 1000
#define MAX_BRIGHTNESS 255
#define DHT11PIN 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET - 1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define API_KEY "AIzaSyBgaiX0lXGlB0UWfTmESvb3Gykywy8yp1k"
#define DATABASE_URL "https://service-learning-3bb28-default-rtdb.asia-southeast1.firebasedatabase.app"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, & Wire, OLED_RESET);

uint32_t tsLastReport = 0;
DHT dht(DHT11PIN, DHT11);

//Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJsonArray arr;

MAX30105 particleSensor;

uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100]; //red LED sensor data
int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid
byte pulseLED = 11; //Must be on PWM pin
byte readLED = 13; //Blinks with each data read 
byte yes_led = 12;
byte no_led = 14;

float temperature, humidity, BPM, SpO2, bodytemperature, humi, temp;

/*Put your SSID & Password*/
const char * ssid = "RPMS"; // Enter SSID here
const char * password = "12345678"; //Enter Password here             

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT); //buzzer
  Serial.println("Connecting to ");
  Serial.println(ssid);

  //connect to your local wi-fi network
  WiFi.begin(ssid, password);
  dht.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  configTime(0, 3600, ntpServer);

  display.clearDisplay();
  display.setCursor(25, 15);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.println("RPMS");
  display.setCursor(25, 35);
  display.setTextSize(1);
  display.print("Initializing");
  display.display();
  digitalWrite(2, HIGH);
  delay(1000);
  digitalWrite(2, LOW);
  delay(1000);

  pinMode(yes_led, OUTPUT);
  pinMode(no_led, OUTPUT);

  //check wi-fi is connected to wi-fi network
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    display.clearDisplay();
    display.setCursor(25, 15);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.println("WIFI");
    display.setCursor(25, 35);
    display.setTextSize(1);
    display.print("Connecting...");
    display.display();
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  display.clearDisplay();
  display.setCursor(25, 15);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.println("WIFI");
  display.setCursor(25, 35);
  display.setTextSize(1);
  display.print("Connected!");
  display.display();
  Serial.println();
  digitalWrite(2, HIGH);
  delay(250);
  digitalWrite(2, LOW);
  delay(250);
  digitalWrite(2, HIGH);
  delay(250);
  digitalWrite(2, LOW);
  delay(500);

  config.api_key = API_KEY;
  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  pinMode(pulseLED, OUTPUT);
  pinMode(readLED, OUTPUT);

  // Initialize max30102 sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println(F("MAX30102 was not found. Please check wiring/power."));

  }

  /* Sign up */
  if (Firebase.signUp( & config, & auth, "", "")) {
    Serial.println("ok");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }
  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  Firebase.begin( & config, & auth);
  Firebase.reconnectWiFi(true);

  byte ledBrightness = 60; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  particleSensor.enableDIETEMPRDY();
}
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setup();
  }
  //  String uploadTime = "patients/UVwM6K7LKFd6gBcRWICCAzA3dOH2/values/"+printLocalTime();
  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  int counter = 5;
  bool cont = false;
  while (cont == false) {
    digitalWrite(no_led, HIGH);
    digitalWrite(yes_led, LOW);
    display.clearDisplay();
    display.invertDisplay(false);
    display.setTextSize(1.9);
    display.setTextColor(WHITE);
    display.setCursor(30, 10);
    display.clearDisplay();
    display.println("Hold on the");
    display.setCursor(25, 25);
    display.println("Sensor Within");
    display.setCursor(60, 40);
    display.setTextSize(2);
    display.println(counter);
    display.display();
    digitalWrite(2, HIGH);
    delay(1000);
    digitalWrite(2, LOW);
    delay(200);
    counter = counter - 1;
    if (counter < 0) {
      cont = true;
    }
  }

  //read the first 100 samples, and determine the signal range
  for (byte i = 0; i < bufferLength; i++) {
    digitalWrite(no_led, LOW);
    digitalWrite(yes_led, HIGH);
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);
    display.clearDisplay();
    display.invertDisplay(false);
    display.setTextSize(1.9);
    display.setTextColor(WHITE);
    display.setCursor(34, 20);
    display.clearDisplay();
    display.println("Wait for");
    display.setCursor(20, 35);
    display.println("few seconds...");
    display.display();
    if (i % 20 == 0) {
      digitalWrite(2, HIGH);
      delay(100);
      digitalWrite(2, LOW);
    }
  }

  //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)

  //Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second

  while (1) 
  {
    digitalWrite(no_led, LOW);
      digitalWrite(yes_led, LOW);
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, & spo2, & validSPO2, & heartRate, & validHeartRate);
    long irValue = particleSensor.getIR();
    //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
    if (irValue > 7000) 
    {
      digitalWrite(no_led, LOW);
      digitalWrite(yes_led, HIGH);
      for (byte i = 25; i < 100; i++) 
      {
        redBuffer[i - 25] = redBuffer[i];
        irBuffer[i - 25] = irBuffer[i];
      }

      //take 25 sets of samples before calculating the heart rate.
      for (byte i = 95; i < 100; i++) 
      {
        while (particleSensor.available() == false) //do we have new data?
          particleSensor.check(); //Check the sensor for new data

        digitalWrite(readLED, !digitalRead(readLED)); //Blink onboard LED with every data read

        redBuffer[i] = particleSensor.getRed();
        irBuffer[i] = particleSensor.getIR();
        particleSensor.nextSample(); //We're finished with this sample so move to next sample

        //send samples and calculation result to terminal program through UART
        Serial.print(F("red="));
        Serial.print(redBuffer[i], DEC);
        Serial.print(F(", ir="));
        Serial.print(irBuffer[i], DEC);

        Serial.print(F(", HR="));
        Serial.print(heartRate, DEC);

        Serial.print(F(", HRvalid="));
        Serial.print(validHeartRate, DEC);

        Serial.print(F(", SPO2="));
        Serial.print(spo2, DEC);

        Serial.print(F(", SPO2Valid="));
        Serial.println(validSPO2, DEC);
        bodytemperature = particleSensor.readTemperatureF();
        humi = dht.readHumidity();
        temp = dht.readTemperature();
        display.clearDisplay();
        display.invertDisplay(true);
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(5, 5);
        display.println("Body Temp : ");
        display.setCursor(80, 5);
        display.print(bodytemperature);
        display.setCursor(5, 15);
        display.println("Spo2 : ");
        display.setCursor(80, 15);
        if (spo2 < 0) {
          display.print("INVALID");
        } else {
          display.print(spo2);
        }
        display.setCursor(5, 25);
        display.println("BPM : ");
        display.setCursor(80, 25);
        if (heartRate < 0) {
          display.print("INVALID");
        } else {
          display.print(heartRate);
        }
        display.setCursor(5, 35);
        display.println("Atmos Temp : ");
        display.setCursor(80, 35);
        display.print(temp);
        display.setCursor(5, 45);
        display.println("Atmos Humi : ");
        display.setCursor(80, 45);
        display.print(humi);
        display.display();
      }
      if (validSPO2 && validHeartRate && spo2 > 85 && heartRate < 120) {
        Serial.println("Break!!!!!!!!!!!!!!!!!!!!!!!!!!");
        break;
      }
      maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, & spo2, & validSPO2, & heartRate, & validHeartRate);
    } else {
      digitalWrite(no_led, HIGH);
      digitalWrite(yes_led, LOW);
      Serial.println("PLACE YOUR FINGER!");
      display.clearDisplay();
      display.invertDisplay(true);
      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(5, 15);
      display.clearDisplay();
      display.println("Place your");
      display.setCursor(25, 35);
      display.print("Finger!");
      display.display();
    }
    //After gathering 25 new samples recalculate HR and SP02
  }

  Serial.println("_______________________________________");
  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    Serial.print("BPM: ");
    Serial.println(heartRate);

    Serial.print("SpO2: ");
    Serial.print(spo2);
    Serial.println("%");

    Serial.print("Body Temperature: ");
    Serial.print(bodytemperature);
    Serial.println("°F");

    Serial.print("Atmospheric Temperature: ");
    Serial.print(temp);
    Serial.println("°C");

    Serial.print("Atmospheric Humidity: ");
    Serial.print(humi);
    Serial.println("%");

    if (Firebase.ready() && signupOK) {
      sendDataPrevMillis = millis();
      display.clearDisplay();
      display.invertDisplay(false);
      display.setTextSize(1.9);
      display.setTextColor(WHITE);
      display.setCursor(25, 25);
      display.clearDisplay();
      display.println("Uploading...");
      display.display();
      arr.add(heartRate);
      arr.add(spo2);
      arr.add(bodytemperature);
      arr.add(temp);
      arr.add(humi);
      digitalWrite(no_led, LOW);
      digitalWrite(yes_led, HIGH);
      delay(1000);
      digitalWrite(no_led, LOW);
      digitalWrite(yes_led, LOW);
      delay(1000);

      struct tm time;
   
      if(!getLocalTime(&time)){
         Serial.println("Could not obtain time info");
      }
      Serial.print(time.tm_mday);
      Serial.print("-");
      Serial.print(time.tm_mon+1);
      Serial.print("-");
      Serial.println(time.tm_year+1900);

      if (Firebase.setArray(fbdo, "/patients/UVwM6K7LKFd6gBcRWICCAzA3dOH2/values/" + String(time.tm_mday), arr)){
        Serial.println("PASSED");
        Serial.println("PATH: " + fbdo.dataPath());
        Serial.println("TYPE: " + fbdo.dataType());
      } else {
        Serial.println("FAILED");
        Serial.println("REASON: " + fbdo.errorReason());
        display.clearDisplay();
        digitalWrite(2, HIGH);
        delay(2000);
        digitalWrite(2, LOW);
        delay(100);
        while (1) {
          display.invertDisplay(false);
          display.setTextSize(1.9);
          display.setTextColor(WHITE);
          display.setCursor(30, 25);
          display.clearDisplay();
          display.println("Check your");
          display.setCursor(5, 40);
          display.println("Network Connection:(");
          display.display();
          digitalWrite(no_led, HIGH);
          digitalWrite(yes_led, LOW);
          delay(1000);
          digitalWrite(no_led, LOW);
          digitalWrite(yes_led, LOW);
          delay(1000);
        }
      }
    } else {
      digitalWrite(2, HIGH);
      delay(2000);
      digitalWrite(2, LOW);
      delay(100);
      while (1) {
        display.clearDisplay();
        display.invertDisplay(false);
        display.setTextSize(1.9);
        display.setTextColor(WHITE);
        display.setCursor(30, 25);
        display.clearDisplay();
        display.println("Check your");
        display.setCursor(5, 40);
        display.println("Network Connection:(");
        display.display();
        digitalWrite(no_led, HIGH);
        digitalWrite(yes_led, LOW);
        delay(1000);
        digitalWrite(no_led, LOW);
        digitalWrite(yes_led, LOW);
        delay(1000);
      }
    }
    digitalWrite(2, HIGH);
    delay(250);
    digitalWrite(2, LOW);
    delay(250);
    digitalWrite(2, HIGH);
    delay(250);
    digitalWrite(2, LOW);
    while (1) {
      display.clearDisplay();
      display.invertDisplay(false);
      display.setTextSize(1.9);
      display.setTextColor(WHITE);
      display.setCursor(18, 25);
      display.clearDisplay();
      display.println("Done Uploading :)");
      display.display();
      digitalWrite(no_led, LOW);
      digitalWrite(yes_led, HIGH);
      delay(1000);
      digitalWrite(no_led, LOW);
      digitalWrite(yes_led, LOW);
      delay(1000);
    }
    tsLastReport = millis();
  }
}
