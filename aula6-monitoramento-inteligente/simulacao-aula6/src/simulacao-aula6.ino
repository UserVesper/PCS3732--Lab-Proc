#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// =========================
// CONFIGURACAO DO WIFI
// =========================
#define USE_SOFT_AP false

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* AP_SSID = "Monitoramento_ESP32";
const char* AP_PASSWORD = "";

WebServer server(80);

// Saida serial usada no terminal do Wokwi
#define DEBUG_SERIAL Serial

// =========================
// PINOS
// =========================
const int LDR_PIN = 4;          // GPIO4 - ADC
const int SOS_BUTTON_PIN = 5;   // GPIO5 - botao SOS
const int RGB_LED_PIN = 8;      // GPIO8 - LED RGB built-in do ESP32-C3 no Wokwi

// =========================
// CONFIGURACAO DO LDR / ADC
// =========================
const int ADC_RESOLUTION_BITS = 12;
const int ADC_MAX_VALUE = 4095;

const unsigned long ADC_READ_INTERVAL_MS = 1000;

const int LOW_LIGHT_THRESHOLD = 1500;
const bool LOW_LIGHT_WHEN_ADC_BELOW_THRESHOLD = true;

// =========================
// CONFIGURACAO DO SOS
// =========================
const unsigned long SOS_LED_TIME_MS = 3000;
const unsigned long DEBOUNCE_TIME_US = 200000;

// =========================
// CONFIGURACAO DO LED
// =========================
const unsigned long LOW_LIGHT_BLINK_PERIOD_MS = 2000;
const unsigned long LOW_LIGHT_BLINK_ON_TIME_MS = 250;

// =========================
// VARIAVEIS GLOBAIS
// =========================
volatile bool sosInterruptFlag = false;
volatile unsigned long lastInterruptUs = 0;

unsigned long sosActiveUntilMs = 0;
unsigned long lastAdcReadMs = 0;
unsigned long lastLinkPrintMs = 0;

int ldrRaw = 0;
int ldrPercent = 0;
bool lowLight = false;

unsigned long sosCounter = 0;
String ledStatus = "desligado";

// =========================
// FUNCOES DO LED RGB
// =========================
void setRgbLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_LED_PIN, r, g, b);
}

void ledOff() {
  setRgbLed(0, 0, 0);
}

void ledRed() {
  setRgbLed(255, 0, 0);
}

void ledYellow() {
  setRgbLed(255, 180, 0);
}

// =========================
// INTERRUPCAO DO BOTAO SOS
// =========================
void IRAM_ATTR handleSosInterrupt() {
  unsigned long nowUs = micros();

  if (nowUs - lastInterruptUs >= DEBOUNCE_TIME_US) {
    sosInterruptFlag = true;
    lastInterruptUs = nowUs;
  }
}

// =========================
// LEITURA DO LDR
// =========================
void readLdr() {
  ldrRaw = analogRead(LDR_PIN);

  ldrPercent = map(ldrRaw, 0, ADC_MAX_VALUE, 0, 100);

  if (LOW_LIGHT_WHEN_ADC_BELOW_THRESHOLD) {
    lowLight = ldrRaw < LOW_LIGHT_THRESHOLD;
  } else {
    lowLight = ldrRaw > LOW_LIGHT_THRESHOLD;
  }
}

// =========================
// ESTADO DO SOS
// =========================
bool isSosActive() {
  return millis() < sosActiveUntilMs;
}

// =========================
// ATUALIZACAO DO LED
// =========================
void updateLed() {
  unsigned long now = millis();

  if (isSosActive()) {
    ledRed();
    ledStatus = "vermelho - SOS ativo";
    return;
  }

  if (lowLight) {
    unsigned long phase = now % LOW_LIGHT_BLINK_PERIOD_MS;

    if (phase < LOW_LIGHT_BLINK_ON_TIME_MS) {
      ledYellow();
      ledStatus = "amarelo piscando - baixa luminosidade";
    } else {
      ledOff();
      ledStatus = "desligado entre piscadas - baixa luminosidade";
    }

    return;
  }

  ledOff();
  ledStatus = "desligado - luminosidade normal";
}

// =========================
// IMPRESSAO DOS LINKS
// =========================
void printAccessLinks() {
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println("===== LINKS DE ACESSO =====");

#if USE_SOFT_AP
  DEBUG_SERIAL.print("ESP32 real / hotspot: http://");
  DEBUG_SERIAL.println(WiFi.softAPIP());
#else
  DEBUG_SERIAL.print("IP interno do ESP32 no Wokwi: http://");
  DEBUG_SERIAL.println(WiFi.localIP());

  DEBUG_SERIAL.println("localhost: http://localhost:8180");

  DEBUG_SERIAL.print("Endpoint JSON no Wokwi: http://");
  DEBUG_SERIAL.print(WiFi.localIP());
  DEBUG_SERIAL.println("/dados");

  DEBUG_SERIAL.println("Endpoint JSON localhost: http://localhost:8180/dados");
#endif

  DEBUG_SERIAL.println("===========================");
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.flush();
}

// =========================
// PAGINA WEB
// =========================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <title>Monitoramento LDR e SOS</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">

  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f4f4f4;
      text-align: center;
      margin: 0;
      padding: 30px;
    }

    h1 {
      color: #222;
    }

    .card {
      background: white;
      max-width: 500px;
      margin: 20px auto;
      padding: 25px;
      border-radius: 12px;
      box-shadow: 0 0 10px rgba(0,0,0,0.15);
    }

    .value {
      font-size: 28px;
      font-weight: bold;
      color: #0066cc;
    }

    .status {
      font-size: 20px;
      font-weight: bold;
      margin-top: 10px;
    }

    .normal {
      color: green;
    }

    .alerta {
      color: orange;
    }

    .sos {
      color: red;
    }

    .small {
      color: #555;
      font-size: 14px;
    }
  </style>
</head>

<body>
  <h1>Sistema de Monitoramento Inteligente</h1>

  <div class="card">
    <h2>Sensor LDR</h2>
    <p>Valor ADC:</p>
    <div class="value" id="ldrRaw">---</div>

    <p>Luminosidade estimada:</p>
    <div class="value" id="ldrPercent">---%</div>

    <p>Condicao:</p>
    <div class="status" id="lightStatus">---</div>
  </div>

  <div class="card">
    <h2>Botao SOS</h2>
    <p>Estado:</p>
    <div class="status" id="sosStatus">---</div>

    <p>Total de acionamentos:</p>
    <div class="value" id="sosCounter">---</div>
  </div>

  <div class="card">
    <h2>LED Built-in</h2>
    <p>Estado atual:</p>
    <div class="status" id="ledStatus">---</div>
  </div>

  <p class="small">Atualizacao automatica a cada 1 segundo.</p>

  <script>
    async function atualizarDados() {
      try {
        const resposta = await fetch('/dados');
        const dados = await resposta.json();

        document.getElementById('ldrRaw').textContent = dados.ldrRaw;
        document.getElementById('ldrPercent').textContent = dados.ldrPercent + '%';

        const lightStatus = document.getElementById('lightStatus');
        if (dados.lowLight) {
          lightStatus.textContent = 'BAIXA LUMINOSIDADE';
          lightStatus.className = 'status alerta';
        } else {
          lightStatus.textContent = 'Luminosidade normal';
          lightStatus.className = 'status normal';
        }

        const sosStatus = document.getElementById('sosStatus');
        if (dados.sosActive) {
          sosStatus.textContent = 'SOS ATIVO';
          sosStatus.className = 'status sos';
        } else {
          sosStatus.textContent = 'Inativo';
          sosStatus.className = 'status normal';
        }

        document.getElementById('sosCounter').textContent = dados.sosCounter;
        document.getElementById('ledStatus').textContent = dados.ledStatus;
      } catch (erro) {
        console.log('Erro ao atualizar dados:', erro);
      }
    }

    setInterval(atualizarDados, 1000);
    atualizarDados();
  </script>
</body>
</html>
)rawliteral";

// =========================
// ROTAS DO WEBSERVER
// =========================
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleDados() {
  String json = "{";
  json += "\"ldrRaw\":" + String(ldrRaw) + ",";
  json += "\"ldrPercent\":" + String(ldrPercent) + ",";
  json += "\"lowLight\":" + String(lowLight ? "true" : "false") + ",";
  json += "\"sosActive\":" + String(isSosActive() ? "true" : "false") + ",";
  json += "\"sosCounter\":" + String(sosCounter) + ",";
  json += "\"ledStatus\":\"" + ledStatus + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// =========================
// SETUP
// =========================
void setup() {
  DEBUG_SERIAL.begin(115200);
  delay(1000);

  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println("--- Iniciando Monitoramento LDR + SOS ESP32-C3 ---");
  DEBUG_SERIAL.flush();

  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RGB_LED_PIN, OUTPUT);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  ledOff();

  attachInterrupt(
    digitalPinToInterrupt(SOS_BUTTON_PIN),
    handleSosInterrupt,
    FALLING
  );

#if USE_SOFT_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  DEBUG_SERIAL.println("Modo Access Point iniciado.");
  DEBUG_SERIAL.print("SSID: ");
  DEBUG_SERIAL.println(AP_SSID);
  DEBUG_SERIAL.print("IP do AP: ");
  DEBUG_SERIAL.println(WiFi.softAPIP());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  DEBUG_SERIAL.print("Conectando ao WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_SERIAL.print(".");
    DEBUG_SERIAL.flush();
  }

  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println("WiFi conectado.");
  DEBUG_SERIAL.print("SSID: ");
  DEBUG_SERIAL.println(WiFi.SSID());

  DEBUG_SERIAL.print("IP: ");
  DEBUG_SERIAL.println(WiFi.localIP());

  DEBUG_SERIAL.print("Gateway: ");
  DEBUG_SERIAL.println(WiFi.gatewayIP());

  DEBUG_SERIAL.print("Mascara: ");
  DEBUG_SERIAL.println(WiFi.subnetMask());

  DEBUG_SERIAL.print("RSSI: ");
  DEBUG_SERIAL.print(WiFi.RSSI());
  DEBUG_SERIAL.println(" dBm");
#endif

  server.on("/", handleRoot);
  server.on("/dados", handleDados);

  server.begin();

  DEBUG_SERIAL.println("Servidor HTTP iniciado.");

  printAccessLinks();

  readLdr();
}

// =========================
// LOOP PRINCIPAL
// =========================
void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (sosInterruptFlag) {
    noInterrupts();
    sosInterruptFlag = false;
    interrupts();

    sosCounter++;
    sosActiveUntilMs = now + SOS_LED_TIME_MS;

    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println("===== SOS ACIONADO =====");
    DEBUG_SERIAL.println("Botao SOS detectado por interrupcao.");
    DEBUG_SERIAL.print("LED vermelho ativo por ");
    DEBUG_SERIAL.print(SOS_LED_TIME_MS / 1000);
    DEBUG_SERIAL.println(" segundos.");
    DEBUG_SERIAL.print("Total de acionamentos: ");
    DEBUG_SERIAL.println(sosCounter);
    DEBUG_SERIAL.println("========================");
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.flush();
  }

  if (now - lastAdcReadMs >= ADC_READ_INTERVAL_MS) {
    lastAdcReadMs = now;

    readLdr();

    DEBUG_SERIAL.print("ADC LDR: ");
    DEBUG_SERIAL.print(ldrRaw);
    DEBUG_SERIAL.print(" | Luminosidade: ");
    DEBUG_SERIAL.print(ldrPercent);
    DEBUG_SERIAL.print("% | Baixa luminosidade: ");
    DEBUG_SERIAL.print(lowLight ? "SIM" : "NAO");
    DEBUG_SERIAL.print(" | SOS ativo: ");
    DEBUG_SERIAL.println(isSosActive() ? "SIM" : "NAO");
    DEBUG_SERIAL.flush();
  }

  if (now - lastLinkPrintMs >= 10000) {
    lastLinkPrintMs = now;
    printAccessLinks();
  }

  updateLed();
}