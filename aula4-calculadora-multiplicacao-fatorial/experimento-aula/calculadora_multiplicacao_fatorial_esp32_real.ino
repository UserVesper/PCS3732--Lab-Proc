#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdint.h>
#include <stdio.h>

// ==========================================================
// CONFIGURACOES PRINCIPAIS
// ==========================================================

// Altere somente esta constante para testar entradas com
// diferentes quantidades de bits.
//
// Exemplos:
// 4 bits -> intervalo de -8 ate +7
// 5 bits -> intervalo de -16 ate +15
// 6 bits -> intervalo de -32 ate +31
const int NUM_BITS_ENTRADA = 4;

// A montagem fisica continua usando exatamente quatro LEDs.
// Os LEDs exibem somente os quatro bits menos significativos
// do resultado.
const int NUM_LEDS = 4;

// LEDS[0] representa o bit menos significativo.
const int LEDS[NUM_LEDS] = {0, 1, 2, 3};

// O limite de 31 bits garante que soma, subtracao e
// multiplicacao caibam com seguranca em variaveis int64_t.
static_assert(
  NUM_BITS_ENTRADA >= 2 && NUM_BITS_ENTRADA <= 31,
  "NUM_BITS_ENTRADA deve estar entre 2 e 31."
);

// ==========================================================
// CONFIGURACAO DO WI-FI PARA O ESP32 REAL
// ==========================================================
//
// Assim como no codigo utilizado durante a aula, o ESP32
// funciona como Access Point: ele cria uma rede Wi-Fi propria,
// sem depender de uma rede externa ou de conexao com a Internet.

const char* AP_SSID = "Calculadora_ESP32";
const char* AP_PASSWORD = "12345678";  // minimo de 8 caracteres

WebServer server(80);

// ==========================================================
// PAGINA HTML
// ==========================================================
//
// O JavaScript nao realiza operacoes matematicas.
// Ele apenas:
// 1. consulta a configuracao atual do ESP32;
// 2. valida o tamanho das entradas;
// 3. envia uma requisicao HTTP;
// 4. exibe a resposta calculada pelo ESP32.

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <title>Calculadora Binaria ESP32</title>

  <style>
    body {
      max-width: 880px;
      margin: 30px auto;
      padding: 0 18px;
      font-family: Arial, sans-serif;
      background-color: #f4f4f4;
    }

    h1 {
      text-align: center;
    }

    .card {
      margin-top: 18px;
      padding: 18px;
      border-radius: 10px;
      background-color: white;
      box-shadow: 0 2px 7px rgba(0, 0, 0, 0.15);
    }

    .inputs,
    .buttons {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: 12px;
      margin-top: 14px;
    }

    input {
      width: 230px;
      padding: 10px;
      font-size: 20px;
      text-align: center;
      letter-spacing: 4px;
    }

    button {
      padding: 11px 15px;
      border: none;
      border-radius: 6px;
      cursor: pointer;
      color: white;
      background-color: #1565c0;
      font-weight: bold;
    }

    button:hover {
      background-color: #0d47a1;
    }

    code {
      font-size: 17px;
    }

    .ok {
      color: green;
      font-weight: bold;
    }

    .erro {
      color: red;
      font-weight: bold;
    }
  </style>
</head>

<body>
  <h1>Calculadora Binaria ESP32-C3</h1>

  <div class="card">
    <p>
      Quantidade configurada de bits por entrada:
      <strong><span id="numeroBitsEntrada">-</span> bits</strong>.
    </p>

    <p>
      Intervalo permitido em complemento de dois:
      <strong>
        <span id="limiteNegativo">-</span>
        ate
        <span id="limitePositivo">-</span>
      </strong>.
    </p>

    <p>
      Quantidade de LEDs fisicos:
      <strong><span id="numeroLeds">-</span></strong>.
    </p>

    <p>
      Para calcular o fatorial, somente o operando A sera utilizado.
      Fatoriais acima de 20 nao sao aceitos para evitar overflow
      da variavel utilizada internamente.
    </p>

    <div class="inputs">
      <input id="operandoA" placeholder="Carregando configuracao...">
      <input id="operandoB" placeholder="Carregando configuracao...">
    </div>

    <div class="buttons">
      <button id="btnSoma">SOMA</button>
      <button id="btnSubtracao">SUBTRACAO</button>
      <button id="btnMultiplicacao">MULTIPLICACAO</button>
      <button id="btnFatorial">FATORIAL DE A</button>
    </div>
  </div>

  <div class="card">
    <h2>Resultado</h2>

    <p>
      Status:
      <span id="status">Carregando configuracao...</span>
    </p>

    <p>
      Operacao:
      <code id="operacao">-</code>
    </p>

    <p>
      Valor decimal de A:
      <code id="valorA">-</code>
    </p>

    <p>
      Valor decimal de B:
      <code id="valorB">-</code>
    </p>

    <p>
      Resultado decimal completo:
      <code id="resultadoDecimal">-</code>
    </p>

    <p>
      Resultado binario em complemento de dois:
      <code id="resultadoBinarioComplementoDeDois">-</code>
    </p>

    <p>
      O resultado ultrapassou o intervalo das entradas?
      <code id="overflowEntrada">-</code>
    </p>
  </div>

  <script>
    let numeroBitsEntrada = 4;
    let numeroLeds = 4;

    function atualizarStatus(texto, erro) {
      const elemento = document.getElementById("status");

      elemento.textContent = texto;
      elemento.className = erro ? "erro" : "ok";
    }

    function criarSequenciaDeZeros(tamanho) {
      return "0".repeat(tamanho);
    }

    function possuiQuantidadeCorretaDeBits(valor) {
      const expressao =
        new RegExp("^[01]{" + numeroBitsEntrada + "}$");

      return expressao.test(valor);
    }

    async function carregarConfiguracao() {
      try {
        const resposta = await fetch("/config");
        const configuracao = await resposta.json();

        if (!resposta.ok) {
          throw new Error(
            "Nao foi possivel carregar a configuracao."
          );
        }

        numeroBitsEntrada = configuracao.numeroBitsEntrada;
        numeroLeds = configuracao.numeroLeds;

        document
          .getElementById("numeroBitsEntrada")
          .textContent = numeroBitsEntrada;

        document
          .getElementById("numeroLeds")
          .textContent = numeroLeds;

        document
          .getElementById("limiteNegativo")
          .textContent = configuracao.limiteNegativo;

        document
          .getElementById("limitePositivo")
          .textContent = configuracao.limitePositivo;

        const operandoA =
          document.getElementById("operandoA");

        const operandoB =
          document.getElementById("operandoB");

        operandoA.maxLength = numeroBitsEntrada;
        operandoB.maxLength = numeroBitsEntrada;

        operandoA.placeholder =
          "A: " + criarSequenciaDeZeros(numeroBitsEntrada);

        operandoB.placeholder =
          "B: " + criarSequenciaDeZeros(numeroBitsEntrada);

        atualizarStatus("Configuracao carregada.", false);
      } catch (erro) {
        atualizarStatus(
          "Erro ao carregar configuracao: " + erro.message,
          true
        );
      }
    }

    async function enviarOperacao(operacao) {
      const operandoA =
        document.getElementById("operandoA").value.trim();

      const operandoBDigitado =
        document.getElementById("operandoB").value.trim();

      const utilizaOperandoB =
        operacao !== "fact";

      const operandoB =
        utilizaOperandoB
          ? operandoBDigitado
          : criarSequenciaDeZeros(numeroBitsEntrada);

      if (!possuiQuantidadeCorretaDeBits(operandoA)) {
        atualizarStatus(
          "Digite exatamente " +
            numeroBitsEntrada +
            " bits binarios no operando A.",
          true
        );

        return;
      }

      if (
        utilizaOperandoB &&
        !possuiQuantidadeCorretaDeBits(operandoB)
      ) {
        atualizarStatus(
          "Digite exatamente " +
            numeroBitsEntrada +
            " bits binarios no operando B.",
          true
        );

        return;
      }

      try {
        const url =
          "/calc?a=" +
          encodeURIComponent(operandoA) +
          "&b=" +
          encodeURIComponent(operandoB) +
          "&op=" +
          encodeURIComponent(operacao);

        const resposta = await fetch(url);
        const dados = await resposta.json();

        if (!resposta.ok) {
          throw new Error(
            dados.erro || "Erro desconhecido."
          );
        }

        atualizarStatus(
          "Operacao executada pelo ESP32.",
          false
        );

        document
          .getElementById("operacao")
          .textContent = dados.operacao;

        document
          .getElementById("valorA")
          .textContent = dados.valorA;

        document
          .getElementById("valorB")
          .textContent = dados.valorB;

        document
          .getElementById("resultadoDecimal")
          .textContent = dados.resultadoDecimal;

        document
          .getElementById("resultadoBinarioComplementoDeDois")
          .textContent = dados.resultadoBinarioComplementoDeDois;

        document
          .getElementById("overflowEntrada")
          .textContent = dados.overflowEntrada ? "SIM" : "NAO";
      } catch (erro) {
        atualizarStatus(
          "Erro: " + erro.message,
          true
        );
      }
    }

    document.addEventListener(
      "DOMContentLoaded",
      async function () {
        await carregarConfiguracao();

        document
          .getElementById("btnSoma")
          .addEventListener("click", function () {
            enviarOperacao("add");
          });

        document
          .getElementById("btnSubtracao")
          .addEventListener("click", function () {
            enviarOperacao("sub");
          });

        document
          .getElementById("btnMultiplicacao")
          .addEventListener("click", function () {
            enviarOperacao("mul");
          });

        document
          .getElementById("btnFatorial")
          .addEventListener("click", function () {
            enviarOperacao("fact");
          });
      }
    );
  </script>
</body>
</html>
)rawliteral";

// ==========================================================
// FUNCOES AUXILIARES
// ==========================================================

// Quantidade total de combinacoes possiveis para uma entrada.
//
// Exemplo:
// NUM_BITS_ENTRADA = 4
// modulo = 2^4 = 16
uint64_t obterModuloEntrada() {
  return 1ULL << NUM_BITS_ENTRADA;
}

// Mascara utilizada para manter somente NUM_BITS_ENTRADA bits.
//
// Exemplo:
// NUM_BITS_ENTRADA = 4
// mascara = 1111
uint64_t obterMascaraEntrada() {
  return obterModuloEntrada() - 1ULL;
}

// Mascara utilizada para manter somente os bits exibidos
// fisicamente pelos quatro LEDs.
//
// Exemplo:
// NUM_LEDS = 4
// mascara = 1111
uint64_t obterMascaraLeds() {
  return (1ULL << NUM_LEDS) - 1ULL;
}

int64_t obterLimiteNegativoEntrada() {
  return -(1LL << (NUM_BITS_ENTRADA - 1));
}

int64_t obterLimitePositivoEntrada() {
  return (1LL << (NUM_BITS_ENTRADA - 1)) - 1LL;
}

String int64ParaString(int64_t valor) {
  char buffer[32];

  snprintf(
    buffer,
    sizeof(buffer),
    "%lld",
    static_cast<long long>(valor)
  );

  return String(buffer);
}

// Verifica se a entrada possui exatamente a quantidade
// configurada de bits e se todos os caracteres sao validos.
bool possuiQuantidadeCorretaDeBits(const String& valor) {
  if (valor.length() != NUM_BITS_ENTRADA) {
    return false;
  }

  for (int i = 0; i < NUM_BITS_ENTRADA; i++) {
    if (valor[i] != '0' && valor[i] != '1') {
      return false;
    }
  }

  return true;
}

// Converte a String binaria para um numero sem sinal.
uint64_t binarioParaUnsigned(const String& valor) {
  uint64_t resultado = 0;

  for (int i = 0; i < NUM_BITS_ENTRADA; i++) {
    resultado <<= 1;

    if (valor[i] == '1') {
      resultado |= 1ULL;
    }
  }

  return resultado;
}

// Converte a entrada binaria para um numero decimal com sinal,
// interpretando o valor em complemento de dois.
//
// Exemplos com quatro bits:
// 0011 ->  3
// 1101 -> -3
// 1000 -> -8
int64_t binarioParaSigned(const String& valor) {
  uint64_t valorUnsigned =
    binarioParaUnsigned(valor);

  uint64_t bitDeSinal =
    1ULL << (NUM_BITS_ENTRADA - 1);

  if ((valorUnsigned & bitDeSinal) != 0) {
    return static_cast<int64_t>(
      valorUnsigned - obterModuloEntrada()
    );
  }

  return static_cast<int64_t>(valorUnsigned);
}

// Converte o resultado decimal para complemento de dois,
// utilizando exatamente NUM_BITS_ENTRADA bits.
//
// Se o resultado ultrapassar o intervalo permitido, somente
// os NUM_BITS_ENTRADA bits menos significativos sao exibidos.
// Nesse caso, a interface tambem informa que houve overflow.
//
// Exemplos com quatro bits:
//   5 -> 0101
//  -3 -> 1101
//   8 -> 1000, com overflow
//  -9 -> 0111, com overflow
String decimalParaBinarioComplementoDeDois(int64_t valor) {
  uint64_t valorMascarado =
    static_cast<uint64_t>(valor) &
    obterMascaraEntrada();

  String binario = "";

  for (
    int bit = NUM_BITS_ENTRADA - 1;
    bit >= 0;
    bit--
  ) {
    if (((valorMascarado >> bit) & 1ULL) != 0) {
      binario += "1";
    } else {
      binario += "0";
    }
  }

  return binario;
}

// ==========================================================
// CONTROLE DOS LEDS
// ==========================================================

// Os LEDs exibem somente os quatro bits menos significativos
// do resultado, independentemente de NUM_BITS_ENTRADA.
void atualizarLeds(int64_t valor) {
  uint64_t valorMascarado =
    static_cast<uint64_t>(valor) &
    obterMascaraLeds();

  for (int bit = 0; bit < NUM_LEDS; bit++) {
    int estado =
      static_cast<int>(
        (valorMascarado >> bit) & 1ULL
      );

    digitalWrite(LEDS[bit], estado);
  }
}

// Teste realizado automaticamente na inicializacao.
// Cada LED acende individualmente.
void testarLeds() {
  Serial.println(
    "Iniciando teste individual dos LEDs."
  );

  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(LEDS[i], HIGH);
    delay(300);

    digitalWrite(LEDS[i], LOW);
    delay(150);
  }

  Serial.println(
    "Teste dos LEDs concluido."
  );
}

// ==========================================================
// OPERACOES EXECUTADAS NO ESP32
// ==========================================================

// Multiplicacao por somas sucessivas.
//
// O operador * nao e utilizado para realizar a operacao
// principal. Isso permite observar explicitamente as
// iteracoes da multiplicacao.
int64_t multiplicar(
  int64_t operandoA,
  int64_t operandoB
) {
  bool resultadoNegativo =
    (operandoA < 0) != (operandoB < 0);

  int64_t multiplicando =
    operandoA < 0 ? -operandoA : operandoA;

  int64_t multiplicador =
    operandoB < 0 ? -operandoB : operandoB;

  int64_t resultado = 0;

  while (multiplicador > 0) {
    resultado += multiplicando;
    multiplicador--;
  }

  if (resultadoNegativo) {
    return -resultado;
  }

  return resultado;
}

// Calcula o fatorial por meio de multiplicacoes sucessivas.
//
// O fatorial utiliza somente o operando A.
// 20! ainda cabe em uma variavel int64_t.
// 21! ultrapassaria o limite.
int64_t calcularFatorial(
  int64_t valor,
  bool& operacaoValida,
  String& mensagemErro
) {
  if (valor < 0) {
    operacaoValida = false;

    mensagemErro =
      "Nao e possivel calcular o fatorial "
      "de um numero negativo.";

    return 0;
  }

  if (valor > 20) {
    operacaoValida = false;

    mensagemErro =
      "O fatorial foi limitado a 20 "
      "para evitar overflow interno.";

    return 0;
  }

  operacaoValida = true;

  int64_t resultado = 1;

  for (
    int64_t fator = 2;
    fator <= valor;
    fator++
  ) {
    resultado =
      multiplicar(resultado, fator);
  }

  return resultado;
}

// Seleciona a operacao solicitada pela interface.
bool calcular(
  const String& operacao,
  int64_t valorA,
  int64_t valorB,
  int64_t& resultado,
  String& mensagemErro
) {
  if (operacao == "add") {
    resultado = valorA + valorB;
    return true;
  }

  if (operacao == "sub") {
    resultado = valorA - valorB;
    return true;
  }

  if (operacao == "mul") {
    resultado = multiplicar(valorA, valorB);
    return true;
  }

  if (operacao == "fact") {
    bool operacaoValida = true;

    resultado =
      calcularFatorial(
        valorA,
        operacaoValida,
        mensagemErro
      );

    return operacaoValida;
  }

  mensagemErro = "Operacao invalida.";
  return false;
}

String obterNomeDaOperacao(const String& operacao) {
  if (operacao == "add") {
    return "soma";
  }

  if (operacao == "sub") {
    return "subtracao";
  }

  if (operacao == "mul") {
    return "multiplicacao";
  }

  if (operacao == "fact") {
    return "fatorial";
  }

  return "desconhecida";
}

// ==========================================================
// ROTAS DO SERVIDOR HTTP
// ==========================================================

// Exibe a pagina principal.
void responderPaginaPrincipal() {
  server.send_P(
    200,
    "text/html",
    HTML_PAGE
  );
}

// Informa ao frontend a quantidade atual de bits,
// a quantidade de LEDs e os limites das entradas.
void responderConfiguracao() {
  String json = "{";

  json += "\"numeroBitsEntrada\":";
  json += String(NUM_BITS_ENTRADA);
  json += ",";

  json += "\"numeroLeds\":";
  json += String(NUM_LEDS);
  json += ",";

  json += "\"limiteNegativo\":";
  json += int64ParaString(
    obterLimiteNegativoEntrada()
  );
  json += ",";

  json += "\"limitePositivo\":";
  json += int64ParaString(
    obterLimitePositivoEntrada()
  );

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// Recebe os operandos e a operacao por HTTP,
// realiza o calculo e devolve o resultado em JSON.
void responderCalculo() {
  if (
    !server.hasArg("a") ||
    !server.hasArg("b") ||
    !server.hasArg("op")
  ) {
    server.send(
      400,
      "application/json",
      "{\"erro\":\"Parametros ausentes na requisicao.\"}"
    );

    return;
  }

  String operandoABinario =
    server.arg("a");

  String operandoBBinario =
    server.arg("b");

  String operacao =
    server.arg("op");

  if (
    !possuiQuantidadeCorretaDeBits(operandoABinario) ||
    !possuiQuantidadeCorretaDeBits(operandoBBinario)
  ) {
    String mensagemErro =
      "{\"erro\":\"Os operandos devem possuir exatamente " +
      String(NUM_BITS_ENTRADA) +
      " bits.\"}";

    server.send(
      400,
      "application/json",
      mensagemErro
    );

    return;
  }

  int64_t valorA =
    binarioParaSigned(
      operandoABinario
    );

  int64_t valorB =
    binarioParaSigned(
      operandoBBinario
    );

  int64_t resultado = 0;
  String mensagemErro = "";

  // Mede somente o tempo gasto para realizar a operacao.
  // A leitura e a escrita HTTP, a atualizacao dos LEDs e as
  // mensagens do monitor serial nao entram nessa medicao.
  unsigned long inicioOperacaoMicros =
    micros();

  bool sucesso =
    calcular(
      operacao,
      valorA,
      valorB,
      resultado,
      mensagemErro
    );

  unsigned long tempoOperacaoMicros =
    micros() - inicioOperacaoMicros;

  if (!sucesso) {
    server.send(
      400,
      "application/json",
      "{\"erro\":\"" + mensagemErro + "\"}"
    );

    return;
  }

  bool ocorreuOverflowEntrada =
    resultado < obterLimiteNegativoEntrada() ||
    resultado > obterLimitePositivoEntrada();

  // Atualiza os quatro LEDs fisicos com os bits menos
  // significativos do resultado.
  atualizarLeds(resultado);

  String json = "{";

  json += "\"operacao\":\"";
  json += obterNomeDaOperacao(operacao);
  json += "\",";

  json += "\"valorA\":";
  json += int64ParaString(valorA);
  json += ",";

  json += "\"valorB\":";
  json += int64ParaString(valorB);
  json += ",";

  json += "\"resultadoDecimal\":";
  json += int64ParaString(resultado);
  json += ",";

  json += "\"resultadoBinarioComplementoDeDois\":\"";
  json += decimalParaBinarioComplementoDeDois(resultado);
  json += "\",";

  json += "\"overflowEntrada\":";
  json += (
    ocorreuOverflowEntrada
      ? "true"
      : "false"
  );

  json += "}";

  Serial.println();
  Serial.println(
    "==================================="
  );

  Serial.print(
    "Quantidade de bits das entradas: "
  );
  Serial.println(
    NUM_BITS_ENTRADA
  );

  Serial.print(
    "Operacao: "
  );
  Serial.println(
    obterNomeDaOperacao(operacao)
  );

  Serial.print(
    "A: "
  );
  Serial.print(
    operandoABinario
  );
  Serial.print(
    " = "
  );
  Serial.println(
    int64ParaString(valorA)
  );

  Serial.print(
    "B: "
  );
  Serial.print(
    operandoBBinario
  );
  Serial.print(
    " = "
  );
  Serial.println(
    int64ParaString(valorB)
  );

  Serial.print(
    "Resultado decimal completo: "
  );
  Serial.println(
    int64ParaString(resultado)
  );

  Serial.print(
    "Resultado binario em complemento de dois: "
  );
  Serial.println(
    decimalParaBinarioComplementoDeDois(resultado)
  );

  Serial.print(
    "Overflow em relacao as entradas: "
  );
  Serial.println(
    ocorreuOverflowEntrada
      ? "SIM"
      : "NAO"
  );

  Serial.print(
    "Tempo para realizar a operacao: "
  );
  Serial.print(
    tempoOperacaoMicros
  );
  Serial.println(
    " microssegundos"
  );

  server.send(
    200,
    "application/json",
    json
  );
}

// ==========================================================
// SETUP
// ==========================================================

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "Inicializando calculadora binaria no ESP32 real."
  );

  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(
      LEDS[i],
      OUTPUT
    );

    digitalWrite(
      LEDS[i],
      LOW
    );
  }

  testarLeds();

  // Configura o ESP32 real como Access Point.
  // O computador ou celular deve se conectar diretamente
  // a rede criada pelo microcontrolador.
  WiFi.mode(
    WIFI_AP
  );

  bool apIniciado =
    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD
    );

  if (apIniciado) {
    Serial.println(
      "Access Point iniciado com sucesso."
    );
  } else {
    Serial.println(
      "ERRO: falha ao iniciar o Access Point."
    );
  }

  IPAddress ipDoAccessPoint =
    WiFi.softAPIP();

  Serial.println();
  Serial.println(
    "===== DADOS DA REDE ====="
  );

  Serial.print(
    "Nome da rede Wi-Fi: "
  );
  Serial.println(
    AP_SSID
  );

  Serial.print(
    "Senha da rede Wi-Fi: "
  );
  Serial.println(
    AP_PASSWORD
  );

  Serial.print(
    "IP do ESP32: "
  );
  Serial.println(
    ipDoAccessPoint
  );

  Serial.println(
    "========================="
  );

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
    "/calc",
    HTTP_GET,
    responderCalculo
  );

  server.begin();

  Serial.println();
  Serial.println(
    "Servidor HTTP iniciado."
  );

  Serial.println();
  Serial.println(
    "===== LINK DE ACESSO ====="
  );

  Serial.print(
    "Conecte seu computador ou celular na rede "
  );
  Serial.println(
    AP_SSID
  );

  Serial.print(
    "Depois acesse no navegador: http://"
  );
  Serial.println(
    ipDoAccessPoint
  );

  Serial.println(
    "=========================="
  );
}

// ==========================================================
// LOOP
// ==========================================================

void loop() {
  server.handleClient();
  delay(2);
}