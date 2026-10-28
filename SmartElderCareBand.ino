#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// PIN ASSIGNMENTS
#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22
#define ONE_WIRE_BUS_PIN     4
#define HR_SIM_PIN           33  
#define MEDICINE_BUTTON_PIN  25  
#define SOS_BUTTON_PIN       26  
#define BUZZER_PIN           27  
#define RED_LED_PIN          14  
#define YELLOW_LED_PIN       12  
#define GREEN_LED_PIN        13  

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Adafruit_MPU6050 mpu;
OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature tempSensor(&oneWire);

// Independent State Flags
bool sosActive = false;
bool fallDetected = false;
bool medPending = false;

// Escalation & Countdown Timers
unsigned long sosStartTime = 0;
unsigned long fallStartTime = 0;
unsigned long medStartTime = 0;
const unsigned long ESCALATION_TIMEOUT = 10000UL; 

// Button Debounce Timers
unsigned long lastSosPress = 0;
unsigned long lastMedPress = 0;
const unsigned long DEBOUNCE_DELAY = 250; 

// Temperature Timers
unsigned long lastTempRequest = 0;
float currentTemp = 36.5; 

// Display rotation timer for medicine list
unsigned long lastDisplayRotate = 0;
int displayMedIndex = 0;

// Mock GPS Data Structure
struct GpsData {
  float latitude;
  float longitude;
  bool isValid;
};
GpsData mockGps = {12.9716, 77.5946, true};

// Medicine Schedule Struct
struct MedicineSchedule {
  const char* name;
  const char* dose;
  unsigned long intervalMs;
  unsigned long lastTakenTime;
};

MedicineSchedule meds[10] = {
  {"Blood Pressure", "10mg | MORNING 8:00 AM", 120000UL, 0},
  {"Diabetes Pill", "500mg | NIGHT 8:00 PM", 240000UL, 0}
};
int totalMeds = 2;
int activeMedIndex = 0;

void setup() {
  Serial.begin(115200);

  pinMode(MEDICINE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED initialization failed"));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- STARTUP SPLASH SCREEN ---
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(5, 10);
  display.println(F("SMART"));
  display.setCursor(5, 30);
  display.println(F("ELDER CARE"));
  display.setTextSize(1);
  display.setCursor(5, 50);
  display.println(F("Initializing..."));
  display.display();
  delay(3000); // Show splash screen for 3 seconds

  mpu.begin();
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();

  unsigned long currentMillis = millis();
  for(int i = 0; i < totalMeds; i++) {
    meds[i].lastTakenTime = currentMillis;
  }
  
  Serial.println(F(">> System Initialized. Type ADD:Name,Dose,Slot,Time in Serial"));
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. SERIAL CONFIGURATION PARSER
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("ADD:")) {
      int firstComma = command.indexOf(',');
      int secondComma = command.indexOf(',', firstComma + 1);
      int thirdComma = command.indexOf(',', secondComma + 1);

      if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
        String medName = command.substring(4, firstComma);
        String medDose = command.substring(firstComma + 1, secondComma);
        String slot = command.substring(secondComma + 1, thirdComma);
        String exactTime = command.substring(thirdComma + 1);

        if (totalMeds < 10) {
          meds[totalMeds].name = strdup(medName.c_str());
          String fullDoseInfo = medDose + " | " + slot + " " + exactTime;
          meds[totalMeds].dose = strdup(fullDoseInfo.c_str());
          
          meds[totalMeds].intervalMs = 180000UL; 
          meds[totalMeds].lastTakenTime = currentMillis;
          totalMeds++;

          Serial.print(F(">> [CONFIG] Added Schedule: "));
          Serial.print(medName);
          Serial.print(F(" at "));
          Serial.println(exactTime);
        } else {
          Serial.println(F(">> [ERROR] Medicine list is full!"));
        }
      } else {
        Serial.println(F(">> [ERROR] Invalid format! Use: ADD:Name,Dose,Slot,Time"));
      }
    }
  }

  // 2. NON-BLOCKING TEMPERATURE MONITOR
  if (currentMillis - lastTempRequest >= 2000) {
    float tempRead = tempSensor.getTempCByIndex(0);
    if (tempRead != DEVICE_DISCONNECTED_C && tempRead > -50.0) {
      currentTemp = tempRead;
    }
    tempSensor.requestTemperatures();
    lastTempRequest = currentMillis;
  }

  // 3. SOS BUTTON HANDLING
  if (digitalRead(SOS_BUTTON_PIN) == LOW && (currentMillis - lastSosPress > DEBOUNCE_DELAY)) {
    sosActive = !sosActive;
    lastSosPress = currentMillis;
    if (sosActive) {
      sosStartTime = currentMillis;
      Serial.println(F(">> [EVENT] SOS ACTIVATED! Countdown started."));
    } else {
      Serial.println(F(">> [EVENT] SOS Cancelled by user."));
    }
  }

  // 4. MEDICINE BUTTON HANDLING
  if (digitalRead(MEDICINE_BUTTON_PIN) == LOW && (currentMillis - lastMedPress > DEBOUNCE_DELAY)) {
    if (medPending) {
      medPending = false;
      meds[activeMedIndex].lastTakenTime = currentMillis;
      Serial.print(F(">> [EVENT] Medicine Acknowledged: "));
      Serial.println(meds[activeMedIndex].name);
    } else if (fallDetected) {
      fallDetected = false;
      Serial.println(F(">> [EVENT] Fall Alert Acknowledged & Cleared."));
    }
    lastMedPress = currentMillis;
  }

  // 5. MULTI-MEDICINE SCHEDULER CHECK
  for (int i = 0; i < totalMeds; i++) {
    if (!medPending && (currentMillis - meds[i].lastTakenTime >= meds[i].intervalMs)) {
      medPending = true;
      activeMedIndex = i;
      medStartTime = currentMillis;
      Serial.print(F(">> [EVENT] Medicine Due: "));
      Serial.println(meds[i].name);
      break;
    }
  }

  // 6. HEART RATE MONITOR
  int rawHr = analogRead(HR_SIM_PIN);
  int heartRate = map(rawHr, 0, 4095, 40, 160); 

  // 7. ACTIVITY MONITOR & FALL DETECTION
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float totalAccel = sqrt(a.acceleration.x * a.acceleration.x +
                          a.acceleration.y * a.acceleration.y +
                          a.acceleration.z * a.acceleration.z);
  if (!fallDetected && totalAccel > 25.0) { 
    fallDetected = true;
    fallStartTime = currentMillis;
    Serial.println(F(">> [EVENT] Fall Detected!"));
  }

  // ESCALATION TIMEOUT CHECKERS
  if (sosActive && (currentMillis - sosStartTime >= ESCALATION_TIMEOUT)) {
    sosStartTime = currentMillis; 
    Serial.println(F(">> [ESCALATION] SOS Unanswered! Alerting contacts..."));
    if (mockGps.isValid) {
      Serial.print(F("Map: https://www.google.com/maps?q="));
      Serial.print(mockGps.latitude, 4);
      Serial.print(",");
      Serial.println(mockGps.longitude, 4);
    }
  }

  if (fallDetected && (currentMillis - fallStartTime >= ESCALATION_TIMEOUT)) {
    fallStartTime = currentMillis;
    Serial.println(F(">> [ESCALATION] Fall Unacknowledged! Auto-notifying contacts..."));
  }

  int healthScore = 100;
  if (heartRate > 100 || heartRate < 60) healthScore -= 15;
  if (currentTemp > 37.5 || currentTemp < 35.5) healthScore -= 15;
  if (fallDetected) healthScore -= 30;

  // 8. ALERT EVALUATION
  bool criticalHR = (heartRate > 120 || heartRate < 50);
  bool warningHR  = ((heartRate > 100 && heartRate <= 120) || (heartRate >= 50 && heartRate < 60));
  bool criticalTemp = (currentTemp > 38.5 || currentTemp < 35.0);
  bool warningTemp  = (currentTemp > 37.5 && currentTemp <= 38.5);

  bool isRedAlert = sosActive || fallDetected || medPending || criticalHR || criticalTemp;
  bool isYellowWarning = !isRedAlert && (warningHR || warningTemp);
  bool isNormal = !isRedAlert && !isYellowWarning;

  digitalWrite(RED_LED_PIN, isRedAlert ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, isYellowWarning ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, isNormal ? HIGH : LOW);

  if (sosActive || fallDetected) {
    tone(BUZZER_PIN, 1800);
  } else if (isRedAlert) {
    tone(BUZZER_PIN, 1000);
  } else {
    noTone(BUZZER_PIN);
  }

  // Rotate displayed medicine list every 3 seconds if normal
  if (currentMillis - lastDisplayRotate >= 3000) {
    displayMedIndex = (displayMedIndex + 1) % totalMeds;
    lastDisplayRotate = currentMillis;
  }

  // 9. OLED DISPLAY LOGIC
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (sosActive) {
    display.setTextSize(2);
    display.println(F("!! SOS !!"));
    display.setTextSize(1);
    display.println(F("EMERGENCY ACTIVE"));
    unsigned long remainingSec = (ESCALATION_TIMEOUT - (currentMillis - sosStartTime)) / 1000;
    display.print(F("Escalating in: ")); display.print(remainingSec); display.println(F("s"));
  } else if (fallDetected) {
    display.setTextSize(2);
    display.println(F("FALL!"));
    display.setTextSize(1);
    display.println(F("Check on elder!"));
    display.println(F("Press Med to clear"));
  } else if (medPending) {
    display.setTextSize(2);
    display.println(F("MEDICINE!"));
    display.setTextSize(1);
    display.println(meds[activeMedIndex].name);
    display.print(F("Info: ")); display.println(meds[activeMedIndex].dose);
    display.println(F("Press Med button"));
  } else {
    // Show Vitals & Cycle through the Scheduled Medicines List
    display.print(F("HR: ")); display.print(heartRate); display.print(F(" | T: ")); display.print(currentTemp, 1); display.println(F("C"));
    display.print(F("Score: ")); display.print(healthScore); display.println(F("/100"));
    display.drawLine(0, 20, 128, 20, SSD1306_WHITE);
    
    display.setCursor(0, 24);
    display.println(F("Scheduled Meds:"));
    display.setCursor(0, 36);
    display.println(meds[displayMedIndex].name);
    display.setCursor(0, 48);
    display.println(meds[displayMedIndex].dose);
  }

  display.display();
}