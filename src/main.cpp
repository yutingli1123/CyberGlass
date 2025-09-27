#include <Arduino.h>

#define LED_PIN 21  // Built-in LED pin for XIAO ESP32S3

void setup() {
  Serial.begin(115200);         // Initialize serial communication
  pinMode(LED_PIN, OUTPUT);     // Set LED pin as output mode
}

void loop() {
  Serial.println("LED is OFF");
  digitalWrite(LED_PIN, HIGH);  // Turn off LED
  delay(1000);                  // Wait 1 second
  
  Serial.println("LED is ON");
  digitalWrite(LED_PIN, LOW);   // Turn on LED

  delay(1000);                  // Wait 1 second
}