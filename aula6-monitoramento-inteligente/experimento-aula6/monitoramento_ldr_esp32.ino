#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ==========================================================
// CONFIGURACOES PRINCIPAIS
// ==========================================================
// Projeto para ESP32-C3 fisico.
//
// LDR:
// Monte um divisor de tensao e conecte o ponto central ao GPIO3.
// Exemplo:
// 3V3 -> LDR -> GPIO3 -> resistor de 10 kohm -> GND
//
// LED built-in:
// Na placa usada nas aulas, o LED RGB onboard e um NeoPixel no GPIO8.
//
// Botao SOS:
// GPIO2 -> botao -> GND
// O pull-up interno mantem o pino em HIGH quando o botao esta solto.

const int PINO_LDR_ADC = 3;
const int PINO_BOTAO_SOS = 2;
const int PINO_LED_RGB = 8;
const int NUMERO_LEDS_RGB = 1;

const int RESOLUCAO_ADC_BITS = 12;
const int VALOR_ADC_MAXIMO = (1 << RESOLUCAO_ADC_BITS) - 1;

// Com o divisor sugerido acima, valores menores indicam menor luminosidade.
// Ajuste este limiar apos observar as leituras no monitor serial ou na pagina.
const int LIMIAR_BAIXA_LUMINOSIDADE = 1400;

const unsigned long INTERVALO_LEITURA_LDR_MS = 1000;
const unsigned long INTERVALO_PISCA_LED_MS = 2000;
const unsigned long TEMPO_LED_VERMELHO_SOS_MS = 3000;
const unsigned long INTERVALO_DEBOUNCE_BOTAO_MS = 60;

// ==========================================================
// CONFIGURACAO DO WI-FI
// ==========================================================

const char* AP_SSID = "Monitoramento_LDR_ESP32_GrupoE";
const char* AP_PASSWORD = "12345678";

WebServer server(80);
Adafruit_NeoPixel ledRgb(NUMERO_LEDS_RGB, PINO_LED_RGB, NEO_GRB + NEO_KHZ800);

int valorLdrAdc = 0;
int luminosidadePercentual = 0;
bool baixaLuminosidade = false;
bool ledAmareloLigado = false;
bool sosAtivo = false;
bool ledVermelhoSosLigado = false;

unsigned long ultimaLeituraLdrMs = 0;
unsigned long ultimaAlternanciaLedMs = 0;
unsigned long inicioSosMs = 0;
unsigned long ultimoSosAceitoMs = 0;

volatile bool interrupcaoSosPendente = false;

// ==========================================================
// PAGINA HTML
// ==========================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Monitoramento LDR ESP32-C3</title>

  <style>
    body {
      max-width: 760px;
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

    .valor-principal {
      font-size: 46px;
      font-weight: bold;
      line-height: 1.1;
      margin: 10px 0;
    }

    .barra {
      width: 100%;
      height: 18px;
      overflow: hidden;
      border-radius: 8px;
      background: #d9e2ec;
    }

    .preenchimento {
      width: 0%;
      height: 100%;
      background: #2f80ed;
      transition: width .25s ease;
    }

    code {
      font-size: 16px;
    }

    .status-ok {
      color: #1b7f3a;
      font-weight: bold;
    }

    .status-alerta {
      color: #b45309;
      font-weight: bold;
    }

    .status-erro {
      color: #b42318;
      font-weight: bold;
    }

    .status-sos {
      color: #b42318;
      font-weight: bold;
    }
  </style>
</head>

<body>
  <h1>Monitoramento LDR ESP32-C3</h1>

  <p class="subtitulo">
    Leitura analogica do sensor LDR via ADC e servidor web local.
  </p>

  <div class="card">
    <h2>Luminosidade</h2>

    <div class="valor-principal">
      <span id="luminosidadePercentual">--</span>%
    </div>

    <div class="barra">
      <div id="barraLuminosidade" class="preenchimento"></div>
    </div>

    <p>
      ADC:
      <code><span id="valorAdc">--</span></code>
    </p>

    <p>
      Condicao:
      <span id="condicao">Aguardando leitura...</span>
    </p>

    <p>
      SOS:
      <span id="sos">Aguardando acionamento...</span>
    </p>

    <p>
      Ultima atualizacao:
      <code><span id="ultimaAtualizacao">--</span></code>
    </p>
  </div>

  <script>
    function atualizarClasseCondicao(baixaLuminosidade) {
      const elemento = document.getElementById("condicao");

      elemento.className = baixaLuminosidade
        ? "status-alerta"
        : "status-ok";
    }

    function atualizarSos(dados) {
      const elemento = document.getElementById("sos");

      elemento.textContent = dados.sosAtivo
        ? "Ativo - LED vermelho com prioridade"
        : "Inativo";

      elemento.className = dados.sosAtivo
        ? "status-sos"
        : "status-ok";
    }

    function preencherLeitura(dados) {
      document.getElementById("luminosidadePercentual").textContent =
        dados.luminosidadePercentual;

      document.getElementById("barraLuminosidade").style.width =
        `${dados.luminosidadePercentual}%`;

      document.getElementById("valorAdc").textContent =
        `${dados.valorLdrAdc}/${dados.valorAdcMaximo}`;

      document.getElementById("condicao").textContent =
        dados.baixaLuminosidade
          ? "Baixa luminosidade - LED amarelo piscando"
          : "Luminosidade normal";

      document.getElementById("ultimaAtualizacao").textContent =
        new Date().toLocaleTimeString();

      atualizarClasseCondicao(dados.baixaLuminosidade);
      atualizarSos(dados);
    }

    async function carregarLeitura() {
      try {
        const resposta = await fetch("/ldr");
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(dados.erro || "Falha na leitura.");
        }

        preencherLeitura(dados);
      } catch (erro) {
        const condicao = document.getElementById("condicao");
        condicao.textContent = "Erro: " + erro.message;
        condicao.className = "status-erro";
      }
    }

    document.addEventListener("DOMContentLoaded", () => {
      carregarLeitura();
      setInterval(carregarLeitura, 1000);
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

void apagarLedRgb() {
  ledRgb.setPixelColor(0, ledRgb.Color(0, 0, 0));
  ledRgb.show();
}

void escreverLedVermelho() {
  ledRgb.setPixelColor(0, ledRgb.Color(255, 0, 0));
  ledRgb.show();
}

void escreverLedAmarelo() {
  ledRgb.setPixelColor(0, ledRgb.Color(255, 200, 0));
  ledRgb.show();
}

void IRAM_ATTR tratarInterrupcaoSos() {
  interrupcaoSosPendente = true;
}

void atualizarLeituraLdr() {
  valorLdrAdc = analogRead(PINO_LDR_ADC);

  luminosidadePercentual = map(
    valorLdrAdc,
    0,
    VALOR_ADC_MAXIMO,
    0,
    100
  );

  luminosidadePercentual = limitarInteiro(
    luminosidadePercentual,
    0,
    100
  );

  baixaLuminosidade = valorLdrAdc < LIMIAR_BAIXA_LUMINOSIDADE;

  Serial.printf(
    "LDR GPIO %d: adc=%d/%d | luminosidade=%d%% | baixa=%s\n",
    PINO_LDR_ADC,
    valorLdrAdc,
    VALOR_ADC_MAXIMO,
    luminosidadePercentual,
    baixaLuminosidade ? "sim" : "nao"
  );
}

void atualizarSensorPeriodicamente() {
  const unsigned long agora = millis();

  if (
    ultimaLeituraLdrMs == 0 ||
    agora - ultimaLeituraLdrMs >= INTERVALO_LEITURA_LDR_MS
  ) {
    ultimaLeituraLdrMs = agora;
    atualizarLeituraLdr();
  }
}

void atualizarPiscaLedBaixaLuminosidade() {
  const unsigned long agora = millis();

  if (sosAtivo) {
    ledVermelhoSosLigado = true;
    escreverLedVermelho();
    return;
  }

  if (!baixaLuminosidade) {
    ledAmareloLigado = false;
    ledVermelhoSosLigado = false;
    apagarLedRgb();
    ultimaAlternanciaLedMs = agora;
    return;
  }

  if (
    ultimaAlternanciaLedMs == 0 ||
    agora - ultimaAlternanciaLedMs >= INTERVALO_PISCA_LED_MS
  ) {
    ultimaAlternanciaLedMs = agora;
    ledAmareloLigado = !ledAmareloLigado;

    if (ledAmareloLigado) {
      escreverLedAmarelo();
    } else {
      apagarLedRgb();
    }
  }
}

void acionarSos() {
  sosAtivo = true;
  ledAmareloLigado = false;
  ledVermelhoSosLigado = true;
  inicioSosMs = millis();
  ultimaAlternanciaLedMs = inicioSosMs;

  escreverLedVermelho();

  Serial.println(
    "SOS acionado: LED built-in vermelho por 3 segundos."
  );
}

void tratarBotaoSosComDebounce() {
  if (!interrupcaoSosPendente) {
    return;
  }

  interrupcaoSosPendente = false;

  const unsigned long agora = millis();

  if (agora - ultimoSosAceitoMs < INTERVALO_DEBOUNCE_BOTAO_MS) {
    return;
  }

  if (digitalRead(PINO_BOTAO_SOS) != LOW) {
    return;
  }

  ultimoSosAceitoMs = agora;
  acionarSos();
}

void atualizarEstadoSos() {
  if (!sosAtivo) {
    return;
  }

  if (millis() - inicioSosMs >= TEMPO_LED_VERMELHO_SOS_MS) {
    sosAtivo = false;
    ledVermelhoSosLigado = false;
    ledAmareloLigado = false;
    ultimaAlternanciaLedMs = 0;

    apagarLedRgb();

    Serial.println(
      "SOS finalizado: retornando as condicoes normais."
    );
  }
}

// ==========================================================
// GERACAO DO JSON
// ==========================================================

String estadoAtualJson() {
  String json = "{";

  json += "\"pinoLdrAdc\":" +
          String(PINO_LDR_ADC) + ",";

  json += "\"pinoBotaoSos\":" +
          String(PINO_BOTAO_SOS) + ",";

  json += "\"valorLdrAdc\":" +
          String(valorLdrAdc) + ",";

  json += "\"valorAdcMaximo\":" +
          String(VALOR_ADC_MAXIMO) + ",";

  json += "\"luminosidadePercentual\":" +
          String(luminosidadePercentual) + ",";

  json += "\"limiarBaixaLuminosidade\":" +
          String(LIMIAR_BAIXA_LUMINOSIDADE) + ",";

  json += "\"baixaLuminosidade\":";
  json += baixaLuminosidade ? "true" : "false";
  json += ",";

  json += "\"ledAmareloLigado\":";
  json += ledAmareloLigado ? "true" : "false";
  json += ",";

  json += "\"sosAtivo\":";
  json += sosAtivo ? "true" : "false";
  json += ",";

  json += "\"ledVermelhoSosLigado\":";
  json += ledVermelhoSosLigado ? "true" : "false";
  json += ",";

  json += "\"tempoLedVermelhoSosMs\":";
  json += String(TEMPO_LED_VERMELHO_SOS_MS);
  json += ",";

  json += "\"debounceBotaoMs\":";
  json += String(INTERVALO_DEBOUNCE_BOTAO_MS);

  json += "}";

  return json;
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

void responderLeituraLdr() {
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
    "Inicializando monitoramento LDR no ESP32-C3."
  );

  analogReadResolution(RESOLUCAO_ADC_BITS);
  pinMode(PINO_LDR_ADC, INPUT);
  pinMode(PINO_BOTAO_SOS, INPUT_PULLUP);

  ledRgb.begin();
  ledRgb.setBrightness(70);
  apagarLedRgb();

  attachInterrupt(
    digitalPinToInterrupt(PINO_BOTAO_SOS),
    tratarInterrupcaoSos,
    FALLING
  );

  atualizarLeituraLdr();

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
    "/ldr",
    HTTP_GET,
    responderLeituraLdr
  );

  server.on(
    "/config",
    HTTP_GET,
    responderLeituraLdr
  );

  server.begin();

  Serial.println("Servidor HTTP iniciado.");
}

void loop() {
  server.handleClient();
  atualizarSensorPeriodicamente();
  tratarBotaoSosComDebounce();
  atualizarEstadoSos();
  atualizarPiscaLedBaixaLuminosidade();

  delay(2);
}
