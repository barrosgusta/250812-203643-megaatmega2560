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
String classifyStatus(int value);
bool determineLedState(int ldrValue);

// ============================================================================
// FUNÇÕES MQTT
// ============================================================================

void setupTopics()
{
	int written = snprintf(TOPIC_BASE, sizeof(TOPIC_BASE),
						   "iot/%s/%s/%s/cell/%d/device/%s",
						   CAMPUS, CURSO, TURMA, CELL_ID, DEVICE_ID);

	// Verifica overflow
	if (written >= (int)sizeof(TOPIC_BASE))
	{
		DEBUG_ERRORLN(F("ERRO: TOPIC_BASE overflow! Aumente o buffer."));
		return;
	}

	snprintf(TOPIC_STATE, sizeof(TOPIC_STATE), "%s/state", TOPIC_BASE);
	snprintf(TOPIC_TELEMETRY, sizeof(TOPIC_TELEMETRY), "%s/telemetry", TOPIC_BASE);
	snprintf(TOPIC_EVENT, sizeof(TOPIC_EVENT), "%s/event", TOPIC_BASE);
	snprintf(TOPIC_CMD, sizeof(TOPIC_CMD), "%s/cmd", TOPIC_BASE);
	snprintf(TOPIC_CONFIG, sizeof(TOPIC_CONFIG), "%s/config", TOPIC_BASE);
	snprintf(TOPIC_LWT, sizeof(TOPIC_LWT), "%s/lwt", TOPIC_BASE);

	DEBUG_INFOLN(F("\n===================================="));
	DEBUG_INFOLN(F("TOPICS MQTT CONFIGURADOS:"));
	DEBUG_INFOLN(F("===================================="));
	DEBUG_INFO(F("BASE: "));
	DEBUG_INFOLN(TOPIC_BASE);
	DEBUG_INFO(F("STATE: "));
	DEBUG_INFOLN(TOPIC_STATE);
	DEBUG_INFO(F("TELEMETRY: "));
	DEBUG_INFOLN(TOPIC_TELEMETRY);
	DEBUG_INFO(F("CMD: "));
	DEBUG_INFOLN(TOPIC_CMD);
	DEBUG_INFO(F("LWT: "));
	DEBUG_INFOLN(TOPIC_LWT);
	DEBUG_INFOLN(F("====================================\n"));
}
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
	DEBUG_INFO(F("[MQTT] Mensagem recebida em: "));
	DEBUG_INFOLN(topic);

	// Converte payload para string (otimizado com reserve)
	String message;
	message.reserve(length + 1);
	for (unsigned int i = 0; i < length; i++)
	{
		message += (char)payload[i];
	}

	DEBUG_VERBOSE(F("Payload: "));
	DEBUG_VERBOSELN(message); // Verifica se é comando
	if (strcmp(topic, TOPIC_CMD) == 0)
	{
		processCommand(message);
	}
}

void processCommand(String command)
{
	StaticJsonDocument<256> doc;
	DeserializationError error = deserializeJson(doc, command);

	if (error)
	{
		DEBUG_ERROR(F("Erro ao parsear JSON: "));
		DEBUG_ERRORLN(error.c_str());
		return;
	}

	// Verifica se campo "cmd" existe
	if (!doc.containsKey("cmd"))
	{
		DEBUG_ERRORLN(F("[CMD] Erro: campo 'cmd' ausente no JSON"));
		return;
	}

	const char *cmd = doc["cmd"];

	// Valida que cmd não é nulo
	if (cmd == nullptr)
	{
		DEBUG_ERRORLN(F("[CMD] Erro: campo 'cmd' é nulo"));
		return;
	}
	if (strcmp(cmd, "get_status") == 0)
	{
		DEBUG_INFOLN(F("[CMD] get_status recebido - publicando status..."));
		telemetryEnabled = true; // Habilita telemetria periódica
		publishTelemetry(true);	 // Força publicação imediata
	}
	else if (strcmp(cmd, "set_thresholds") == 0)
	{
		// Valida thresholds antes de aplicar
		int newDarkCrit = doc["dark_critical"] | thresholds.dark_critical;
		int newDarkAtt = doc["dark_attention"] | thresholds.dark_attention;
		int newLightAtt = doc["light_attention"] | thresholds.light_attention;
		int newLightCrit = doc["light_critical"] | thresholds.light_critical;

		// Validação de ranges
		if (newDarkCrit < 0 || newDarkCrit > 1023 ||
			newDarkAtt < 0 || newDarkAtt > 1023 ||
			newLightAtt < 0 || newLightAtt > 1023 ||
			newLightCrit < 0 || newLightCrit > 1023)
		{
			DEBUG_ERRORLN(F("[CMD] Erro: thresholds fora do range (0-1023)"));
			return;
		}

		// Validação de ordem lógica
		if (newDarkCrit >= newDarkAtt || newDarkAtt >= newLightAtt || newLightAtt >= newLightCrit)
		{
			DEBUG_ERRORLN(F("[CMD] Erro: thresholds devem estar em ordem crescente"));
			DEBUG_ERRORLN(F("  Esperado: dark_critical < dark_attention < light_attention < light_critical"));
			return;
		}

		// Aplica novos valores
		thresholds.dark_critical = newDarkCrit;
		thresholds.dark_attention = newDarkAtt;
		thresholds.light_attention = newLightAtt;
		thresholds.light_critical = newLightCrit;

		DEBUG_INFOLN(F("[CMD] set_thresholds recebido - thresholds atualizados!"));
		DEBUG_INFO(F("  dark_critical: "));
		DEBUG_INFOLN(thresholds.dark_critical);
		DEBUG_INFO(F("  dark_attention: "));
		DEBUG_INFOLN(thresholds.dark_attention);
		DEBUG_INFO(F("  light_attention: "));
		DEBUG_INFOLN(thresholds.light_attention);
		DEBUG_INFO(F("  light_critical: "));
		DEBUG_INFOLN(thresholds.light_critical);

		// Força reclassificação do status atual
		previousStatus = currentStatus;
		currentStatus = classifyStatus(average);

		// Publica confirmação
		publishConfig();
	}
	else
	{
		DEBUG_ERROR(F("[CMD] Comando desconhecido: "));
		DEBUG_ERRORLN(cmd);
	}
}

bool connectMQTT()
{
	DEBUG_INFO(F("Conectando ao MQTT broker "));
	DEBUG_INFO(MQTT_BROKER);
	DEBUG_INFO(F(":"));
	DEBUG_INFO(MQTT_PORT);
	DEBUG_INFO(F("...")); // Cria client ID único com MAC address
	String clientId = "ESP8266-";
	clientId += String(DEVICE_ID);
	clientId += "-";
	clientId += String(random(0xffff), HEX);

	// Prepara Last Will Testament (LWT)
	StaticJsonDocument<128> lwtDoc;
	lwtDoc["status"] = "offline";
	lwtDoc["ts"] = startTime + (millis() / 1000);
	String lwtPayload;
	serializeJson(lwtDoc, lwtPayload);

	// Conecta com LWT
	bool connected = mqttClient.connect(
		clientId.c_str(),
		MQTT_USER,
		MQTT_PASSWORD,
		TOPIC_LWT,
		1,	  // QoS
		true, // retain
		lwtPayload.c_str());

	if (connected)
	{
		DEBUG_INFOLN(F(" ✓ Conectado!"));

		// Subscreve aos tópicos de comando
		mqttClient.subscribe(TOPIC_CMD, 1);
		DEBUG_INFO(F("✓ Subscrito a: "));
		DEBUG_INFOLN(TOPIC_CMD);

		// Publica estado online
		StaticJsonDocument<256> onlineDoc;
		onlineDoc["status"] = "online";
		onlineDoc["ts"] = startTime + (millis() / 1000);
		onlineDoc["ip"] = WiFi.localIP().toString();
		onlineDoc["rssi"] = WiFi.RSSI();

		String onlinePayload;
		serializeJson(onlineDoc, onlinePayload);
		mqttClient.publish(TOPIC_STATE, onlinePayload.c_str(), true);

		return true;
	}
	else
	{
		DEBUG_ERROR(F(" ✗ Falha, rc="));
		DEBUG_ERRORLN(mqttClient.state());
		return false;
	}
}

// ============================================================================
// FUNÇÕES WiFi
// ============================================================================

void connectWiFi()
{
	DEBUG_INFOLN(F("\n===================================="));
	DEBUG_INFOLN(F("CONECTANDO AO WiFi..."));
	DEBUG_INFOLN(F("===================================="));
	DEBUG_INFO(F("SSID: "));
	DEBUG_INFOLN(WIFI_SSID);
	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	int attempts = 0;
	while (WiFi.status() != WL_CONNECTED && attempts < 30)
	{
		delay(500);
		DEBUG_VERBOSE(F("."));
		attempts++;
	}

	if (WiFi.status() == WL_CONNECTED)
	{
		DEBUG_INFOLN(F("\n✓ WiFi conectado!"));
		DEBUG_INFO(F("IP: "));
		DEBUG_INFOLN(WiFi.localIP());
		DEBUG_INFO(F("RSSI: "));
		DEBUG_INFO(WiFi.RSSI());
		DEBUG_INFOLN(F(" dBm"));
	}
	else
	{
		DEBUG_ERRORLN(F("\n✗ Falha ao conectar WiFi!"));
	}
} // ============================================================================
// SENSOR E CLASSIFICAÇÃO
// ============================================================================

int getMovingAverage()
{
	total = total - readings[readIndex];
	readings[readIndex] = analogRead(LDR_PIN);
	total = total + readings[readIndex];
	readIndex = (readIndex + 1) % SAMPLE_SIZE;
	return total / SAMPLE_SIZE;
}

String classifyStatus(int value)
{
	if (value < thresholds.dark_critical)
	{
		return "critico";
	}
	else if (value < thresholds.dark_attention)
	{
		return "atencao";
	}
	else if (value <= thresholds.light_attention)
	{
		return "normal";
	}
	else if (value <= thresholds.light_critical)
	{
		return "atencao";
	}
	else
	{
		return "critico";
	}
}

bool determineLedState(int ldrValue)
{
	// LED acende APENAS quando está escuro (baixa luminosidade)
	return (ldrValue < thresholds.dark_attention); // LED ON se < 600 (escuro)
}

// ============================================================================
// PUBLICAÇÃO MQTT
// ============================================================================

void publishTelemetry(bool forcePublish = false)
{
	if (!mqttClient.connected())
	{
		DEBUG_ERRORLN(F("[TELEMETRIA] ✗ MQTT desconectado!"));
		return;
	}
	StaticJsonDocument<512> doc;

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
	if (payloadSize > 512)
	{
		DEBUG_ERROR(F("[TELEMETRIA] ⚠️ Payload muito grande: "));
		DEBUG_INFO(payloadSize);
		DEBUG_INFOLN(F(" bytes (max recomendado: 512)"));
	}
	bool published = mqttClient.publish(TOPIC_TELEMETRY, payload.c_str(), false);

	if (published)
	{
		telemetryCount++;
		DEBUG_INFO(F("[TELEMETRIA #"));
		DEBUG_INFO(telemetryCount);
		DEBUG_INFO(F("] Status: "));
		DEBUG_INFO(currentStatus);
		DEBUG_INFO(F(" | LDR: "));
		DEBUG_INFO(average);
		DEBUG_INFO(F(" | RSSI: "));
		DEBUG_INFO(WiFi.RSSI());
		DEBUG_INFO(F(" dBm | Size: "));
		DEBUG_INFO(payloadSize);
		DEBUG_INFOLN(F(" bytes"));

		if (forcePublish)
		{
			DEBUG_VERBOSE(F("Payload: "));
			DEBUG_VERBOSELN(payload);
		}
	}
	else
	{
		DEBUG_ERRORLN(F("[TELEMETRIA] ✗ Falha ao publicar!"));
		DEBUG_ERROR(F("  → MQTT State: "));
		DEBUG_ERRORLN(mqttClient.state());
		DEBUG_ERROR(F("  → WiFi: "));
		DEBUG_ERRORLN(WiFi.status() == WL_CONNECTED ? "OK" : "DESCONECTADO");
		DEBUG_ERROR(F("  → Payload size: "));
		DEBUG_ERROR(payloadSize);
		DEBUG_ERRORLN(F(" bytes"));
		DEBUG_ERROR(F("  → Tópico: "));
		DEBUG_ERRORLN(TOPIC_TELEMETRY);
		DEBUG_ERROR(F("  → Buffer size MQTT: "));
		DEBUG_ERRORLN(mqttClient.getBufferSize());
	}
}
void publishEvent(String eventType, String description)
{
	if (!mqttClient.connected())
	{
		DEBUG_ERRORLN(F("[EVENT] ✗ MQTT desconectado - evento não publicado"));
		return;
	}

	StaticJsonDocument<256> doc;
	doc["ts"] = startTime + (millis() / 1000);
	doc["event"] = eventType;
	doc["description"] = description;
	doc["ldr"] = average;
	doc["status"] = currentStatus;

	String payload;
	serializeJson(doc, payload);

	bool published = mqttClient.publish(TOPIC_EVENT, payload.c_str(), false);

	if (published)
	{
		DEBUG_INFO(F("[EVENT] "));
		DEBUG_INFO(eventType);
		DEBUG_INFO(F(": "));
		DEBUG_INFOLN(description);
	}
	else
	{
		DEBUG_ERROR(F("[EVENT] ✗ Falha ao publicar evento: "));
		DEBUG_ERRORLN(eventType);
	}
}

void publishConfig()
{
	if (!mqttClient.connected())
	{
		DEBUG_ERRORLN(F("[CONFIG] ✗ MQTT desconectado - config não publicada"));
		return;
	}

	StaticJsonDocument<256> doc;
	doc["ts"] = startTime + (millis() / 1000);

	JsonObject thresh = doc["thresholds"].to<JsonObject>();
	thresh["dark_critical"] = thresholds.dark_critical;
	thresh["dark_attention"] = thresholds.dark_attention;
	thresh["light_attention"] = thresholds.light_attention;
	thresh["light_critical"] = thresholds.light_critical;

	String payload;
	serializeJson(doc, payload);

	bool published = mqttClient.publish(TOPIC_CONFIG, payload.c_str(), true); // retained

	if (published)
	{
		DEBUG_INFOLN(F("[CONFIG] ✓ Configuração publicada!"));
	}
	else
	{
		DEBUG_ERRORLN(F("[CONFIG] ✗ Falha ao publicar configuração!"));
	}
}

// ============================================================================
// FUNÇÕES DE CONEXÃO
// ============================================================================

void ensureConnections()
{
	// Verifica WiFi
	if (WiFi.status() != WL_CONNECTED)
	{
		DEBUG_ERRORLN(F("\n WiFi desconectado! Reconectando..."));
		connectWiFi();
	}

	// Verifica MQTT
	if (!mqttClient.connected())
	{
		unsigned long now = millis();
		if (now - lastReconnectAttempt > RECONNECT_INTERVAL)
		{
			lastReconnectAttempt = now;
			DEBUG_ERRORLN(F("\n MQTT desconectado! Reconectando..."));
			if (connectMQTT())
			{
				lastReconnectAttempt = 0;
			}
		}
	}
}

// ============================================================================
// SETUP
// ============================================================================

void setup()
{
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);

	Serial.begin(115200);
	delay(2000);

	DEBUG_INFOLN(F("\n\n"));
	DEBUG_INFOLN(F("╔════════════════════════════════════════════════════════════╗"));
	DEBUG_INFOLN(F("║  TRABALHO 02 - CONECTIVIDADE MQTT                        ║"));
	DEBUG_INFOLN(F("║  Arduino ESP8266 WiFi - Sistema IoT                       ║"));
	DEBUG_INFOLN(F("╚════════════════════════════════════════════════════════════╝"));

	// Inicializa array de leituras
	for (int i = 0; i < SAMPLE_SIZE; i++)
	{
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
	mqttClient.setBufferSize(512); // Aumenta buffer para suportar payloads maiores

	// Conecta MQTT
	if (WiFi.status() == WL_CONNECTED)
	{
		connectMQTT();
	}

	DEBUG_INFOLN(F("\n✓ Sistema iniciado!"));
	DEBUG_INFOLN(F("------------------------------------------------------------"));
	DEBUG_INFOLN(F("⏸️  Telemetria em espera - envie comando 'get_status' para iniciar"));
	DEBUG_INFOLN(F("Comandos disponíveis via MQTT:"));
	DEBUG_INFOLN(F("  - get_status: Inicia telemetria e força publicação de status"));
	DEBUG_INFOLN(F("  - set_thresholds: Atualiza thresholds"));
	DEBUG_INFOLN(F("------------------------------------------------------------\n"));
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void loop()
{
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
	bool newLedState = determineLedState(average);
	if (newLedState != ledState)
	{
		ledState = newLedState;
		digitalWrite(LED_PIN, ledState ? HIGH : LOW);
	}

	unsigned long now = millis();
	bool shouldPublish = false;

	// Detecta mudança de status primeiro
	if (currentStatus != previousStatus && previousStatus != "")
	{
		DEBUG_INFO(F("\n[STATUS CHANGE] "));
		DEBUG_INFO(previousStatus);
		DEBUG_INFO(F(" → "));
		DEBUG_INFOLN(currentStatus);

		String eventDesc = "Status mudou de " + previousStatus + " para " + currentStatus;
		publishEvent("status_change", eventDesc);

		// Força publicação de telemetria após evento
		shouldPublish = true;
	}

	// Publica a cada 3 segundos (apenas se telemetria estiver habilitada)
	if (now - lastTelemetryTime >= TELEMETRY_INTERVAL)
	{
		shouldPublish = telemetryEnabled;
		lastTelemetryTime = now;
	}

	if (shouldPublish)
	{
		publishTelemetry();
	}

	// Delay reduzido para melhor responsividade
	delay(100);
}
