// ============================================================================
// TRABALHO 02 - CONECTIVIDADE MQTT
// Sistema de Controle de Iluminação com LDR - Arduino ESP8266 WiFi
// ============================================================================
// Hardware:
// - Arduino ESP8266 WiFi (placa compatível com UNO)
// - LDR no pino A5 + resistor 10kΩ
// - LED no pino D2
// 
// Programação via Arduino Mega (ponte USB-Serial):
//   TX0(Mega) → TXD(ESP8266) - Pinagem direta (não cruzada)
//   RX0(Mega) → RXD(ESP8266) - Placa com inversão interna
// ============================================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h" // Configurações WiFi, MQTT e identificação

// ============================================================================
// DEBUG LEVELS
// ============================================================================
// 0 = Nenhum log (produção)
// 1 = Apenas erros
// 2 = Informações importantes (padrão)
// 3 = Modo verbose (todos os logs)
#define DEBUG_LEVEL 3

// Macros para controle de debug
#if DEBUG_LEVEL >= 1
#define DEBUG_ERROR(x) Serial.print(x)
#define DEBUG_ERRORLN(x) Serial.println(x)
#else
#define DEBUG_ERROR(x)
#define DEBUG_ERRORLN(x)
#endif

#if DEBUG_LEVEL >= 2
#define DEBUG_INFO(x) Serial.print(x)
#define DEBUG_INFOLN(x) Serial.println(x)
#else
#define DEBUG_INFO(x)
#define DEBUG_INFOLN(x)
#endif

#if DEBUG_LEVEL >= 3
#define DEBUG_VERBOSE(x) Serial.print(x)
#define DEBUG_VERBOSELN(x) Serial.println(x)
#else
#define DEBUG_VERBOSE(x)
#define DEBUG_VERBOSELN(x)
#endif

// ============================================================================
// CONFIGURAÇÃO DE HARDWARE
// ============================================================================
static const uint8_t LDR_PIN = A0; // Sensor LDR (porta analógica A0 - ADC do ESP8266)
static const uint8_t LED_PIN = D2;

// ============================================================================
// THRESHOLDS - Classificação de Status
// ============================================================================
// ATENÇÃO: LDR invertido - Valores ALTOS = muita luz, BAIXOS = escuro
// Quanto MAIOR o valor, MAIS LUZ tem no ambiente
struct Thresholds
{
	int dark_critical;	 // < 450 = muito escuro (crítico)
	int dark_attention;	 // 450-600 = pouca luz (atenção)
	int light_attention; // 600-800 = muita luz (atenção)
	int light_critical;	 // > 800 = luz excessiva (crítico)
};

Thresholds thresholds = {
	450, // dark_critical: < 450 = muito escuro
	600, // dark_attention: 450-600 = pouca luz
	800, // light_attention: 600-800 = muita luz
	950	 // light_critical: > 950 = luz excessiva
};

// ============================================================================
// TÓPICOS MQTT
// ============================================================================
char TOPIC_BASE[128];
char TOPIC_STATE[150];
char TOPIC_TELEMETRY[150];
char TOPIC_EVENT[150];
char TOPIC_CMD[150];
char TOPIC_CONFIG[150];
char TOPIC_LWT[150];

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

static const uint8_t SAMPLE_SIZE = 5;
int readings[SAMPLE_SIZE];
uint8_t readIndex = 0;
int total = 0;
int average = 0;

bool ledState = false;
String currentStatus = "normal";
String previousStatus = "";

unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL = 3000;  // 3 segundos

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

unsigned long startTime = 0;
unsigned long telemetryCount = 0;

// ============================================================================
// DECLARAÇÕES FORWARD
// ============================================================================
void publishTelemetry(bool forcePublish);
void publishConfig();
void processCommand(String command);

// ============================================================================
// FUNÇÕES MQTT
// ============================================================================

void setupTopics() {
  snprintf(TOPIC_BASE, sizeof(TOPIC_BASE), 
           "iot/%s/%s/%s/cell/%d/device/%s",
           CAMPUS, CURSO, TURMA, CELL_ID, DEVICE_ID);
  
  snprintf(TOPIC_STATE, sizeof(TOPIC_STATE), "%s/state", TOPIC_BASE);
  snprintf(TOPIC_TELEMETRY, sizeof(TOPIC_TELEMETRY), "%s/telemetry", TOPIC_BASE);
  snprintf(TOPIC_EVENT, sizeof(TOPIC_EVENT), "%s/event", TOPIC_BASE);
  snprintf(TOPIC_CMD, sizeof(TOPIC_CMD), "%s/cmd", TOPIC_BASE);
  snprintf(TOPIC_CONFIG, sizeof(TOPIC_CONFIG), "%s/config", TOPIC_BASE);
  snprintf(TOPIC_LWT, sizeof(TOPIC_LWT), "%s/lwt", TOPIC_BASE);
  
  Serial.println(F("\n===================================="));
  Serial.println(F("TOPICS MQTT CONFIGURADOS:"));
  Serial.println(F("===================================="));
  Serial.print(F("BASE: ")); Serial.println(TOPIC_BASE);
  Serial.print(F("STATE: ")); Serial.println(TOPIC_STATE);
  Serial.print(F("TELEMETRY: ")); Serial.println(TOPIC_TELEMETRY);
  Serial.print(F("CMD: ")); Serial.println(TOPIC_CMD);
  Serial.print(F("LWT: ")); Serial.println(TOPIC_LWT);
  Serial.println(F("====================================\n"));
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("[MQTT] Mensagem recebida em: "));
  Serial.println(topic);
  
  // Converte payload para string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print(F("Payload: "));
  Serial.println(message);
  
  // Verifica se é comando
  if (strcmp(topic, TOPIC_CMD) == 0) {
    processCommand(message);
  }
}

void processCommand(String command) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, command);
  
  if (error) {
    Serial.print(F("Erro ao parsear JSON: "));
    Serial.println(error.c_str());
    return;
  }
  
  const char* cmd = doc["cmd"];
  
  if (strcmp(cmd, "get_status") == 0) {
    Serial.println(F("[CMD] get_status recebido - publicando status..."));
    publishTelemetry(true);  // Força publicação imediata
  } 
  else if (strcmp(cmd, "set_thresholds") == 0) {
    if (doc["dark_critical"].is<int>()) {
      thresholds.dark_critical = doc["dark_critical"];
    }
    if (doc["dark_attention"].is<int>()) {
      thresholds.dark_attention = doc["dark_attention"];
    }
    if (doc["light_attention"].is<int>()) {
      thresholds.light_attention = doc["light_attention"];
    }
    if (doc["light_critical"].is<int>()) {
      thresholds.light_critical = doc["light_critical"];
    }
    
    Serial.println(F("[CMD] set_thresholds recebido - thresholds atualizados!"));
    Serial.print(F("  dark_critical: ")); Serial.println(thresholds.dark_critical);
    Serial.print(F("  dark_attention: ")); Serial.println(thresholds.dark_attention);
    Serial.print(F("  light_attention: ")); Serial.println(thresholds.light_attention);
    Serial.print(F("  light_critical: ")); Serial.println(thresholds.light_critical);
    
    // Publica confirmação
    publishConfig();
  }
  else {
    Serial.print(F("[CMD] Comando desconhecido: "));
    Serial.println(cmd);
  }
}

bool connectMQTT() {
  Serial.print(F("Conectando ao MQTT broker "));
  Serial.print(MQTT_BROKER);
  Serial.print(F(":"));
  Serial.print(MQTT_PORT);
  Serial.print(F("..."));
  
  // Cria client ID único com MAC address
  String clientId = "ESP8266-";
  clientId += String(DEVICE_ID);
  clientId += "-";
  clientId += String(random(0xffff), HEX);
  
  // Prepara Last Will Testament (LWT)
  JsonDocument lwtDoc;
  lwtDoc["status"] = "offline";
  lwtDoc["ts"] = millis() / 1000;
  String lwtPayload;
  serializeJson(lwtDoc, lwtPayload);
  
  // Conecta com LWT
  bool connected = mqttClient.connect(
    clientId.c_str(),
    MQTT_USER,
    MQTT_PASSWORD,
    TOPIC_LWT,
    1,  // QoS
    true,  // retain
    lwtPayload.c_str()
  );
  
  if (connected) {
    Serial.println(F(" ✓ Conectado!"));
    
    // Subscreve aos tópicos de comando
    mqttClient.subscribe(TOPIC_CMD, 1);
    Serial.print(F("✓ Subscrito a: "));
    Serial.println(TOPIC_CMD);
    
    // Publica estado online
    JsonDocument onlineDoc;
    onlineDoc["status"] = "online";
    onlineDoc["ts"] = millis() / 1000;
    onlineDoc["ip"] = WiFi.localIP().toString();
    onlineDoc["rssi"] = WiFi.RSSI();
    
    String onlinePayload;
    serializeJson(onlineDoc, onlinePayload);
    mqttClient.publish(TOPIC_STATE, onlinePayload.c_str(), true);
    
    return true;
  } else {
    Serial.print(F(" ✗ Falha, rc="));
    Serial.println(mqttClient.state());
    return false;
  }
}

// ============================================================================
// FUNÇÕES WiFi
// ============================================================================

void connectWiFi() {
  Serial.println(F("\n===================================="));
  Serial.println(F("CONECTANDO AO WiFi..."));
  Serial.println(F("===================================="));
  Serial.print(F("SSID: "));
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n✓ WiFi conectado!"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.println(F("\n✗ Falha ao conectar WiFi!"));
  }
}

// ============================================================================
// SENSOR E CLASSIFICAÇÃO
// ============================================================================

int getMovingAverage() {
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(LDR_PIN);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % SAMPLE_SIZE;
  return total / SAMPLE_SIZE;
}

String classifyStatus(int value) {
  if (value < thresholds.dark_critical) {
    return "critico";
  } else if (value < thresholds.dark_attention) {
    return "atencao";
  } else if (value <= thresholds.light_attention) {
    return "normal";
  } else if (value <= thresholds.light_critical) {
    return "atencao";
  } else {
    return "critico";
  }
}

bool determineLedState(String status, int ldrValue) {
  // LED acende APENAS quando está escuro (baixa luminosidade)
  return (ldrValue < thresholds.dark_attention);  // LED ON se < 600 (escuro)
}

// ============================================================================
// PUBLICAÇÃO MQTT
// ============================================================================

void publishTelemetry(bool forcePublish = false) {
  if (!mqttClient.connected()) {
    Serial.println(F("[TELEMETRIA] ✗ MQTT desconectado!"));
    return;
  }
  
  JsonDocument doc;
  
  unsigned long timestamp = startTime + (millis() / 1000);
  doc["ts"] = timestamp;
  doc["cellId"] = CELL_ID;
  doc["devId"] = DEVICE_ID;
  
  JsonObject metrics = doc["metrics"].to<JsonObject>();
  metrics["ldr"] = average;
  metrics["led_state"] = ledState;
  metrics["rssi"] = WiFi.RSSI();
  metrics["uptime"] = millis() / 1000;
  
  doc["status"] = currentStatus;
  
  JsonObject units = doc["units"].to<JsonObject>();
  units["ldr"] = "ADC";
  units["led_state"] = "boolean";
  units["rssi"] = "dBm";
  units["uptime"] = "seconds";
  
  JsonObject thresh = doc["thresholds"].to<JsonObject>();
  thresh["dark_critical"] = thresholds.dark_critical;
  thresh["dark_attention"] = thresholds.dark_attention;
  thresh["light_attention"] = thresholds.light_attention;
  thresh["light_critical"] = thresholds.light_critical;
  
  String payload;
  serializeJson(doc, payload);
  
  // Diagnóstico de tamanho
  size_t payloadSize = payload.length();
  if (payloadSize > 512) {
    Serial.print(F("[TELEMETRIA] Payload muito grande: "));
    Serial.print(payloadSize);
    Serial.println(F(" bytes (max recomendado: 512)"));
  }
  
  bool published = mqttClient.publish(TOPIC_TELEMETRY, payload.c_str(), false);
  
  if (published) {
    telemetryCount++;
    Serial.print(F("[TELEMETRIA #"));
    Serial.print(telemetryCount);
    Serial.print(F("] Status: "));
    Serial.print(currentStatus);
    Serial.print(F(" | LDR: "));
    Serial.print(average);
    Serial.print(F(" | RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.print(F(" dBm | Size: "));
    Serial.print(payloadSize);
    Serial.println(F(" bytes"));
    
    if (forcePublish) {
      Serial.print(F("Payload: "));
      Serial.println(payload);
    }
  } else {
    Serial.println(F("[TELEMETRIA] ✗ Falha ao publicar!"));
    Serial.print(F("  → MQTT State: "));
    Serial.println(mqttClient.state());
    Serial.print(F("  → WiFi: "));
    Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "DESCONECTADO");
    Serial.print(F("  → Payload size: "));
    Serial.print(payloadSize);
    Serial.println(F(" bytes"));
    Serial.print(F("  → Tópico: "));
    Serial.println(TOPIC_TELEMETRY);
    Serial.print(F("  → Buffer size MQTT: "));
    Serial.println(mqttClient.getBufferSize());
  }
}

void publishEvent(String eventType, String description) {
  if (!mqttClient.connected()) {
    return;
  }
  
  JsonDocument doc;
  doc["ts"] = startTime + (millis() / 1000);
  doc["event"] = eventType;
  doc["description"] = description;
  doc["ldr"] = average;
  doc["status"] = currentStatus;
  
  String payload;
  serializeJson(doc, payload);
  
  mqttClient.publish(TOPIC_EVENT, payload.c_str(), false);
  
  Serial.print(F("[EVENT] "));
  Serial.print(eventType);
  Serial.print(F(": "));
  Serial.println(description);
}

void publishConfig() {
  if (!mqttClient.connected()) {
    return;
  }
  
  JsonDocument doc;
  doc["ts"] = startTime + (millis() / 1000);
  
  JsonObject thresh = doc["thresholds"].to<JsonObject>();
  thresh["dark_critical"] = thresholds.dark_critical;
  thresh["dark_attention"] = thresholds.dark_attention;
  thresh["light_attention"] = thresholds.light_attention;
  thresh["light_critical"] = thresholds.light_critical;
  
  String payload;
  serializeJson(doc, payload);
  
  mqttClient.publish(TOPIC_CONFIG, payload.c_str(), true);  // retained
  
  Serial.println(F("[CONFIG] Configuração publicada!"));
}

// ============================================================================
// FUNÇÕES DE CONEXÃO
// ============================================================================

void ensureConnections() {
  // Verifica WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n WiFi desconectado! Reconectando..."));
    connectWiFi();
  }
  
  // Verifica MQTT
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      Serial.println(F("\n MQTT desconectado! Reconectando..."));
      if (connectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  }
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.begin(115200);
  delay(2000);
  
  Serial.println(F("\n\n"));
  Serial.println(F("╔════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║  TRABALHO 02 - CONECTIVIDADE MQTT                        ║"));
  Serial.println(F("║  Arduino ESP8266 WiFi - Sistema IoT                       ║"));
  Serial.println(F("╚════════════════════════════════════════════════════════════╝"));
  
  // Inicializa array de leituras
  for (int i = 0; i < SAMPLE_SIZE; i++) {
    readings[i] = analogRead(LDR_PIN);
    total += readings[i];
    delay(50);
  }
  average = total / SAMPLE_SIZE;
  
  startTime = millis() / 1000;
  
  setupTopics();
  
  // Conecta WiFi
  connectWiFi();
  
  // Configura MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(30);
  mqttClient.setBufferSize(512);  // Aumenta buffer para suportar payloads maiores
  
  // Conecta MQTT
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
  
  Serial.println(F("\n✓ Sistema iniciado!"));
  Serial.println(F("------------------------------------------------------------"));
  Serial.println(F("Sistema publicará telemetria a cada 3 segundos"));
  Serial.println(F("Comandos disponíveis via MQTT:"));
  Serial.println(F("  - get_status: Força publicação de status"));
  Serial.println(F("  - set_thresholds: Atualiza thresholds"));
  Serial.println(F("------------------------------------------------------------\n"));
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void loop() {
  // Garante conexões ativas
  ensureConnections();
  
  // Processa mensagens MQTT
  mqttClient.loop();
  
  // Lê sensor
  average = getMovingAverage();
  
  // Classifica status
  previousStatus = currentStatus;
  currentStatus = classifyStatus(average);
  
  // Controla LED - acende apenas quando está escuro (sensor de luminosidade)
  bool newLedState = determineLedState(currentStatus, average);
  if (newLedState != ledState) {
    ledState = newLedState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
  
  unsigned long now = millis();
  bool shouldPublish = false;
  
  // Publica a cada 3 segundos
  if (now - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    shouldPublish = true;
    lastTelemetryTime = now;
  }
  
  // Publica on-change
  if (currentStatus != previousStatus && previousStatus != "") {
    shouldPublish = true;
    Serial.print(F("\n[STATUS CHANGE] "));
    Serial.print(previousStatus);
    Serial.print(F(" → "));
    Serial.println(currentStatus);
    
    String eventDesc = "Status mudou de " + previousStatus + " para " + currentStatus;
    publishEvent("status_change", eventDesc);
  }
  
  if (shouldPublish) {
    publishTelemetry();
  }
  
  delay(200);
}
