#include <Arduino.h>
#include <LiquidCrystal.h>

// Configurações de pinos e thresholds
static const uint8_t LDR_PIN = A0;      // LDR ligado ao A0
static const uint8_t LED_PIN = 22;      // LED vermelho no pino digital 22
static const int LUX_THRESHOLD = 980;   // Valor ADC para ambiente escuro

// LCD 16x2 em modo 4-bit: RS=7, E=8, D4=9, D5=10, D6=11, D7=12
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  // Inicializa o LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Pronto");
  delay(500);
}

void loop() {
  int ldrValue = analogRead(LDR_PIN);
  bool isDark = (ldrValue > LUX_THRESHOLD);

  // Controle do LED conforme validações
  digitalWrite(LED_PIN, isDark ? HIGH : LOW);

  // Saída na Serial
  Serial.print("LDR: ");
  Serial.print(ldrValue);
  Serial.print(" | Luz: ");
  Serial.println(isDark ? "Ligada" : "Desligada");

  // Saída no LCD 16x2
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LDR:");
  lcd.print(ldrValue);
  lcd.setCursor(0, 1);
  lcd.print("Luz:");
  lcd.print(isDark ? "Ligada" : "Desligada");

  delay(2000);
}
