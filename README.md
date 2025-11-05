# 🌐 Sistema IoT - Monitoramento com MQTT

Sistema de monitoramento de luminosidade com ESP8266, sensor LDR e publicação de dados via protocolo MQTT.

---

## 📋 Visão Geral

Este projeto implementa um **sensor IoT inteligente** que:

1. 🌡️ Monitora luminosidade ambiente via sensor LDR
2. 📊 Classifica o ambiente em 3 níveis: **normal**, **atenção** ou **crítico**
3. 💡 Controla LED automaticamente baseado no status
4. 📡 Publica telemetria via MQTT a cada 3 segundos
5. 🔄 Detecta mudanças e publica eventos on-change
6. 💬 Recebe comandos remotos via MQTT

---

## 🔧 Ambientes Disponíveis

### **ESP8266** (Principal - Sistema IoT Completo)

Sistema completo com WiFi e MQTT para monitoramento remoto.

```bash
# Compilar
pio run -e esp8266

# Upload no ESP8266
pio run -e esp8266 -t upload

# Monitor Serial
pio device monitor -e esp8266
```

**Componentes necessários:**
- 1x **Arduino ESP8266 WiFi** (placa compatível com UNO)
- 1x Arduino Mega 2560 (apenas para programação via USB)
- 1x Sensor LDR (Light Dependent Resistor)
- 1x Resistor 10kΩ (pull-down do LDR)
- 1x LED externo
- 1x Resistor 330Ω (para o LED)
- Jumpers para conexão Mega ↔ ESP8266
- Protoboard
- Cabo USB para programação

**Funcionalidades:**
- ✅ Conexão WiFi automática
- ✅ Publicação MQTT em broker público
- ✅ Telemetria JSON a cada 3 segundos
- ✅ Publicação instantânea ao detectar mudança de status
- ✅ Recebe comandos: `get_status`, `set_thresholds`
- ✅ Last Will Testament (LWT) para detectar desconexão
- ✅ Reconexão automática WiFi e MQTT
- ✅ Média móvel (5 amostras) para estabilidade
- ✅ 3 níveis de classificação com thresholds ajustáveis

---

### **Arduino Mega** (Ambiente de Testes)

Firmware em branco para testes e experimentação.

```bash
# Compilar e upload
pio run -e mega -t upload
```

**Uso:** Template vazio para desenvolver e testar código no Arduino Mega 2560.

---

## 🔌 Diagrama de Conexões

### **Configuração: Arduino Mega (Ponte USB) + Arduino ESP8266 WiFi**

Este projeto utiliza:
- **Arduino Mega 2560**: Apenas como ponte USB-Serial para programação
- **Arduino ESP8266 WiFi**: Gerencia sensores, LED, WiFi e MQTT

⚠️ **IMPORTANTE - PINAGEM INVERTIDA:**
Sua placa **Arduino ESP8266 WiFi** tem pinagem serial **DIRETA** (não cruzada):
- TX0(Mega) conecta em **TXD**(ESP8266) ✅
- RX0(Mega) conecta em **RXD**(ESP8266) ✅

Isso é diferente do padrão, mas está **CORRETO** para sua placa específica!

```
┌─────────────────────────────────────────────────────────────────────┐
│              CONEXÃO PONTE USB (PROGRAMAÇÃO)                        │
└─────────────────────────────────────────────────────────────────────┘

    Computador USB       Arduino Mega 2560        Arduino ESP8266 WiFi
       ┌──────┐           ┌─────────────┐          ┌─────────────────┐
       │      │           │             │          │                 │
       │ USB ─┼───────────┼─► USB       │          │                 │
       │      │           │             │          │                 │
       └──────┘           │ TX0 (Pin 1)─┼──────────┼─► TXD          │
                          │             │          │                 │
                          │ RX0 (Pin 0)─┼──────────┼─► RXD          │
                          │             │          │                 │
                          │ 5V ─────────┼──────────┼─► VIN          │
                          │             │          │                 │
                          │ GND ────────┼──────────┼─► GND          │
                          │             │          │                 │
                          └─────────────┘          └─────────────────┘
                           (Ponte USB)                     │
                                                            │
                                                      ┌─────┴──────┐
                                                      │  Sensores  │
                                                      │  LDR  LED  │
                                                      │  A0   D13  │
                                                      └────────────┘

⚠️  CONFIGURAÇÃO ESPECÍFICA PARA "Arduino ESP8266 WiFi":
   • TX0 → TXD (DIRETO, não cruzado) ← Sua placa é assim!
   • RX0 → RXD (DIRETO, não cruzado) ← Diferente do padrão!
   • Arduino Mega apenas faz ponte USB-Serial
   • Sensores conectam DIRETO no Arduino ESP8266 WiFi


┌─────────────────────────────────────────────────────────────────────┐
│              SENSORES NO ARDUINO ESP8266 WIFI                       │
└─────────────────────────────────────────────────────────────────────┘

┌──────────────────────── CIRCUITO LDR ────────────────────────┐
│                                                               │
│   5V ──────┬─────[ LDR ]─────┬───────● A0 (ESP8266 WiFi)     │
│            │                 │                                │
│            │              [ 10kΩ ]                            │
│            │                 │                                │
│            │                 └───────● GND                    │
│            │                                                  │
│       (Alimentação)       (Pull-down)                         │
│                                                               │
│  Funcionamento:                                               │
│  - Muita luz  → LDR ~1kΩ   → V_A0 alta → ADC ~800-1023      │
│  - Pouca luz  → LDR ~100kΩ → V_A0 baixa → ADC ~0-200        │
│                                                               │
│  ⚠️  Placa compatível com Arduino UNO (mesmo ADC 10-bit)    │
│                                                               │
└───────────────────────────────────────────────────────────────┘


┌──────────────────────── CIRCUITO LED ─────────────────────────┐
│                                                               │
│   D13 ───[ 330Ω ]────┬───● LED (+) Anodo                     │
│   (ESP8266)          │                                        │
│                      └───● LED (-) Catodo ─── GND            │
│                                                               │
│  Nota: D13 é o LED_BUILTIN do Arduino ESP8266 WiFi          │
│        Pode usar LED externo ou o integrado na placa         │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

### **Tabela de Conexões Completa**

#### **Ponte USB-Serial (Programação apenas)**

| Computador | Arduino Mega | Arduino ESP8266 WiFi | Observação |
|------------|--------------|----------------------|------------|
| USB | USB | - | Para programar/monitorar |
| - | **TX0** (Pin 1) | **TXD** | ⚠️ DIRETO (não cruzado!) |
| - | **RX0** (Pin 0) | **RXD** | ⚠️ DIRETO (não cruzado!) |
| - | **5V** | **VIN** | Alimentação 5V |
| - | **GND** | **GND** | Terra comum |

#### **Sensores e Atuadores (Arduino ESP8266 WiFi)**

| Componente | Pino ESP8266 | Via | Destino | Observações |
|------------|--------------|-----|---------|-------------|
| **LDR** (terminal 1) | - | - | 5V | Alimentação positiva |
| **LDR** (terminal 2) | A0 | Direto | A0 | Entrada analógica (0-1023) |
| **Resistor 10kΩ** (terminal 1) | A0 | - | A0 | Pull-down |
| **Resistor 10kΩ** (terminal 2) | GND | - | GND | Terra |
| **LED** (anodo +) | D2 | Via R330Ω | D2 |  externo |
| **LED** (catodo -) | GND | Direto | GND | Terra |

#### **Pinagem Arduino ESP8266 WiFi (Compatível com UNO)**

| Pino | Tipo | Função no Projeto | Observação |
|------|------|-------------------|------------|
| **A0** | Analógico | Sensor LDR | ADC 10-bit (0-1023) |
| **D13** | Digital | LED (builtin) | LED_BUILTIN |
| **TXD** | Serial | Recebe do Mega | ⚠️ Pinagem invertida |
| **RXD** | Serial | Envia ao Mega | ⚠️ Pinagem invertida |
| **VIN** | Power | Alimentação 5V | Do Arduino Mega |
| **3V3** | Power | Saída 3.3V | Regulador interno |
| **GND** | Ground | Terra | Comum com Mega |
| **D0-D12** | Digital | Livres | Expansão futura |
| **WiFi** | Interno | MQTT/WiFi | ESP8266 integrado |

### **Arquitetura do Sistema**

```
┌────────────────────────────────────────────────────────────────┐
│                       FLUXO DE DADOS                           │
└────────────────────────────────────────────────────────────────┘

   Sensor LDR          Arduino ESP8266 WiFi         Broker MQTT
       │                      │                          │
       ├──► ADC (A0) ────────┤                          │
       │                  ┌───┴───┐                      │
       │                  │ Lê    │                      │
       │                  │ LDR   │                      │
       │                  │       │                      │
   LED │◄─── D13 ◄────────┤ Controla                    │
       │                  │ LED   │                      │
       │                  │       │                      │
       │                  │ WiFi  ├──────► Publica ──────┤
       │                  │ MQTT  │       Telemetria     │
       │                  │       │◄────── Comandos ─────┤
       │                  └───────┘                      │
       │                  │       │◄──── RX ◄── TX ─┤WiFi │
       │                  └───────┘                 │MQTT │
       │                                            └─────┘
       │                                               │
       │                                               ▼
       │                                        [Broker MQTT]
       │                                        test.mosquitto.org
       │                                               │
       │                                               ▼
       │                                        [Cliente MQTT]
       │                                        (Monitor remoto)
```

### **Pinos Utilizados**

#### **Arduino Mega 2560**

```
┌─────────────────────────────────────────────────┐
│  Pino    │  Tipo   │   Função                  │
├──────────┼─────────┼───────────────────────────┤
│  A0      │ Analog  │ Sensor LDR (0-1023)       │
│  D22     │ Digital │ LED indicador             │
│  TX0 (1) │ Serial  │ Envia para ESP8266        │
│  RX0 (0) │ Serial  │ Recebe do ESP8266         │
│  5V      │ Power   │ Alimentação ESP8266       │
│  GND     │ Ground  │ Terra comum               │
└─────────────────────────────────────────────────┘
```

#### **ESP8266 NodeMCU**

```
┌─────────────────────────────────────────────────┐
│  Pino    │  Tipo   │   Função                  │
├──────────┼─────────┼───────────────────────────┤
│  TX (TXD)│ Serial  │ Envia para Arduino        │
│  RX (RXD)│ Serial  │ Recebe do Arduino         │
│  VIN     │ Power   │ Recebe 5V do Arduino      │
│  GND     │ Ground  │ Terra comum               │
│  WiFi    │ Interno │ Conecta rede 2.4GHz       │
└─────────────────────────────────────────────────┘
```

### **Valores Típicos do LDR (Arduino ESP8266 WiFi)**

| Condição | Resistência LDR | Tensão A0 | ADC (0-1023) | Status |
|----------|-----------------|-----------|--------------|--------|
| Escuro total | ~100kΩ | ~0.3V | 0-200 | **Crítico** |
| Penumbra | ~10kΩ | ~1.0V | 200-400 | **Atenção** |
| Normal | ~5kΩ | ~1.5V | 400-600 | **Normal** |
| Iluminado | ~2kΩ | ~2.5V | 600-800 | **Atenção** |
| Muito claro | ~1kΩ | ~3.0V | 800-1023 | **Crítico** |

⚠️ **Nota:** Arduino ESP8266 WiFi usa ADC de 10-bit (0-1023) compatível com Arduino UNO.

---

## ⚙️ Configuração do Sistema

### **1. Editar Credenciais WiFi e MQTT**

Abra `src/main_esp8266_mqtt.cpp` e configure:

```cpp
// ============ ALTERE AQUI ============

// WiFi
const char* WIFI_SSID = "SuaRede";          // Nome da rede WiFi
const char* WIFI_PASSWORD = "SuaSenha";     // Senha do WiFi

// MQTT Broker
const char* MQTT_BROKER = "test.mosquitto.org";  // Broker público
const int MQTT_PORT = 1883;                      // Porta padrão

// =====================================
```

### **2. Ajustar Thresholds (Opcional)**

Configure os limites de classificação conforme seu ambiente:

```cpp
Thresholds thresholds = {
  200,   // dark_critical   (< 200 = muito escuro)
  400,   // dark_attention  (200-400 = escuro)
  600,   // light_attention (400-600 = normal)
  800    // light_critical  (> 800 = muito claro)
};
```

---

## 📡 Protocolo MQTT

### **Estrutura de Tópicos**

```
iot/{campus}/{curso}/{turma}/cell/{cellId}/device/{devId}/
│
├── state       ← Status online/offline (retained)
├── telemetry   ← Dados do sensor (QoS 1, a cada 3s)
├── event       ← Eventos de mudança (on-change)
├── cmd         ← Comandos recebidos (subscribe)
├── config      ← Configuração atual (retained)
└── lwt         ← Last Will Testament (retained)
```

**Exemplo de tópico completo:**
```
iot/riodosul/si/BSN22025T26F8/cell/4/device/c4-gustavo-daniel/telemetry
```

### **Payload de Telemetria (JSON)**

```json
{
  "ts": 1234567890,
  "cellId": 4,
  "devId": "c4-gustavo-daniel",
  "metrics": {
    "ldr": 512,
    "led_state": false,
    "rssi": -45,
    "uptime": 120
  },
  "status": "normal",
  "units": {
    "ldr": "ADC",
    "led_state": "boolean",
    "rssi": "dBm",
    "uptime": "seconds"
  },
  "thresholds": {
    "dark_critical": 200,
    "dark_attention": 400,
    "light_attention": 600,
    "light_critical": 800
  }
}
```

### **Comandos Disponíveis**

#### **1. Obter Status Atual**

Publique no tópico `iot/.../cmd`:
```json
{"cmd": "get_status"}
```

Resposta: Publicação imediata de telemetria

#### **2. Atualizar Thresholds**

Publique no tópico `iot/.../cmd`:
```json
{
  "cmd": "set_thresholds",
  "dark_critical": 150,
  "dark_attention": 350,
  "light_attention": 650,
  "light_critical": 850
}
```

Resposta: Publicação no tópico `config` com novos valores

---

## 🧪 Teste do Sistema

### **1. Compilar e Fazer Upload**

### **1. Compilar e Fazer Upload**

```bash
# Conecte Arduino Mega via USB (com Arduino ESP8266 WiFi conectado)
# O Mega serve apenas como ponte USB-Serial
pio run -e esp8266 -t upload
```

⚠️ **Configuração física necessária:**
- Arduino Mega conectado ao PC via USB
- Arduino ESP8266 WiFi conectado ao Mega:
  - TX0(Mega) → TXD(ESP8266)
  - RX0(Mega) → RXD(ESP8266)
  - 5V(Mega) → VIN(ESP8266)
  - GND(Mega) → GND(ESP8266)
- Sensores (LDR e LED) conectados no Arduino ESP8266 WiFi

### **2. Abrir Monitor Serial**

```bash
pio device monitor -e esp8266
```

**Saída esperada:**
```
╔════════════════════════════════════════════════════════════╗
║  TRABALHO 02 - CONECTIVIDADE MQTT                        ║
║  Arduino ESP8266 WiFi - Sistema IoT                       ║
╚════════════════════════════════════════════════════════════╝

====================================
CONECTANDO AO WiFi...
====================================
SSID: SuaRede
..........
✓ WiFi conectado!
IP: 192.168.1.100
RSSI: -45 dBm

Conectando ao MQTT broker test.mosquitto.org:1883... ✓ Conectado!
✓ Subscrito a: iot/riodosul/si/BSN22025T26F8/cell/4/device/c4-gustavo-daniel/cmd

[TELEMETRIA #1] Status: normal | LDR: 512 | RSSI: -45 dBm | Size: 302 bytes
[TELEMETRIA #2] Status: normal | LDR: 520 | RSSI: -45 dBm | Size: 302 bytes

[STATUS CHANGE] normal → atencao
[EVENT] status_change: Status mudou de normal para atencao
[TELEMETRIA #3] Status: atencao | LDR: 720 | RSSI: -46 dBm | Size: 302 bytes
```

### **3. Testar com MQTT Explorer**

1. Baixe: https://mqtt-explorer.com/
2. Conecte ao broker: `test.mosquitto.org:1883`
3. Navegue até: `iot/riodosul/si/BSN22025T26F8/cell/4/device/c4-gustavo-daniel/`
4. Veja mensagens em tempo real nos tópicos
5. Envie comandos pelo tópico `cmd`

### **4. Testes Funcionais**

| Teste | Ação | Resultado Esperado |
|-------|------|-------------------|
| **WiFi** | Ligar placa | Conexão automática ao WiFi |
| **MQTT** | Aguardar 5s | Conexão ao broker |
| **Telemetria** | Aguardar 3s | Publicação periódica (302 bytes) |
| **Sensor** | Cobrir LDR | LDR < 200, status "critico" |
| **LED D2** | Cobrir LDR | LED acende (atenção/crítico) |
| **On-change** | Variar luz | Publicação imediata ao mudar |
| **Comando** | Enviar `get_status` | Publicação instantânea |
| **Reconexão** | Reiniciar broker | Reconecta automaticamente |

---

## 🛠️ Troubleshooting

### **Não consegue fazer upload**
- ✅ Arduino Mega conectado via USB ao computador
- ✅ Arduino ESP8266 WiFi conectado ao Mega (TX0→TXD, RX0→RXD, 5V, GND)
- ✅ Verifique porta COM no Device Manager (Windows) ou `ls /dev/tty*` (Mac/Linux)
- ✅ Sua placa tem pinagem DIRETA (TX→TX, RX→RX) - diferente do padrão!
- ✅ Tente segurar botão RESET do ESP8266 durante início do upload

### **WiFi não conecta**
- ✅ Verifique SSID e senha no código (`src/main_esp8266_mqtt.cpp`)
- ✅ ESP8266 só suporta WiFi 2.4GHz (não funciona em 5GHz)
- ✅ Verifique se a rede está disponível
- ✅ Algumas redes corporativas bloqueiam ESP8266

### **MQTT não conecta**
- ✅ Broker `test.mosquitto.org` está online?
- ✅ Firewall bloqueando porta 1883?
- ✅ Tente outro broker: `broker.hivemq.com`
- ✅ Verifique serial monitor para código de erro `rc=X`:
  - `rc=-4`: Timeout de conexão
  - `rc=-2`: Falha de rede
  - `rc=2`: Identificador duplicado
  - `rc=5`: Não autorizado

### **Falha ao publicar MQTT (`✗ Falha ao publicar!`)**
- ✅ Payload muito grande? Buffer configurado para 512 bytes
- ✅ Payload típico: ~302 bytes (dentro do limite)
- ✅ Verifique tamanho do JSON no Serial Monitor
- ✅ Se continuar falhando, aumente buffer em `setup()`: `mqttClient.setBufferSize(768);`

### **LDR sempre retorna 0 ou 1023**
- ✅ Verifique resistor pull-down de 10kΩ entre A0 e GND
- ✅ LDR conectado entre 5V e A0
- ✅ Conexões do LDR corretas (polaridade não importa, é resistor)
- ✅ Teste LDR com multímetro (resistência varia com luz?)
- ✅ Troque o LDR se estiver queimado

### **LED D13 não acende**
- ✅ D13 (LED_BUILTIN) tem lógica normal: `HIGH = ligado`, `LOW = apagado`
- ✅ LED deve acender quando status = "atencao" ou "critico"
- ✅ Cubra o LDR completamente para forçar status crítico
- ✅ Se usar LED externo, verifique polaridade (anodo +, catodo -)
- ✅ Resistor de 330Ω presente se usar LED externo

### **Valores oscilando muito**
- ✅ Normal: média móvel suaviza em ~1 segundo (5 amostras)
- ✅ Verifique jumpers soltos ou mal conectados
- ✅ Ambiente com luz oscilante? (lâmpadas fluorescentes, LED PWM)
- ✅ Aumente janela de média móvel no código (altere `SAMPLE_SIZE`)

### **Upload falha ou caracteres estranhos no Serial Monitor**
- ✅ Sua placa tem pinagem DIRETA (TX→TXD, RX→RXD)
- ✅ Não inverta os cabos! Conecte como está: TX0(Mega)→TXD(ESP8266)
- ✅ Baud rate correto: 115200
- ✅ Tente segurar botão RESET do Arduino ao iniciar upload

---

## 📚 Tecnologias Utilizadas

| Tecnologia | Versão | Descrição |
|------------|--------|-----------|
| **PlatformIO** | Latest | Build system e gerenciador de pacotes |
| **Arduino Framework** | 3.1.2 | Framework para ESP8266 |
| **PubSubClient** | 2.8 | Cliente MQTT para Arduino |
| **ArduinoJson** | 7.4.2 | Parser/gerador JSON |
| **ESP8266WiFi** | 1.0 | Biblioteca WiFi nativa |

---

## 📁 Estrutura do Projeto

```
📦 250812-203643-megaatmega2560/
├── 📂 src/
│   ├── main_esp8266_mqtt.cpp    ← Código principal (ESP8266 + MQTT)
│   └── mega_blank.cpp            ← Template vazio (Arduino Mega)
│
├── 📂 include/                   ← Headers (vazio)
├── 📂 lib/                       ← Bibliotecas customizadas (vazio)
├── 📂 test/                      ← Testes unitários (vazio)
│
├── platformio.ini                ← Configuração dos ambientes
├── README.md                     ← Esta documentação
└── .gitignore                    ← Git ignore
```

---

## 👨‍💻 Autor

**Gustavo Barros**  
📚 Curso: Sistemas de Informação - UNIDAVI  
📌 Trabalho: Conectividade MQTT  
📅 Data: Outubro 2025

---

## 📄 Licença

Este projeto é open-source para fins educacionais.
