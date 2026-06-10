#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ==========================================================
// CONFIGURACOES PRINCIPAIS
// ==========================================================
//
// Ajuste estes pinos conforme a montagem fisica usada no ESP32.
// Evite pinos usados pelo USB/Serial ou pelo boot da placa.

const int PINO_LED_PWM = 4;
const int PINO_SERVO_PWM = 5;

const int CANAL_PWM_LED = 0;
const int CANAL_PWM_SERVO = 1;

const int RESOLUCAO_PWM_LED_BITS = 10;
const int RESOLUCAO_PWM_SERVO_BITS = 16;

const int FREQUENCIA_LED_MIN_HZ = 1;
const int FREQUENCIA_LED_MAX_HZ = 40000;
const int FREQUENCIA_LED_PADRAO_HZ = 1000;

const int INTENSIDADE_LED_MIN = 0;
const int INTENSIDADE_LED_MAX = 100;
const int INTENSIDADE_LED_PADRAO = 50;

const int FREQUENCIA_SERVO_HZ = 50;
const int ANGULO_SERVO_MIN = 0;
const int ANGULO_SERVO_MAX = 180;
const int ANGULO_SERVO_PADRAO = 90;

// Valores tipicos para servos pequenos: pulso entre 0,5 ms e 2,5 ms.
// Caso o servo utilizado tenha outro intervalo seguro, ajuste aqui.
const int SERVO_PULSO_MIN_US = 500;
const int SERVO_PULSO_MAX_US = 2500;

// ==========================================================
// CONFIGURACAO DO WI-FI
// ==========================================================
//
// O ESP32 cria sua propria rede Wi-Fi local. Conecte o celular
// ou computador nesta rede e acesse o IP mostrado no Monitor Serial.

const char* AP_SSID = "Controle_PWM_ESP32";
const char* AP_PASSWORD = "12345678";

WebServer server(80);

int frequenciaLedHz = FREQUENCIA_LED_PADRAO_HZ;
int intensidadeLedPercentual = INTENSIDADE_LED_PADRAO;
int anguloServoGraus = ANGULO_SERVO_PADRAO;

// ==========================================================
// PAGINA HTML
// ==========================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Controle PWM ESP32</title>
  <style>
    body {
      max-width: 860px;
      margin: 28px auto;
      padding: 0 16px;
      font-family: Arial, sans-serif;
      background-color: #f3f5f7;
      color: #1f2933;
    }

    h1 {
      text-align: center;
      font-size: 28px;
      margin-bottom: 8px;
    }

    .subtitulo {
      text-align: center;
      margin-bottom: 22px;
      color: #52606d;
    }

    .card {
      margin-top: 16px;
      padding: 18px;
      border-radius: 8px;
      background-color: white;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
    }

    .linha {
      display: grid;
      grid-template-columns: 1fr 110px;
      gap: 12px;
      align-items: center;
      margin: 14px 0;
    }

    label {
      font-weight: bold;
    }

    input[type="range"] {
      width: 100%;
    }

    input[type="number"] {
      width: 100%;
      box-sizing: border-box;
      padding: 9px;
      font-size: 16px;
    }

    button {
      padding: 11px 14px;
      border: none;
      border-radius: 6px;
      cursor: pointer;
      color: white;
      background-color: #1565c0;
      font-weight: bold;
      margin-top: 8px;
    }

    button:hover {
      background-color: #0d47a1;
    }

    code {
      font-size: 16px;
    }

    .status-ok {
      color: #1b7f3a;
      font-weight: bold;
    }

    .status-erro {
      color: #b42318;
      font-weight: bold;
    }

    @media (max-width: 560px) {
      .linha {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <h1>Controle PWM ESP32</h1>
  <p class="subtitulo">LED externo e servomotor por interface web local.</p>

  <div class="card">
    <h2>LED externo</h2>

    <label for="intensidadeLed">Intensidade</label>
    <div class="linha">
      <input id="intensidadeLed" type="range" min="0" max="100" step="1">
      <input id="intensidadeLedNumero" type="number" min="0" max="100" step="1">
    </div>

    <label for="frequenciaLed">Frequencia PWM (Hz)</label>
    <div class="linha">
      <input id="frequenciaLed" type="range" min="1" max="40000" step="1">
      <input id="frequenciaLedNumero" type="number" min="1" max="40000" step="1">
    </div>

    <button id="btnAplicarLed">Aplicar LED</button>
  </div>

  <div class="card">
    <h2>Servomotor</h2>

    <label for="anguloServo">Posicao angular</label>
    <div class="linha">
      <input id="anguloServo" type="range" min="0" max="180" step="1">
      <input id="anguloServoNumero" type="number" min="0" max="180" step="1">
    </div>

    <button id="btnAplicarServo">Aplicar Servo</button>
  </div>

  <div class="card">
    <h2>Estado atual</h2>
    <p>Status: <span id="status">Carregando configuracao...</span></p>
    <p>LED: <code><span id="estadoLed">-</span></code></p>
    <p>Servo: <code><span id="estadoServo">-</span></code></p>
  </div>

  <script>
    function limitar(valor, minimo, maximo) {
      return Math.min(Math.max(Number(valor), minimo), maximo);
    }

    function atualizarStatus(texto, erro) {
      const status = document.getElementById("status");
      status.textContent = texto;
      status.className = erro ? "status-erro" : "status-ok";
    }

    function sincronizarControles(rangeId, numeroId, minimo, maximo) {
      const range = document.getElementById(rangeId);
      const numero = document.getElementById(numeroId);

      range.addEventListener("input", function () {
        numero.value = range.value;
      });

      numero.addEventListener("input", function () {
        const valor = limitar(numero.value, minimo, maximo);
        range.value = valor;
      });
    }

    function atualizarEstado(dados) {
      document.getElementById("estadoLed").textContent =
        dados.intensidadeLedPercentual + "%, " +
        dados.frequenciaLedHz + " Hz";

      document.getElementById("estadoServo").textContent =
        dados.anguloServoGraus + " graus";
    }

    function preencherControles(dados) {
      document.getElementById("intensidadeLed").value =
        dados.intensidadeLedPercentual;
      document.getElementById("intensidadeLedNumero").value =
        dados.intensidadeLedPercentual;

      document.getElementById("frequenciaLed").value =
        dados.frequenciaLedHz;
      document.getElementById("frequenciaLedNumero").value =
        dados.frequenciaLedHz;

      document.getElementById("anguloServo").value =
        dados.anguloServoGraus;
      document.getElementById("anguloServoNumero").value =
        dados.anguloServoGraus;

      atualizarEstado(dados);
    }

    async function carregarConfiguracao() {
      try {
        const resposta = await fetch("/config");
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(dados.erro || "Falha ao carregar configuracao.");
        }

        preencherControles(dados);
        atualizarStatus("Configuracao carregada.", false);
      } catch (erro) {
        atualizarStatus("Erro: " + erro.message, true);
      }
    }

    async function aplicarLed() {
      const intensidade = limitar(
        document.getElementById("intensidadeLedNumero").value,
        0,
        100
      );

      const frequencia = limitar(
        document.getElementById("frequenciaLedNumero").value,
        1,
        40000
      );

      try {
        const resposta = await fetch(
          "/led?intensidade=" + encodeURIComponent(intensidade) +
          "&frequencia=" + encodeURIComponent(frequencia)
        );
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(dados.erro || "Falha ao aplicar PWM no LED.");
        }

        preencherControles(dados);
        atualizarStatus("PWM do LED atualizado pelo ESP32.", false);
      } catch (erro) {
        atualizarStatus("Erro: " + erro.message, true);
      }
    }

    async function aplicarServo() {
      const angulo = limitar(
        document.getElementById("anguloServoNumero").value,
        0,
        180
      );

      try {
        const resposta = await fetch(
          "/servo?angulo=" + encodeURIComponent(angulo)
        );
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(dados.erro || "Falha ao aplicar PWM no servo.");
        }

        preencherControles(dados);
        atualizarStatus("Posicao do servo atualizada pelo ESP32.", false);
      } catch (erro) {
        atualizarStatus("Erro: " + erro.message, true);
      }
    }

    document.addEventListener("DOMContentLoaded", async function () {
      sincronizarControles("intensidadeLed", "intensidadeLedNumero", 0, 100);
      sincronizarControles("frequenciaLed", "frequenciaLedNumero", 1, 40000);
      sincronizarControles("anguloServo", "anguloServoNumero", 0, 180);

      document.getElementById("btnAplicarLed").addEventListener("click", aplicarLed);
      document.getElementById("btnAplicarServo").addEventListener("click", aplicarServo);

      await carregarConfiguracao();
    });
  </script>
</body>
</html>
)rawliteral";

// ==========================================================
// FUNCOES AUXILIARES
// ==========================================================

int limitarInteiro(int valor, int minimo, int maximo) {
  if (valor < minimo) {
    return minimo;
  }

  if (valor > maximo) {
    return maximo;
  }

  return valor;
}

uint32_t obterDutyMaximo(int resolucaoBits) {
  return (1UL << resolucaoBits) - 1UL;
}

void configurarPwm(
  int pino,
  int canal,
  int frequenciaHz,
  int resolucaoBits
) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(
    pino,
    frequenciaHz,
    resolucaoBits
  );
#else
  ledcSetup(
    canal,
    frequenciaHz,
    resolucaoBits
  );

  ledcAttachPin(
    pino,
    canal
  );
#endif
}

void escreverPwm(
  int pino,
  int canal,
  uint32_t duty
) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(
    pino,
    duty
  );
#else
  ledcWrite(
    canal,
    duty
  );
#endif
}

String estadoAtualJson() {
  String json = "{";

  json += "\"pinoLedPwm\":";
  json += String(PINO_LED_PWM);
  json += ",";

  json += "\"pinoServoPwm\":";
  json += String(PINO_SERVO_PWM);
  json += ",";

  json += "\"frequenciaLedHz\":";
  json += String(frequenciaLedHz);
  json += ",";

  json += "\"intensidadeLedPercentual\":";
  json += String(intensidadeLedPercentual);
  json += ",";

  json += "\"anguloServoGraus\":";
  json += String(anguloServoGraus);

  json += "}";

  return json;
}

// ==========================================================
// CONTROLE PWM
// ==========================================================

void aplicarPwmLed() {
  uint32_t dutyMaximo =
    obterDutyMaximo(RESOLUCAO_PWM_LED_BITS);

  uint32_t duty =
    (dutyMaximo * intensidadeLedPercentual) / 100;

  configurarPwm(
    PINO_LED_PWM,
    CANAL_PWM_LED,
    frequenciaLedHz,
    RESOLUCAO_PWM_LED_BITS
  );

  escreverPwm(
    PINO_LED_PWM,
    CANAL_PWM_LED,
    duty
  );
}

uint32_t converterAnguloParaDutyServo(int anguloGraus) {
  uint32_t dutyMaximo =
    obterDutyMaximo(RESOLUCAO_PWM_SERVO_BITS);

  int larguraPulsoUs =
    map(
      anguloGraus,
      ANGULO_SERVO_MIN,
      ANGULO_SERVO_MAX,
      SERVO_PULSO_MIN_US,
      SERVO_PULSO_MAX_US
    );

  int periodoUs =
    1000000 / FREQUENCIA_SERVO_HZ;

  return (
    static_cast<uint32_t>(larguraPulsoUs) *
    dutyMaximo
  ) / periodoUs;
}

void aplicarPwmServo() {
  configurarPwm(
    PINO_SERVO_PWM,
    CANAL_PWM_SERVO,
    FREQUENCIA_SERVO_HZ,
    RESOLUCAO_PWM_SERVO_BITS
  );

  escreverPwm(
    PINO_SERVO_PWM,
    CANAL_PWM_SERVO,
    converterAnguloParaDutyServo(anguloServoGraus)
  );
}

void registrarEstadoNoSerial() {
  Serial.println();
  Serial.println("===== ESTADO PWM =====");

  Serial.print("LED externo no GPIO ");
  Serial.print(PINO_LED_PWM);
  Serial.print(": ");
  Serial.print(intensidadeLedPercentual);
  Serial.print("% em ");
  Serial.print(frequenciaLedHz);
  Serial.println(" Hz");

  Serial.print("Servo no GPIO ");
  Serial.print(PINO_SERVO_PWM);
  Serial.print(": ");
  Serial.print(anguloServoGraus);
  Serial.println(" graus");

  Serial.println("======================");
}

// ==========================================================
// ROTAS HTTP
// ==========================================================

void responderPaginaPrincipal() {
  server.send_P(
    200,
    "text/html",
    HTML_PAGE
  );
}

void responderConfiguracao() {
  server.send(
    200,
    "application/json",
    estadoAtualJson()
  );
}

void responderControleLed() {
  if (
    !server.hasArg("intensidade") ||
    !server.hasArg("frequencia")
  ) {
    server.send(
      400,
      "application/json",
      "{\"erro\":\"Informe intensidade e frequencia.\"}"
    );

    return;
  }

  intensidadeLedPercentual =
    limitarInteiro(
      server.arg("intensidade").toInt(),
      INTENSIDADE_LED_MIN,
      INTENSIDADE_LED_MAX
    );

  frequenciaLedHz =
    limitarInteiro(
      server.arg("frequencia").toInt(),
      FREQUENCIA_LED_MIN_HZ,
      FREQUENCIA_LED_MAX_HZ
    );

  aplicarPwmLed();
  registrarEstadoNoSerial();

  server.send(
    200,
    "application/json",
    estadoAtualJson()
  );
}

void responderControleServo() {
  if (!server.hasArg("angulo")) {
    server.send(
      400,
      "application/json",
      "{\"erro\":\"Informe o angulo do servo.\"}"
    );

    return;
  }

  anguloServoGraus =
    limitarInteiro(
      server.arg("angulo").toInt(),
      ANGULO_SERVO_MIN,
      ANGULO_SERVO_MAX
    );

  aplicarPwmServo();
  registrarEstadoNoSerial();

  server.send(
    200,
    "application/json",
    estadoAtualJson()
  );
}

// ==========================================================
// SETUP
// ==========================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Inicializando controle PWM no ESP32.");

  aplicarPwmLed();
  aplicarPwmServo();

  WiFi.mode(WIFI_AP);

  bool apIniciado =
    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD
    );

  if (apIniciado) {
    Serial.println("Access Point iniciado com sucesso.");
  } else {
    Serial.println("ERRO: falha ao iniciar o Access Point.");
  }

  IPAddress ipDoAccessPoint =
    WiFi.softAPIP();

  Serial.println();
  Serial.println("===== DADOS DA REDE =====");
  Serial.print("Nome da rede Wi-Fi: ");
  Serial.println(AP_SSID);
  Serial.print("Senha da rede Wi-Fi: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP do ESP32: ");
  Serial.println(ipDoAccessPoint);
  Serial.println("=========================");

  server.on(
    "/",
    HTTP_GET,
    responderPaginaPrincipal
  );

  server.on(
    "/config",
    HTTP_GET,
    responderConfiguracao
  );

  server.on(
    "/led",
    HTTP_GET,
    responderControleLed
  );

  server.on(
    "/servo",
    HTTP_GET,
    responderControleServo
  );

  server.begin();

  Serial.println();
  Serial.println("Servidor HTTP iniciado.");
  Serial.print("Acesse no navegador: http://");
  Serial.println(ipDoAccessPoint);

  registrarEstadoNoSerial();
}

// ==========================================================
// LOOP
// ==========================================================

void loop() {
  server.handleClient();
  delay(2);
}
