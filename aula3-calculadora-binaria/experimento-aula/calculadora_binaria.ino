#include <WiFi.h>
#include <WebServer.h>

// =========================
// CONFIGURACAO DOS LEDS
// =========================
// Para montagem fisica no ESP32-C3 SuperMini, evitamos o GPIO2,
// pois ele pode interferir no boot em alguns cenarios.
//
// LEDS[0] representa o bit menos significativo (bit 0).
// LEDS[3] representa o bit mais significativo (bit 3 / sinal em complemento de dois).
//
// Ligacao fisica sugerida:
// GPIO 0 -> resistor -> LED bit 0 -> GND
// GPIO 1 -> resistor -> LED bit 1 -> GND
// GPIO 2 -> resistor -> LED bit 2 -> GND
// GPIO 3 -> resistor -> LED bit 3 -> GND

const int LEDS[4] = {0, 1, 2, 3};

// =========================
// CONFIGURACAO DO WIFI
// =========================
// Neste codigo, o ESP32 funciona como Access Point,
// ou seja, cria uma rede Wi-Fi propria sem Internet.

const char* AP_SSID = "Calculadora_ESP32";
const char* AP_PASSWORD = "12345678";  // minimo de 8 caracteres

WebServer server(80);

// =========================
// FRONTEND: HTML + CSS + JS
// =========================
// O JavaScript NAO calcula a soma/subtracao.
// Ele apenas envia A, B e operacao para o ESP32 pela rota /calc.

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <title>Calculadora Binaria ESP32-C3</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      margin: 40px auto;
      max-width: 700px;
      padding: 0 16px;
    }

    input {
      width: 180px;
      padding: 10px;
      margin: 10px;
      font-size: 24px;
      text-align: center;
      letter-spacing: 4px;
    }

    button {
      padding: 12px 24px;
      margin: 10px;
      font-size: 20px;
      cursor: pointer;
    }

    .card {
      margin: 20px auto;
      padding: 16px;
      border: 1px solid #ddd;
      border-radius: 10px;
      max-width: 520px;
      text-align: left;
    }

    .ok {
      color: green;
      font-weight: bold;
    }

    .erro {
      color: red;
      font-weight: bold;
    }

    code {
      font-size: 18px;
    }
  </style>
</head>

<body>
  <h1>Calculadora Binaria ESP32-C3</h1>

  <p>
    Digite operandos de 4 bits em complemento de dois.
    Intervalo representavel: <strong>-8 a +7</strong>.
  </p>

  <div>
    <input id="a" maxlength="4" placeholder="A: 0000">
  </div>

  <div>
    <input id="b" maxlength="4" placeholder="B: 0000">
  </div>

  <div>
    <button id="btnAdd" type="button">SOMA</button>
    <button id="btnSub" type="button">SUBTRACAO</button>
  </div>

  <div class="card">
    <h2>Resultado</h2>
    <p>Status: <span id="status">Aguardando entrada</span></p>
    <p>Operacao: <code id="operacao">-</code></p>
    <p>A decimal: <code id="aDec">-</code></p>
    <p>B decimal: <code id="bDec">-</code></p>
    <p>Resultado decimal completo: <code id="resultadoDecimalCompleto">-</code></p>
    <p>Resultado armazenado em 4 bits: <code id="resultadoBinario">----</code></p>
    <p>Resultado interpretado em complemento de dois: <code id="resultadoDecimal4Bits">-</code></p>
    <p>Overflow: <code id="overflow">-</code></p>
  </div>

  <script>
    function isBinary4(value) {
      return /^[01]{4}$/.test(value);
    }

    function setStatus(message, isError) {
      const status = document.getElementById("status");
      status.textContent = message;
      status.className = isError ? "erro" : "ok";
    }

    function sendOperation(op) {
      const a = document.getElementById("a").value.trim();
      const b = document.getElementById("b").value.trim();

      if (!isBinary4(a) || !isBinary4(b)) {
        setStatus("Digite exatamente 4 bits binarios em A e B.", true);
        return;
      }

      const url = "/calc?a=" + encodeURIComponent(a) +
                  "&b=" + encodeURIComponent(b) +
                  "&op=" + encodeURIComponent(op);

      fetch(url)
        .then(function(response) {
          if (!response.ok) {
            throw new Error("Erro HTTP: " + response.status);
          }
          return response.json();
        })
        .then(function(data) {
          setStatus("Calculo realizado pelo ESP32.", false);

          document.getElementById("operacao").textContent = data.operacao;
          document.getElementById("aDec").textContent = data.aDecimal;
          document.getElementById("bDec").textContent = data.bDecimal;
          document.getElementById("resultadoDecimalCompleto").textContent = data.resultadoDecimalCompleto;
          document.getElementById("resultadoBinario").textContent = data.resultadoBinario4Bits;
          document.getElementById("resultadoDecimal4Bits").textContent = data.resultadoDecimal4Bits;
          document.getElementById("overflow").textContent = data.overflow ? "SIM" : "NAO";
        })
        .catch(function(err) {
          setStatus("Falha na requisicao: " + err.message, true);
        });
    }

    document.addEventListener("DOMContentLoaded", function() {
      document.getElementById("btnAdd").addEventListener("click", function() {
        sendOperation("add");
      });

      document.getElementById("btnSub").addEventListener("click", function() {
        sendOperation("sub");
      });
    });
  </script>
</body>
</html>
)rawliteral";

// =========================
// FUNCOES AUXILIARES
// =========================

bool isBinary4(String value) {
  if (value.length() != 4) {
    return false;
  }

  for (int i = 0; i < 4; i++) {
    if (value[i] != '0' && value[i] != '1') {
      return false;
    }
  }

  return true;
}

int binary4ToUnsigned(String value) {
  return (int) strtol(value.c_str(), NULL, 2) & 0x0F;
}

int unsigned4ToSigned(int value) {
  value = value & 0x0F;

  if (value >= 8) {
    return value - 16;
  }

  return value;
}

String toBinary4(int value) {
  value = value & 0x0F;

  String output = "";

  for (int i = 3; i >= 0; i--) {
    output += ((value >> i) & 0x01) ? "1" : "0";
  }

  return output;
}

void updateLeds(int value) {
  value = value & 0x0F;

  for (int i = 0; i < 4; i++) {
    digitalWrite(LEDS[i], (value >> i) & 0x01);
  }
}

// =========================
// ROTAS DO SERVIDOR
// =========================

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleCalc() {
  if (!server.hasArg("a") || !server.hasArg("b") || !server.hasArg("op")) {
    server.send(400, "application/json", "{\"erro\":\"Faltam parametros: a, b e op\"}");
    return;
  }

  String paramA = server.arg("a");
  String paramB = server.arg("b");
  String op = server.arg("op");

  if (!isBinary4(paramA) || !isBinary4(paramB)) {
    server.send(400, "application/json", "{\"erro\":\"Operandos devem ter exatamente 4 bits\"}");
    return;
  }

  if (op != "add" && op != "sub") {
    server.send(400, "application/json", "{\"erro\":\"Operacao invalida\"}");
    return;
  }

  int rawA = binary4ToUnsigned(paramA);
  int rawB = binary4ToUnsigned(paramB);

  int valueA = unsigned4ToSigned(rawA);
  int valueB = unsigned4ToSigned(rawB);

  // ==========================================================
  // BACKEND: SOMA E SUBTRACAO FEITAS NO ESP32, EM C/C++
  // ==========================================================
  int resultFull;

  if (op == "add") {
    resultFull = valueA + valueB;
  } else {
    resultFull = valueA - valueB;
  }

  // Em 4 bits com sinal, o intervalo representavel e de -8 a +7.
  bool overflow = (resultFull < -8 || resultFull > 7);

  // Mantem apenas os 4 bits menos significativos,
  // como ocorreria em um registrador de 4 bits.
  int resultRaw4Bits = resultFull & 0x0F;
  int resultSigned4Bits = unsigned4ToSigned(resultRaw4Bits);

  updateLeds(resultRaw4Bits);

  String operationName = (op == "add") ? "soma" : "subtracao";

  String json = "{";
  json += "\"operacao\":\"" + operationName + "\",";
  json += "\"aBinario\":\"" + paramA + "\",";
  json += "\"bBinario\":\"" + paramB + "\",";
  json += "\"aDecimal\":" + String(valueA) + ",";
  json += "\"bDecimal\":" + String(valueB) + ",";
  json += "\"resultadoDecimalCompleto\":" + String(resultFull) + ",";
  json += "\"resultadoBinario4Bits\":\"" + toBinary4(resultRaw4Bits) + "\",";
  json += "\"resultadoDecimal4Bits\":" + String(resultSigned4Bits) + ",";
  json += "\"overflow\":" + String(overflow ? "true" : "false");
  json += "}";

  Serial.println();
  Serial.println("===== NOVA REQUISICAO =====");

  Serial.print("A binario: ");
  Serial.print(paramA);
  Serial.print(" | A decimal: ");
  Serial.println(valueA);

  Serial.print("B binario: ");
  Serial.print(paramB);
  Serial.print(" | B decimal: ");
  Serial.println(valueB);

  Serial.print("Operacao: ");
  Serial.println(operationName);

  Serial.print("Resultado completo: ");
  Serial.println(resultFull);

  Serial.print("Resultado 4 bits: ");
  Serial.print(toBinary4(resultRaw4Bits));
  Serial.print(" | decimal em complemento de dois: ");
  Serial.println(resultSigned4Bits);

  Serial.print("Overflow: ");
  Serial.println(overflow ? "SIM" : "NAO");

  Serial.println("===========================");

  server.send(200, "application/json", json);
}

// =========================
// SETUP E LOOP
// =========================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("--- Iniciando Calculadora Binaria ESP32-C3 ---");

  // Configura os GPIOs dos LEDs como saida
  for (int i = 0; i < 4; i++) {
    pinMode(LEDS[i], OUTPUT);
    digitalWrite(LEDS[i], LOW);
  }

  // Configura o ESP32 como Access Point
  WiFi.mode(WIFI_AP);

  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);

  if (apStarted) {
    Serial.println("Access Point iniciado com sucesso.");
  } else {
    Serial.println("ERRO: Falha ao iniciar o Access Point.");
  }

  IPAddress apIP = WiFi.softAPIP();

  Serial.println();
  Serial.println("===== DADOS DA REDE =====");
  Serial.print("Nome da rede Wi-Fi: ");
  Serial.println(AP_SSID);
  Serial.print("Senha da rede Wi-Fi: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP do ESP32: ");
  Serial.println(apIP);
  Serial.println("=========================");

  // Configura as rotas do servidor
  server.on("/", handleRoot);
  server.on("/calc", handleCalc);

  server.begin();

  Serial.println();
  Serial.println("Servidor HTTP iniciado.");
  Serial.println();
  Serial.println("===== LINK DE ACESSO =====");
  Serial.print("Conecte seu computador/celular na rede ");
  Serial.println(AP_SSID);
  Serial.print("Depois acesse no navegador: http://");
  Serial.println(apIP);
  Serial.println("==========================");
}

void loop() {
  server.handleClient();
}