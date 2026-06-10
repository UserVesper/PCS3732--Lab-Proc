#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ==========================================================
// CONFIGURACOES PRINCIPAIS
// ==========================================================
// Versao final para uma placa ESP32-C3 fisica.
// Use os numeros GPIO impressos na placa, e nao a posicao
// fisica do pino no conector.
//
// LED:
// GPIO4 -> resistor de 220 a 330 ohms -> anodo do LED
// catodo do LED -> GND
//
// Servo:
// fio de sinal -> GPIO5
// alimentacao externa de 5 V
// GND da fonte externa conectado ao GND do ESP32

const int PINO_LED_PWM = 4;
const int PINO_SERVO_PWM = 5;

// O canal 5 e reservado para o LED.
// O servomotor fica sob responsabilidade da biblioteca ESP32Servo,
// que aloca automaticamente um canal livre.
const int CANAL_PWM_LED = 5;

// A resolucao do LED e escolhida automaticamente.
// O codigo tenta usar inicialmente a maior resolucao possivel.
const int RESOLUCAO_PWM_LED_MIN_BITS = 1;
const int RESOLUCAO_PWM_LED_MAX_BITS = 14;
const int RESOLUCAO_PWM_LED_PADRAO_BITS = 10;

// Utilizado apenas para exibir na interface web o duty cycle
// equivalente do servo. O sinal efetivo e gerado pela ESP32Servo.
const int RESOLUCAO_PWM_SERVO_REFERENCIA_BITS = 14;

const int FREQUENCIA_LED_MIN_HZ = 1;
const int FREQUENCIA_LED_MAX_HZ = 20000;
const int FREQUENCIA_LED_PADRAO_HZ = 1000;

const int INTENSIDADE_LED_MIN = 0;
const int INTENSIDADE_LED_MAX = 100;
const int INTENSIDADE_LED_PADRAO = 50;

const int FREQUENCIA_SERVO_HZ = 50;
const int ANGULO_SERVO_MIN = 0;
const int ANGULO_SERVO_MAX = 180;
const int ANGULO_SERVO_PADRAO = 90;

// Faixa mostrada no grafico:
// 0 graus   -> 1000 us
// 90 graus  -> 1500 us
// 180 graus -> 2000 us
const int SERVO_PULSO_MIN_US = 1000;
const int SERVO_PULSO_MAX_US = 2000;

// Testa o LED no boot antes de iniciar o servidor web.
const bool EXECUTAR_TESTE_INICIAL_LED = true;

// ==========================================================
// CONFIGURACAO DO WI-FI
// ==========================================================

const char* AP_SSID = "Controle_PWM_ESP32";
const char* AP_PASSWORD = "12345678";

WebServer server(80);
Servo servoMotor;

int frequenciaLedHz = FREQUENCIA_LED_PADRAO_HZ;
int intensidadeLedPercentual = INTENSIDADE_LED_PADRAO;
int resolucaoPwmLedBits = RESOLUCAO_PWM_LED_PADRAO_BITS;
int anguloServoGraus = ANGULO_SERVO_PADRAO;

bool ledPwmConfigurado = false;

// ==========================================================
// PAGINA HTML
// ==========================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Controle PWM ESP32-C3</title>

  <style>
    body {
      max-width: 900px;
      margin: 28px auto;
      padding: 0 16px;
      font-family: Arial, sans-serif;
      background: #f3f5f7;
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
      background: white;
      box-shadow: 0 2px 8px rgba(0, 0, 0, .12);
    }

    .linha {
      display: grid;
      grid-template-columns: 1fr 120px;
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
      background: #1565c0;
      font-weight: bold;
      margin-top: 8px;
    }

    button:hover {
      background: #0d47a1;
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
  <h1>Controle PWM ESP32-C3</h1>

  <p class="subtitulo">
    LED externo e servomotor por interface web local.
  </p>

  <div class="card">
    <h2>LED externo</h2>

    <label for="intensidadeLed">Intensidade</label>

    <div class="linha">
      <input
        id="intensidadeLed"
        type="range"
        min="0"
        max="100"
        step="1"
      >

      <input
        id="intensidadeLedNumero"
        type="number"
        min="0"
        max="100"
        step="1"
      >
    </div>

    <label for="frequenciaLed">Frequencia PWM (Hz)</label>

    <div class="linha">
      <input
        id="frequenciaLed"
        type="range"
        min="1"
        max="20000"
        step="1"
      >

      <input
        id="frequenciaLedNumero"
        type="number"
        min="1"
        max="20000"
        step="1"
      >
    </div>

    <button id="btnAplicarLed">
      Aplicar LED
    </button>
  </div>

  <div class="card">
    <h2>Servomotor</h2>

    <label for="anguloServo">Posicao angular</label>

    <div class="linha">
      <input
        id="anguloServo"
        type="range"
        min="0"
        max="180"
        step="1"
      >

      <input
        id="anguloServoNumero"
        type="number"
        min="0"
        max="180"
        step="1"
      >
    </div>

    <button id="btnAplicarServo">
      Aplicar servo
    </button>
  </div>

  <div class="card">
    <h2>Estado atual</h2>

    <p>
      Status:
      <span id="status">Carregando configuracao...</span>
    </p>

    <p>
      LED:
      <code>
        <span id="estadoLed">-</span>
      </code>
    </p>

    <p>
      Servo:
      <code>
        <span id="estadoServo">-</span>
      </code>
    </p>
  </div>

  <script>
    function limitar(valor, minimo, maximo) {
      return Math.min(Math.max(Number(valor), minimo), maximo);
    }

    function atualizarStatus(texto, erro) {
      const elemento = document.getElementById("status");

      elemento.textContent = texto;
      elemento.className = erro ? "status-erro" : "status-ok";
    }

    function sincronizarControles(rangeId, numeroId, minimo, maximo) {
      const range = document.getElementById(rangeId);
      const numero = document.getElementById(numeroId);

      range.addEventListener("input", () => {
        numero.value = range.value;
      });

      numero.addEventListener("input", () => {
        range.value = limitar(numero.value, minimo, maximo);
      });
    }

    function atualizarEstado(dados) {
      document.getElementById("estadoLed").textContent =
        `${dados.intensidadeLedPercentual}% | ` +
        `${dados.frequenciaLedHz} Hz | ` +
        `resolucao=${dados.resolucaoLedBits} bits | ` +
        `duty=${dados.dutyLed}/${dados.dutyMaximoLed}`;

      document.getElementById("estadoServo").textContent =
        `${dados.anguloServoGraus} graus | ` +
        `pulso=${dados.pulsoServoUs} us | ` +
        `duty=${dados.dutyServo}/${dados.dutyMaximoServo}`;
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

    async function consultar(url, mensagem) {
      try {
        const resposta = await fetch(url);
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(dados.erro || "Falha na requisicao.");
        }

        preencherControles(dados);
        atualizarStatus(mensagem, false);
      } catch (erro) {
        atualizarStatus("Erro: " + erro.message, true);
      }
    }

    async function carregarConfiguracao() {
      await consultar("/config", "Configuracao carregada.");
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
        20000
      );

      await consultar(
        `/led?intensidade=${encodeURIComponent(intensidade)}` +
        `&frequencia=${encodeURIComponent(frequencia)}`,
        "PWM do LED atualizado pelo ESP32."
      );
    }

    async function aplicarServo() {
      const angulo = limitar(
        document.getElementById("anguloServoNumero").value,
        0,
        180
      );

      await consultar(
        `/servo?angulo=${encodeURIComponent(angulo)}`,
        "Posicao do servo atualizada pelo ESP32."
      );
    }

    document.addEventListener("DOMContentLoaded", async () => {
      sincronizarControles(
        "intensidadeLed",
        "intensidadeLedNumero",
        0,
        100
      );

      sincronizarControles(
        "frequenciaLed",
        "frequenciaLedNumero",
        1,
        20000
      );

      sincronizarControles(
        "anguloServo",
        "anguloServoNumero",
        0,
        180
      );

      document
        .getElementById("btnAplicarLed")
        .addEventListener("click", aplicarLed);

      document
        .getElementById("btnAplicarServo")
        .addEventListener("click", aplicarServo);

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
  if (valor < minimo) return minimo;
  if (valor > maximo) return maximo;

  return valor;
}

uint32_t obterDutyMaximo(int resolucaoBits) {
  return (1UL << resolucaoBits) - 1UL;
}

uint32_t obterDutyLed() {
  return (
    obterDutyMaximo(resolucaoPwmLedBits) *
    intensidadeLedPercentual
  ) / 100;
}

int obterPulsoServoUs() {
  return map(
    anguloServoGraus,
    ANGULO_SERVO_MIN,
    ANGULO_SERVO_MAX,
    SERVO_PULSO_MIN_US,
    SERVO_PULSO_MAX_US
  );
}

// Essa funcao existe apenas para mostrar uma referencia na interface.
// O servo nao e controlado manualmente por este duty cycle.
uint32_t obterDutyServoReferencia() {
  const int periodoUs = 1000000 / FREQUENCIA_SERVO_HZ;

  return (
    static_cast<uint32_t>(obterPulsoServoUs()) *
    obterDutyMaximo(RESOLUCAO_PWM_SERVO_REFERENCIA_BITS)
  ) / periodoUs;
}

// ==========================================================
// CONTROLE DO PWM DO LED
// ==========================================================

bool escreverPwmLed(uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3

  return ledcWrite(PINO_LED_PWM, duty);

#else

  ledcWrite(CANAL_PWM_LED, duty);
  return true;

#endif
}

bool tentarConfigurarOuAtualizarPwmLedComResolucao(
  int resolucaoBits
) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3

  if (!ledPwmConfigurado) {
    const bool configurado = ledcAttachChannel(
      PINO_LED_PWM,
      frequenciaLedHz,
      resolucaoBits,
      CANAL_PWM_LED
    );

    if (configurado) {
      ledPwmConfigurado = true;
    }

    return configurado;
  }

  return ledcChangeFrequency(
    PINO_LED_PWM,
    frequenciaLedHz,
    resolucaoBits
  ) != 0;

#else

  const double frequenciaObtida = ledcSetup(
    CANAL_PWM_LED,
    frequenciaLedHz,
    resolucaoBits
  );

  if (frequenciaObtida == 0) {
    return false;
  }

  if (!ledPwmConfigurado) {
    ledcAttachPin(PINO_LED_PWM, CANAL_PWM_LED);
    ledPwmConfigurado = true;
  }

  return true;

#endif
}

bool configurarOuAtualizarPwmLed() {
  // Testa primeiro a maior resolucao disponivel.
  // Caso a combinacao nao seja suportada, diminui a resolucao
  // progressivamente ate encontrar uma configuracao valida.

  for (
    int bits = RESOLUCAO_PWM_LED_MAX_BITS;
    bits >= RESOLUCAO_PWM_LED_MIN_BITS;
    bits--
  ) {
    if (tentarConfigurarOuAtualizarPwmLedComResolucao(bits)) {
      resolucaoPwmLedBits = bits;

      return true;
    }
  }

  return false;
}

bool aplicarPwmLed() {
  if (!configurarOuAtualizarPwmLed()) {
    return false;
  }

  return escreverPwmLed(obterDutyLed());
}

// ==========================================================
// CONTROLE DO SERVOMOTOR COM ESP32Servo
// ==========================================================

bool configurarServo() {
  if (servoMotor.attached()) {
    return true;
  }

  servoMotor.setPeriodHertz(FREQUENCIA_SERVO_HZ);

  servoMotor.attach(
    PINO_SERVO_PWM,
    SERVO_PULSO_MIN_US,
    SERVO_PULSO_MAX_US
  );

  return servoMotor.attached();
}

bool aplicarPosicaoServo() {
  if (!configurarServo()) {
    return false;
  }

  servoMotor.write(anguloServoGraus);

  return true;
}

// ==========================================================
// GERACAO DO JSON
// ==========================================================

String estadoAtualJson() {
  String json = "{";

  json += "\"pinoLedPwm\":" +
          String(PINO_LED_PWM) + ",";

  json += "\"pinoServoPwm\":" +
          String(PINO_SERVO_PWM) + ",";

  json += "\"frequenciaLedHz\":" +
          String(frequenciaLedHz) + ",";

  json += "\"intensidadeLedPercentual\":" +
          String(intensidadeLedPercentual) + ",";

  json += "\"resolucaoLedBits\":" +
          String(resolucaoPwmLedBits) + ",";

  json += "\"dutyLed\":" +
          String(obterDutyLed()) + ",";

  json += "\"dutyMaximoLed\":" +
          String(obterDutyMaximo(resolucaoPwmLedBits)) + ",";

  json += "\"anguloServoGraus\":" +
          String(anguloServoGraus) + ",";

  json += "\"pulsoServoUs\":" +
          String(obterPulsoServoUs()) + ",";

  json += "\"dutyServo\":" +
          String(obterDutyServoReferencia()) + ",";

  json += "\"dutyMaximoServo\":" +
          String(
            obterDutyMaximo(
              RESOLUCAO_PWM_SERVO_REFERENCIA_BITS
            )
          );

  json += "}";

  return json;
}

// ==========================================================
// MONITOR SERIAL
// ==========================================================

void registrarEstadoNoSerial() {
  Serial.println();
  Serial.println("===== ESTADO PWM =====");

  Serial.printf(
    "LED externo: GPIO %d | intensidade=%d%% | "
    "frequencia=%d Hz | resolucao=%d bits | duty=%lu/%lu\n",
    PINO_LED_PWM,
    intensidadeLedPercentual,
    frequenciaLedHz,
    resolucaoPwmLedBits,
    static_cast<unsigned long>(obterDutyLed()),
    static_cast<unsigned long>(
      obterDutyMaximo(resolucaoPwmLedBits)
    )
  );

  Serial.printf(
    "Servo ESP32Servo: GPIO %d | angulo=%d graus | "
    "pulso=%d us | duty equivalente=%lu/%lu | "
    "frequencia=%d Hz\n",
    PINO_SERVO_PWM,
    anguloServoGraus,
    obterPulsoServoUs(),
    static_cast<unsigned long>(
      obterDutyServoReferencia()
    ),
    static_cast<unsigned long>(
      obterDutyMaximo(
        RESOLUCAO_PWM_SERVO_REFERENCIA_BITS
      )
    ),
    FREQUENCIA_SERVO_HZ
  );

  Serial.println("======================");
}

// ==========================================================
// TESTE DO LED NO BOOT
// ==========================================================

void executarTesteInicialLed() {
  if (!EXECUTAR_TESTE_INICIAL_LED) {
    return;
  }

  Serial.println();
  Serial.println("Teste independente do LED externo: inicio.");

  const int intensidades[] = {
    0,
    25,
    50,
    75,
    100,
    0
  };

  const int intensidadeOriginal = intensidadeLedPercentual;

  for (const int intensidade : intensidades) {
    intensidadeLedPercentual = intensidade;

    aplicarPwmLed();

    Serial.printf(
      "Teste LED: intensidade=%d%% | resolucao=%d bits | "
      "duty=%lu/%lu\n",
      intensidadeLedPercentual,
      resolucaoPwmLedBits,
      static_cast<unsigned long>(obterDutyLed()),
      static_cast<unsigned long>(
        obterDutyMaximo(resolucaoPwmLedBits)
      )
    );

    delay(350);
  }

  intensidadeLedPercentual = intensidadeOriginal;

  aplicarPwmLed();

  Serial.println("Teste independente do LED externo: fim.");
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

  const int intensidadeAnterior = intensidadeLedPercentual;
  const int frequenciaAnterior = frequenciaLedHz;

  intensidadeLedPercentual = limitarInteiro(
    server.arg("intensidade").toInt(),
    INTENSIDADE_LED_MIN,
    INTENSIDADE_LED_MAX
  );

  frequenciaLedHz = limitarInteiro(
    server.arg("frequencia").toInt(),
    FREQUENCIA_LED_MIN_HZ,
    FREQUENCIA_LED_MAX_HZ
  );

  if (!aplicarPwmLed()) {
    // Caso nenhuma resolucao funcione, o ultimo estado valido
    // e restaurado automaticamente.

    intensidadeLedPercentual = intensidadeAnterior;
    frequenciaLedHz = frequenciaAnterior;

    aplicarPwmLed();

    server.send(
      500,
      "application/json",
      "{\"erro\":\"Nao foi possivel configurar o PWM do LED "
      "nessa frequencia.\"}"
    );

    return;
  }

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

  anguloServoGraus = limitarInteiro(
    server.arg("angulo").toInt(),
    ANGULO_SERVO_MIN,
    ANGULO_SERVO_MAX
  );

  if (!aplicarPosicaoServo()) {
    server.send(
      500,
      "application/json",
      "{\"erro\":\"Nao foi possivel configurar o PWM do servo.\"}"
    );

    return;
  }

  registrarEstadoNoSerial();

  server.send(
    200,
    "application/json",
    estadoAtualJson()
  );
}

// ==========================================================
// WI-FI
// ==========================================================

bool iniciarWifi() {
  WiFi.mode(WIFI_AP);

  const bool apIniciado = WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );

  if (!apIniciado) {
    Serial.println(
      "ERRO: falha ao iniciar o Access Point."
    );

    return false;
  }

  Serial.println();
  Serial.println("===== REDE WI-FI LOCAL =====");

  Serial.print("Rede criada: ");
  Serial.println(AP_SSID);

  Serial.print("Senha: ");
  Serial.println(AP_PASSWORD);

  Serial.print("Abra no navegador: http://");
  Serial.println(WiFi.softAPIP());

  Serial.println("============================");

  return true;
}

// ==========================================================
// SETUP E LOOP
// ==========================================================

void setup() {
  Serial.begin(115200);

  delay(700);

  Serial.println();
  Serial.println(
    "Inicializando controle PWM no ESP32-C3."
  );

  Serial.println(
    "Servomotor controlado pela biblioteca ESP32Servo."
  );

  Serial.printf(
    "LED PWM: GPIO %d | Servo PWM: GPIO %d\n",
    PINO_LED_PWM,
    PINO_SERVO_PWM
  );

  if (!aplicarPwmLed()) {
    Serial.println(
      "ERRO: falha na configuracao inicial do LED PWM."
    );
  }

  delay(30);

  if (!aplicarPosicaoServo()) {
    Serial.println(
      "ERRO: falha na configuracao inicial do servo PWM."
    );
  }

  executarTesteInicialLed();

  if (!iniciarWifi()) {
    Serial.println(
      "Servidor HTTP nao iniciado porque o Access Point falhou."
    );

    return;
  }

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

  Serial.println("Servidor HTTP iniciado.");

  registrarEstadoNoSerial();
}

void loop() {
  server.handleClient();

  delay(2);
}