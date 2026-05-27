// ======================================================
// Calculadora binaria em complemento de um
// Entrada e saida exclusivamente pela Serial
// ESP32 / Arduino IDE
// ======================================================

const int N_BITS = 4;
const int MASK_4_BITS = 0x0F;

String linhaEntrada = "";

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
  return (int) strtol(value.c_str(), NULL, 2) & MASK_4_BITS;
}

String toBinary4(int value) {
  value = value & MASK_4_BITS;

  String output = "";

  for (int i = 3; i >= 0; i--) {
    output += ((value >> i) & 0x01) ? "1" : "0";
  }

  return output;
}

// Interpreta um numero de 4 bits em complemento de um
int unsigned4ToOnesComplementSigned(int value) {
  value = value & MASK_4_BITS;

  // Se o bit mais significativo for 0, o numero e positivo
  if ((value & 0b1000) == 0) {
    return value;
  }

  // Se for 1111, representa -0 em complemento de um
  if (value == 0b1111) {
    return 0;
  }

  // Para negativos, inverte os bits para descobrir a magnitude
  int magnitude = (~value) & MASK_4_BITS;

  return -magnitude;
}

// Faz o complemento de um de um valor de 4 bits
int onesComplement(int value) {
  return (~value) & MASK_4_BITS;
}

// Soma em complemento de um usando end-around carry
int onesComplementAdd(int a, int b) {
  int result = a + b;

  // Se passou de 4 bits, o carry volta para o bit menos significativo
  while (result > MASK_4_BITS) {
    int carry = result >> N_BITS;
    result = (result & MASK_4_BITS) + carry;
  }

  return result & MASK_4_BITS;
}

// =========================
// PROCESSAMENTO DA OPERACAO
// =========================

void processarOperacao(String linha) {
  linha.trim();

  // Permite tanto "0101+0011" quanto "0101 + 0011"
  linha.replace(" ", "");
  linha.replace("\t", "");

  if (linha.length() != 9) {
    Serial.println();
    Serial.println("ERRO: formato invalido.");
    Serial.println("Use o formato:");
    Serial.println("AAAA+BBBB");
    Serial.println("AAAA-BBBB");
    Serial.println();
    Serial.println("Exemplos:");
    Serial.println("0101+0011");
    Serial.println("0101-0011");
    Serial.println();
    return;
  }

  String paramA = linha.substring(0, 4);
  char operacao = linha[4];
  String paramB = linha.substring(5, 9);

  if (!isBinary4(paramA) || !isBinary4(paramB)) {
    Serial.println();
    Serial.println("ERRO: operandos invalidos.");
    Serial.println("Cada operando deve ter exatamente 4 bits, usando apenas 0 e 1.");
    Serial.println();
    return;
  }

  if (operacao != '+' && operacao != '-') {
    Serial.println();
    Serial.println("ERRO: operacao invalida.");
    Serial.println("Use apenas + ou -.");
    Serial.println();
    return;
  }

  int rawA = binary4ToUnsigned(paramA);
  int rawB = binary4ToUnsigned(paramB);

  int valueA = unsigned4ToOnesComplementSigned(rawA);
  int valueB = unsigned4ToOnesComplementSigned(rawB);

  int resultRaw4Bits;

  if (operacao == '+') {
    resultRaw4Bits = onesComplementAdd(rawA, rawB);
  } else {
    // A - B em complemento de um equivale a:
    // A + complemento_de_um(B)
    int negativeB = onesComplement(rawB);
    resultRaw4Bits = onesComplementAdd(rawA, negativeB);
  }

  int resultSigned4Bits = unsigned4ToOnesComplementSigned(resultRaw4Bits);

  int resultFull;

  if (operacao == '+') {
    resultFull = valueA + valueB;
  } else {
    resultFull = valueA - valueB;
  }

  // Em complemento de um com 4 bits, o intervalo representavel e -7 ate +7
  bool overflow = resultFull < -7 || resultFull > 7;

  Serial.println();
  Serial.println("===== NOVA OPERACAO =====");

  Serial.print("Entrada: ");
  Serial.print(paramA);
  Serial.print(" ");
  Serial.print(operacao);
  Serial.print(" ");
  Serial.println(paramB);

  Serial.println();

  Serial.print("A binario: ");
  Serial.print(paramA);
  Serial.print(" | A decimal em complemento de um: ");
  Serial.println(valueA);

  Serial.print("B binario: ");
  Serial.print(paramB);
  Serial.print(" | B decimal em complemento de um: ");
  Serial.println(valueB);

  Serial.print("Operacao: ");
  if (operacao == '+') {
    Serial.println("soma");
  } else {
    Serial.println("subtracao");
  }

  Serial.println();

  Serial.print("Resultado decimal completo: ");
  Serial.println(resultFull);

  Serial.print("Resultado armazenado em 4 bits: ");
  Serial.println(toBinary4(resultRaw4Bits));

  Serial.print("Resultado interpretado em complemento de um: ");
  Serial.println(resultSigned4Bits);

  if (resultRaw4Bits == 0b1111) {
    Serial.println("Observacao: 1111 representa -0 em complemento de um.");
  }

  Serial.print("Overflow: ");
  Serial.println(overflow ? "SIM" : "NAO");

  Serial.println("=========================");
  Serial.println();

  Serial.println("Digite outra operacao:");
}

// =========================
// SETUP E LOOP
// =========================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("--- Calculadora Binaria ESP32 ---");
  Serial.println("--- Aritmetica em complemento de um ---");
  Serial.println("--- Entrada e saida exclusivamente pela Serial ---");
  Serial.println();

  Serial.println("Formato da entrada:");
  Serial.println("AAAA+BBBB");
  Serial.println("AAAA-BBBB");
  Serial.println();

  Serial.println("Exemplos:");
  Serial.println("0101+0011");
  Serial.println("0101-0011");
  Serial.println("1101+0010");
  Serial.println();

  Serial.println("Representacao em complemento de um com 4 bits:");
  Serial.println("0111 = +7");
  Serial.println("0001 = +1");
  Serial.println("0000 = +0");
  Serial.println("1111 = -0");
  Serial.println("1110 = -1");
  Serial.println("1000 = -7");
  Serial.println();

  Serial.println("Intervalo representavel: -7 ate +7");
  Serial.println();
  Serial.println("Digite uma operacao:");
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (linhaEntrada.length() > 0) {
        processarOperacao(linhaEntrada);
        linhaEntrada = "";
      }
    } else {
      linhaEntrada += c;
    }
  }
}