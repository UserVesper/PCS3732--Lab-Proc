#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// =========================
// CONFIGURACAO DO WIFI
// =========================
// Coloque aqui o Wi-Fi real que o ESP32-C3 deve acessar.
// Importante: ESP32 usa Wi-Fi 2.4 GHz, nao 5 GHz.
const char* WIFI_SSID = "NOME_DO_SEU_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_SEU_WIFI";

// Caso o ESP32 nao consiga conectar ao Wi-Fi acima,
// ele cria uma rede propria aberta com este nome.
const char* AP_SSID = "Monitoramento_ESP32_GRUPO_E";
const char* AP_PASSWORD = "12345678";

WebServer server(80);

// =========================
// SERIAL
// =========================
#define DEBUG_SERIAL Serial

// =========================
// PINOS
// =========================
const int LDR_PIN = 4;          // GPIO4 - entrada analogica ADC
const int SOS_BUTTON_PIN = 5;   // GPIO5 - botao com INPUT_PULLUP

// LED BuiltIn da placa ESP32-C3 usado como NeoPixel
// Baseado no codigo aula2: 1 LED NeoPixel no GPIO8.
#define PINO_LED_BUILTIN      8
#define NUMERO_LEDS_BUILTIN   1

Adafruit_NeoPixel led(NUMERO_LEDS_BUILTIN, PINO_LED_BUILTIN, NEO_GRB + NEO_KHZ800);

// =========================
// CONFIGURACAO DO LDR / ADC
// =========================
const int ADC_RESOLUTION_BITS = 12;
const int ADC_MAX_VALUE = 4095;
const unsigned long ADC_READ_INTERVAL_MS = 1000;

// Na sua montagem real, foi observado que:
// - lanterna direta no LDR  -> valor ADC menor
// - sensor tampado/escuro   -> valor ADC maior
// Por isso, o percentual de luminosidade e calculado de forma invertida:
// ADC alto  -> pouca luz  -> percentual baixo
// ADC baixo -> muita luz   -> percentual alto
const int LOW_LIGHT_PERCENT_THRESHOLD = 30; // <= 30% entra em modo noturno

// =========================
// CONFIGURACAO DO BOTAO DE TRAVESSIA
// =========================
const unsigned long PEDESTRIAN_RED_TIME_MS = 3000;
const unsigned long DEBOUNCE_TIME_US = 200000;

// =========================
// CONFIGURACAO DO SEMAFORO
// =========================
const unsigned long GREEN_TIME_MS = 3000;
const unsigned long YELLOW_TIME_MS = 1000;
const unsigned long RED_TIME_MS = 4000;

// Modo noturno: 1 piscada por segundo
const unsigned long NIGHT_BLINK_PERIOD_MS = 1000;
const unsigned long NIGHT_BLINK_ON_TIME_MS = 500;

// =========================
// VARIAVEIS GLOBAIS
// =========================
volatile bool pedestrianInterruptFlag = false;
volatile unsigned long lastInterruptUs = 0;

unsigned long pedestrianRedUntilMs = 0;
unsigned long lastAdcReadMs = 0;
unsigned long lastLinkPrintMs = 0;

int ldrRaw = 0;
int ldrPercent = 0;
bool lowLight = false;

unsigned long pedestrianCounter = 0;
String ledStatus = "desligado";
String operationMode = "iniciando";

bool usingSoftAP = false;
bool normalCycleInitialized = false;

uint8_t currentLedRed = 0;
uint8_t currentLedGreen = 0;
uint8_t currentLedBlue = 0;
bool currentLedDefined = false;

enum TrafficState {
  TRAFFIC_GREEN,
  TRAFFIC_YELLOW,
  TRAFFIC_RED
};

TrafficState trafficState = TRAFFIC_GREEN;
unsigned long trafficStateStartMs = 0;

// =========================
// FUNCOES DO LED BUILTIN NEOPIXEL
// =========================
void setBuiltinLed(uint8_t red, uint8_t green, uint8_t blue) {
  if (currentLedDefined &&
      currentLedRed == red &&
      currentLedGreen == green &&
      currentLedBlue == blue) {
    return;
  }

  led.setPixelColor(0, led.Color(red, green, blue));
  led.show();

  currentLedRed = red;
  currentLedGreen = green;
  currentLedBlue = blue;
  currentLedDefined = true;
}

void ledOff() {
  setBuiltinLed(0, 0, 0);
}

void ledGreen() {
  setBuiltinLed(0, 255, 0);
}

void ledYellow() {
  setBuiltinLed(255, 200, 0);
}

void ledRed() {
  setBuiltinLed(255, 0, 0);
}

// =========================
// INTERRUPCAO DO BOTAO DE TRAVESSIA
// =========================
void IRAM_ATTR handlePedestrianInterrupt() {
  unsigned long nowUs = micros();

  if (nowUs - lastInterruptUs >= DEBOUNCE_TIME_US) {
    pedestrianInterruptFlag = true;
    lastInterruptUs = nowUs;
  }
}

// =========================
// LEITURA DO LDR
// =========================
void readLdr() {
  ldrRaw = analogRead(LDR_PIN);

  ldrPercent = map(ldrRaw, ADC_MAX_VALUE, 0, 0, 100);
  ldrPercent = constrain(ldrPercent, 0, 100);

  lowLight = ldrPercent <= LOW_LIGHT_PERCENT_THRESHOLD;
}

// =========================
// ESTADO DO BOTAO DE TRAVESSIA
// =========================
bool isPedestrianRequestActive() {
  return millis() < pedestrianRedUntilMs;
}

// =========================
// SEMAFORO NORMAL
// =========================
unsigned long getTrafficStateDuration(TrafficState state) {
  switch (state) {
    case TRAFFIC_GREEN:
      return GREEN_TIME_MS;
    case TRAFFIC_YELLOW:
      return YELLOW_TIME_MS;
    case TRAFFIC_RED:
      return RED_TIME_MS;
    default:
      return GREEN_TIME_MS;
  }
}

void goToNextTrafficState(unsigned long now) {
  if (trafficState == TRAFFIC_GREEN) {
    trafficState = TRAFFIC_YELLOW;
  } else if (trafficState == TRAFFIC_YELLOW) {
    trafficState = TRAFFIC_RED;
  } else {
    trafficState = TRAFFIC_GREEN;
  }

  trafficStateStartMs = now;
}

void updateNormalTrafficCycle(unsigned long now) {
  if (!normalCycleInitialized) {
    trafficState = TRAFFIC_GREEN;
    trafficStateStartMs = now;
    normalCycleInitialized = true;
  }

  if (now - trafficStateStartMs >= getTrafficStateDuration(trafficState)) {
    goToNextTrafficState(now);
  }
}

// =========================
// ATUALIZACAO DO LED / MODOS DO SEMAFORO
// =========================
void updateTrafficLight() {
  unsigned long now = millis();

  if (isPedestrianRequestActive()) {
    normalCycleInitialized = false;
    ledRed();
    operationMode = "travessia de pedestres";
    ledStatus = "vermelho - travessia solicitada";
    return;
  }

  if (lowLight) {
    normalCycleInitialized = false;
    operationMode = "noturno";

    unsigned long phase = now % NIGHT_BLINK_PERIOD_MS;

    if (phase < NIGHT_BLINK_ON_TIME_MS) {
      ledYellow();
      ledStatus = "amarelo aceso - modo noturno";
    } else {
      ledOff();
      ledStatus = "amarelo apagado - modo noturno";
    }

    return;
  }

  operationMode = "normal";
  updateNormalTrafficCycle(now);

  if (trafficState == TRAFFIC_GREEN) {
    ledGreen();
    ledStatus = "verde - fluxo liberado";
  } else if (trafficState == TRAFFIC_YELLOW) {
    ledYellow();
    ledStatus = "amarelo - atencao";
  } else {
    ledRed();
    ledStatus = "vermelho - pare";
  }
}

// =========================
// IMPRESSAO DOS LINKS
// =========================
void printAccessLinks() {
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println("===== LINKS DE ACESSO =====");

  if (usingSoftAP) {
    DEBUG_SERIAL.print("Rede criada pelo ESP32: ");
    DEBUG_SERIAL.println(AP_SSID);
    DEBUG_SERIAL.print("Acesse: http://");
    DEBUG_SERIAL.println(WiFi.softAPIP());
  } else {
    DEBUG_SERIAL.print("Wi-Fi conectado: ");
    DEBUG_SERIAL.println(WiFi.SSID());

    DEBUG_SERIAL.print("IP do ESP32: ");
    DEBUG_SERIAL.println(WiFi.localIP());

    DEBUG_SERIAL.print("Acesse: http://");
    DEBUG_SERIAL.println(WiFi.localIP());

    DEBUG_SERIAL.print("Endpoint JSON: http://");
    DEBUG_SERIAL.print(WiFi.localIP());
    DEBUG_SERIAL.println("/dados");
  }

  DEBUG_SERIAL.println("===========================");
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.flush();
}

// =========================
// WIFI
// =========================
void startSoftAP() {
  WiFi.mode(WIFI_AP);

  if (strlen(AP_PASSWORD) == 0) {
    WiFi.softAP(AP_SSID);
  } else {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }

  usingSoftAP = true;

  DEBUG_SERIAL.println();
  DEBUG_SERIAL.println("Nao foi possivel conectar ao Wi-Fi configurado.");
  DEBUG_SERIAL.println("Modo Access Point iniciado.");
  DEBUG_SERIAL.print("SSID: ");
  DEBUG_SERIAL.println(AP_SSID);
  DEBUG_SERIAL.print("IP do AP: ");
  DEBUG_SERIAL.println(WiFi.softAPIP());
}

void connectToWiFiOrStartAP() {
  usingSoftAP = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  DEBUG_SERIAL.print("Conectando ao Wi-Fi: ");
  DEBUG_SERIAL.println(WIFI_SSID);

  unsigned long startAttemptTime = millis();
  const unsigned long WIFI_TIMEOUT_MS = 20000;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    delay(500);
    DEBUG_SERIAL.print(".");
    DEBUG_SERIAL.flush();
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println("Wi-Fi conectado com sucesso.");
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
  } else {
    startSoftAP();
  }
}

// =========================
// PAGINA WEB
// =========================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <title>Semaforo com LDR e Botao</title>
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
  <h1>Semaforo Inteligente com LDR</h1>

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
    <h2>Botao de Travessia</h2>
    <p>Estado:</p>
    <div class="status" id="pedestrianStatus">---</div>

    <p>Total de solicitacoes:</p>
    <div class="value" id="pedestrianCounter">---</div>
  </div>

  <div class="card">
    <h2>Semaforo</h2>
    <p>Modo:</p>
    <div class="status" id="operationMode">---</div>

    <p>LED BuiltIn:</p>
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
          lightStatus.textContent = 'BAIXA LUMINOSIDADE - MODO NOTURNO';
          lightStatus.className = 'status alerta';
        } else {
          lightStatus.textContent = 'Luminosidade normal';
          lightStatus.className = 'status normal';
        }

        const pedestrianStatus = document.getElementById('pedestrianStatus');
        if (dados.pedestrianActive) {
          pedestrianStatus.textContent = 'TRAVESSIA SOLICITADA';
          pedestrianStatus.className = 'status sos';
        } else {
          pedestrianStatus.textContent = 'Inativo';
          pedestrianStatus.className = 'status normal';
        }

        document.getElementById('pedestrianCounter').textContent = dados.pedestrianCounter;
        document.getElementById('operationMode').textContent = dados.operationMode;
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
  json += "\"pedestrianActive\":" + String(isPedestrianRequestActive() ? "true" : "false") + ",";
  json += "\"pedestrianCounter\":" + String(pedestrianCounter) + ",";
  json += "\"operationMode\":\"" + operationMode + "\",";
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
  DEBUG_SERIAL.println("--- Iniciando Semaforo Inteligente com LDR e Botao ---");
  DEBUG_SERIAL.flush();

  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);

  led.begin();
  led.setBrightness(80);
  ledOff();

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  attachInterrupt(
    digitalPinToInterrupt(SOS_BUTTON_PIN),
    handlePedestrianInterrupt,
    FALLING
  );

  connectToWiFiOrStartAP();

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

  if (pedestrianInterruptFlag) {
    noInterrupts();
    pedestrianInterruptFlag = false;
    interrupts();

    pedestrianCounter++;
    pedestrianRedUntilMs = now + PEDESTRIAN_RED_TIME_MS;

    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println("===== TRAVESSIA SOLICITADA =====");
    DEBUG_SERIAL.println("Botao de travessia detectado por interrupcao.");
    DEBUG_SERIAL.print("LED vermelho ativo por ");
    DEBUG_SERIAL.print(PEDESTRIAN_RED_TIME_MS / 1000);
    DEBUG_SERIAL.println(" segundos.");
    DEBUG_SERIAL.print("Total de solicitacoes: ");
    DEBUG_SERIAL.println(pedestrianCounter);
    DEBUG_SERIAL.println("================================");
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
    DEBUG_SERIAL.print("% | Modo noturno: ");
    DEBUG_SERIAL.print(lowLight ? "SIM" : "NAO");
    DEBUG_SERIAL.print(" | Travessia ativa: ");
    DEBUG_SERIAL.println(isPedestrianRequestActive() ? "SIM" : "NAO");
    DEBUG_SERIAL.flush();
  }

  if (now - lastLinkPrintMs >= 10000) {
    lastLinkPrintMs = now;
    printAccessLinks();
  }

  updateTrafficLight();
}