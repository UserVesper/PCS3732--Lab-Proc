#!/usr/bin/env python3

"""
Calculadora binaria standalone para Raspberry Pi 3.

Hardware esperado:
  - Teclado matricial 4x4 conectado aos GPIOs.
  - Display LCD 16x2 com adaptador I2C PCF8574.

Uso:
  - A: soma
  - B: subtracao
  - C: multiplicacao
  - D: divisao
  - *: fatorial
  - 0 e 1: bits dos operandos
  - #: limpa/reinicia a operacao atual

As entradas e a saida principal possuem 4 bits em complemento de 2.
Intervalo representavel: -8 ate +7.
"""

from time import sleep


# ==========================================================
# CONFIGURACAO DA CALCULADORA
# ==========================================================

NUM_BITS = 4
MODULO = 1 << NUM_BITS
MASCARA = MODULO - 1
LIMITE_NEGATIVO = -(1 << (NUM_BITS - 1))
LIMITE_POSITIVO = (1 << (NUM_BITS - 1)) - 1


# ==========================================================
# CONFIGURACAO DO TECLADO MATRICIAL
# ==========================================================
#
# Numeracao BCM do Raspberry Pi.
#
# Pinagem seguida do exemplo Freenove "Chapter 21 Matrix Keypad".
#
# Linhas:
#   R1 -> GPIO16 -> pino fisico 36
#   R2 -> GPIO20 -> pino fisico 38
#   R3 -> GPIO21 -> pino fisico 40
#   R4 -> GPIO26 -> pino fisico 37
#
# Colunas:
#   C1 -> GPIO19 -> pino fisico 35
#   C2 -> GPIO13 -> pino fisico 33
#   C3 -> GPIO6  -> pino fisico 31
#   C4 -> GPIO5  -> pino fisico 29

ROWS = 4
COLS = 4
ROW_PINS = [16, 20, 21, 26]
COL_PINS = [19, 13, 6, 5]
KEYPAD_DEBOUNCE_SECONDS = 0.05

KEYMAP = [
    ["1", "2", "3", "A"],
    ["4", "5", "6", "B"],
    ["7", "8", "9", "C"],
    ["*", "0", "#", "D"],
]

OPERACOES = {
    "A": "+",
    "B": "-",
    "C": "*",
    "D": "/",
    "*": "!",
}


# ==========================================================
# CONFIGURACAO DO LCD I2C
# ==========================================================
#
# Interface seguida do exemplo Freenove "Chapter 19 LCD1602".
# Enderecos comuns: PCF8574T = 0x27, PCF8574AT = 0x3F.
# Caso o LCD nao responda, execute no Raspberry Pi:
#   i2cdetect -y 1

I2C_BUS = 1
LCD_I2C_ADDRESS = 0x27
LCD_COLS = 16
LCD_ROWS = 2


# Bits mais comuns do backpack PCF8574 para LCD HD44780.
LCD_RS = 0x01
LCD_RW = 0x02
LCD_ENABLE = 0x04
LCD_BACKLIGHT = 0x08


# ==========================================================
# ARITMETICA BINARIA EM COMPLEMENTO DE 2
# ==========================================================

def binario_para_inteiro_com_sinal(valor_binario):
    valor_sem_sinal = int(valor_binario, 2)
    bit_sinal = 1 << (NUM_BITS - 1)

    if valor_sem_sinal & bit_sinal:
        return valor_sem_sinal - MODULO

    return valor_sem_sinal


def inteiro_para_binario_4_bits(valor):
    return format(valor & MASCARA, "04b")


def ocorreu_overflow(valor):
    return valor < LIMITE_NEGATIVO or valor > LIMITE_POSITIVO


def multiplicar(valor_a, valor_b):
    resultado_negativo = (valor_a < 0) != (valor_b < 0)
    multiplicando = abs(valor_a)
    multiplicador = abs(valor_b)
    resultado = 0

    while multiplicador > 0:
        if multiplicador & 1:
            resultado += multiplicando

        multiplicando <<= 1
        multiplicador >>= 1

    return -resultado if resultado_negativo else resultado


def dividir(valor_a, valor_b):
    if valor_b == 0:
        raise ZeroDivisionError("Divisao por zero")

    resultado_negativo = (valor_a < 0) != (valor_b < 0)
    dividendo = abs(valor_a)
    divisor = abs(valor_b)
    quociente = 0
    resto = 0

    for bit in range(NUM_BITS - 1, -1, -1):
        resto <<= 1
        resto |= (dividendo >> bit) & 1

        if resto >= divisor:
            resto -= divisor
            quociente |= 1 << bit

    if resultado_negativo:
        quociente = -quociente

    if valor_a < 0:
        resto = -resto

    return quociente, resto


def fatorial(valor):
    if valor < 0:
        raise ValueError("Fat de negativo")

    resultado = 1

    for fator in range(2, valor + 1):
        resultado = multiplicar(resultado, fator)

    return resultado


def calcular(operacao, valor_a, valor_b):
    if operacao == "+":
        return valor_a + valor_b, None

    if operacao == "-":
        return valor_a - valor_b, None

    if operacao == "*":
        return multiplicar(valor_a, valor_b), None

    if operacao == "/":
        return dividir(valor_a, valor_b)

    if operacao == "!":
        return fatorial(valor_a), None

    raise ValueError("Operacao invalida")


# ==========================================================
# DRIVER DO LCD1602 I2C PCF8574
# ==========================================================

def abrir_barramento_i2c():
    try:
        from smbus2 import SMBus
    except ImportError:
        from smbus import SMBus

    return SMBus(I2C_BUS)


class Lcd1602I2c:
    def __init__(self, bus, address=LCD_I2C_ADDRESS, cols=16, rows=2):
        self.bus = bus
        self.address = address
        self.cols = cols
        self.rows = rows
        self.backlight = LCD_BACKLIGHT

    def escrever_expansor(self, valor):
        self.bus.write_byte(self.address, valor | self.backlight)

    def pulsar_enable(self, valor):
        self.escrever_expansor(valor | LCD_ENABLE)
        sleep(0.0005)
        self.escrever_expansor(valor & ~LCD_ENABLE)
        sleep(0.0001)

    def enviar_4_bits(self, valor):
        self.escrever_expansor(valor)
        self.pulsar_enable(valor)

    def enviar(self, valor, modo=0):
        parte_alta = valor & 0xF0
        parte_baixa = (valor << 4) & 0xF0

        self.enviar_4_bits(parte_alta | modo)
        self.enviar_4_bits(parte_baixa | modo)

    def comando(self, valor):
        self.enviar(valor, 0)

        if valor in (0x01, 0x02):
            sleep(0.002)

    def escrever_caractere(self, caractere):
        self.enviar(ord(caractere), LCD_RS)

    def init_lcd(self, addr=None, bl=1):
        if addr is not None:
            self.address = addr

        self.backlight = LCD_BACKLIGHT if bl else 0
        sleep(0.05)

        self.enviar_4_bits(0x30)
        sleep(0.005)
        self.enviar_4_bits(0x30)
        sleep(0.005)
        self.enviar_4_bits(0x30)
        sleep(0.001)
        self.enviar_4_bits(0x20)

        self.comando(0x28)  # 4 bits, 2 linhas, matriz 5x8.
        self.comando(0x0C)  # Display ligado, cursor desligado.
        self.comando(0x06)  # Incrementa cursor.
        self.clear()

    def clear(self):
        self.comando(0x01)

    def limpar(self):
        self.clear()

    def posicionar_cursor(self, coluna, linha):
        enderecos_linha = [0x00, 0x40, 0x14, 0x54]
        linha = max(0, min(linha, self.rows - 1))
        coluna = max(0, min(coluna, self.cols - 1))
        self.comando(0x80 | (enderecos_linha[linha] + coluna))

    def write(self, x, y, texto):
        texto = str(texto)
        x = max(0, min(x, self.cols - 1))
        y = max(0, min(y, self.rows - 1))
        espaco_disponivel = self.cols - x

        self.posicionar_cursor(x, y)

        for caractere in texto[:espaco_disponivel]:
            self.escrever_caractere(caractere)

    def display_num(self, x, y, numero):
        self.write(x, y, str(numero))

    def escrever_linha(self, linha, texto):
        self.write(0, linha, str(texto)[:self.cols].ljust(self.cols))

    def mostrar(self, linha_0="", linha_1=""):
        self.clear()
        self.write(0, 0, linha_0)
        self.write(0, 1, linha_1)


# ==========================================================
# LEITURA DO TECLADO MATRICIAL
# ==========================================================

class TecladoMatricial:
    def __init__(self, row_pins, col_pins, keymap, rows=ROWS, cols=COLS):
        import RPi.GPIO as GPIO

        self.GPIO = GPIO
        self.row_pins = row_pins
        self.col_pins = col_pins
        self.keymap = keymap
        self.rows = rows
        self.cols = cols

        if len(self.row_pins) != self.rows or len(self.col_pins) != self.cols:
            raise ValueError("Pinagem do teclado nao confere com ROWS/COLS.")

        if len(self.keymap) != self.rows or any(
            len(linha) != self.cols for linha in self.keymap
        ):
            raise ValueError("Mapa de teclas nao confere com ROWS/COLS.")

        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)

        for pin in self.row_pins:
            GPIO.setup(pin, GPIO.OUT)
            GPIO.output(pin, GPIO.HIGH)

        for pin in self.col_pins:
            GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)

    def ler_varredura(self):
        for row_index, row_pin in enumerate(self.row_pins):
            self.GPIO.output(row_pin, self.GPIO.LOW)
            sleep(0.001)

            for col_index, col_pin in enumerate(self.col_pins):
                if self.GPIO.input(col_pin) == self.GPIO.LOW:
                    self.GPIO.output(row_pin, self.GPIO.HIGH)
                    return self.keymap[row_index][col_index]

            self.GPIO.output(row_pin, self.GPIO.HIGH)

        return None

    def aguardar_tecla(self):
        while True:
            tecla = self.ler_varredura()

            if tecla is not None:
                sleep(KEYPAD_DEBOUNCE_SECONDS)

                if self.ler_varredura() == tecla:
                    while self.ler_varredura() is not None:
                        sleep(0.01)

                    sleep(KEYPAD_DEBOUNCE_SECONDS)
                    return tecla

            sleep(0.01)

    def limpar(self):
        self.GPIO.cleanup()


# ==========================================================
# APLICACAO DA CALCULADORA
# ==========================================================

class CalculadoraStandalone:
    def __init__(self, lcd, teclado):
        self.lcd = lcd
        self.teclado = teclado
        self.reiniciar_estado()

    def reiniciar_estado(self):
        self.operacao = None
        self.operando_a_binario = ""
        self.operando_b_binario = ""
        self.estado = "operacao"

    def mostrar_inicio(self):
        self.lcd.mostrar("A:+ B:- C:* D:/", "*:fat #:limpa")

    def mostrar_entrada(self):
        if self.estado == "operando_a":
            linha_0 = f"Op {self.operacao} A:{self.operando_a_binario}"
            linha_1 = "Digite 4 bits"
        else:
            linha_0 = f"Op {self.operacao} B:{self.operando_b_binario}"
            valor_a = binario_para_inteiro_com_sinal(
                self.operando_a_binario
            )
            linha_1 = f"A={self.operando_a_binario} {valor_a:+d}"

        self.lcd.mostrar(linha_0, linha_1)

    def mostrar_erro(self, linha_0, linha_1=""):
        self.lcd.mostrar(linha_0, linha_1)
        sleep(1.3)

        if self.estado == "operacao":
            self.mostrar_inicio()
        elif self.estado in ("operando_a", "operando_b"):
            self.mostrar_entrada()
        else:
            self.mostrar_inicio()

    def selecionar_operacao(self, tecla):
        if tecla not in OPERACOES:
            self.mostrar_erro("Tecla invalida", "Use A/B/C/D/*")
            return

        self.operacao = OPERACOES[tecla]
        self.estado = "operando_a"
        self.operando_a_binario = ""
        self.operando_b_binario = ""
        self.mostrar_entrada()

    def adicionar_bit(self, tecla):
        if tecla not in ("0", "1"):
            self.mostrar_erro("Entrada invalida", "Use apenas 0/1")
            return

        if self.estado == "operando_a":
            self.operando_a_binario += tecla

            if len(self.operando_a_binario) == NUM_BITS:
                if self.operacao == "!":
                    self.executar_calculo()
                    return
                else:
                    self.estado = "operando_b"

        elif self.estado == "operando_b":
            self.operando_b_binario += tecla

            if len(self.operando_b_binario) == NUM_BITS:
                self.executar_calculo()
                return

        self.mostrar_entrada()

    def executar_calculo(self):
        valor_a = binario_para_inteiro_com_sinal(self.operando_a_binario)
        valor_b = 0

        if self.operacao != "!":
            valor_b = binario_para_inteiro_com_sinal(
                self.operando_b_binario
            )

        try:
            resultado, resto = calcular(self.operacao, valor_a, valor_b)
        except (ValueError, ZeroDivisionError) as erro:
            self.lcd.mostrar("Erro", str(erro))
            sleep(2.0)
            self.reiniciar_estado()
            self.mostrar_inicio()
            return

        self.estado = "resultado"
        self.mostrar_resultado(resultado, resto)

    def mostrar_resultado(self, resultado, resto):
        resultado_binario = inteiro_para_binario_4_bits(resultado)
        status = "OVF" if ocorreu_overflow(resultado) else "OK"

        if self.operacao == "!":
            linha_0 = f"{self.operando_a_binario}!={resultado_binario}"
        else:
            linha_0 = (
                f"{self.operando_a_binario}"
                f"{self.operacao}"
                f"{self.operando_b_binario}"
                f"={resultado_binario}"
            )

        if resto is None:
            linha_1 = f"dec={resultado} {status}"
        else:
            linha_1 = f"q={resultado} r={resto} {status}"

        self.lcd.mostrar(linha_0, linha_1)

    def tratar_tecla(self, tecla):
        if tecla == "#":
            self.reiniciar_estado()
            self.mostrar_inicio()
            return

        if self.estado == "resultado":
            self.reiniciar_estado()

            if tecla in OPERACOES:
                self.selecionar_operacao(tecla)
            else:
                self.mostrar_inicio()

            return

        if self.estado == "operacao":
            self.selecionar_operacao(tecla)
            return

        self.adicionar_bit(tecla)

    def executar(self):
        self.mostrar_inicio()

        while True:
            tecla = self.teclado.aguardar_tecla()
            self.tratar_tecla(tecla)


def main():
    bus = None
    teclado = None

    try:
        bus = abrir_barramento_i2c()
        lcd = Lcd1602I2c(bus, cols=LCD_COLS, rows=LCD_ROWS)
        lcd.init_lcd(LCD_I2C_ADDRESS)
        teclado = TecladoMatricial(ROW_PINS, COL_PINS, KEYMAP)
        app = CalculadoraStandalone(lcd, teclado)
        app.executar()
    except KeyboardInterrupt:
        pass
    except Exception as erro:
        print(f"Falha ao iniciar a calculadora: {erro}")
        print("Verifique GPIO, I2C, endereco do LCD e bibliotecas instaladas.")
        sleep(2)
    finally:
        if teclado is not None:
            teclado.limpar()

        if bus is not None:
            bus.close()


if __name__ == "__main__":
    main()
