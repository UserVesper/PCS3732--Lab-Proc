#!/usr/bin/env python3

"""
Calculadora binaria para Raspberry Pi 3.

Entradas:
  - Teclado conectado ao Raspberry Pi.

Saidas:
  - Terminal exibido no monitor conectado via HDMI/VGA.

Representacao:
  - Entradas e saidas usam NUM_BITS bits em complemento de 2.
  - O intervalo representavel e calculado a partir de NUM_BITS.
"""

from time import perf_counter_ns

NUM_BITS = 4

if NUM_BITS < 1:
    raise ValueError("NUM_BITS deve ser maior ou igual a 1.")

MODULO = 1 << NUM_BITS
MASCARA = MODULO - 1
LIMITE_NEGATIVO = -(1 << (NUM_BITS - 1))
LIMITE_POSITIVO = (1 << (NUM_BITS - 1)) - 1
FORMATO_BINARIO = f"0{NUM_BITS}b"
UNIDADE_BITS = "bit" if NUM_BITS == 1 else "bits"


def eh_binario(valor):
    return len(valor) == NUM_BITS and all(bit in "01" for bit in valor)


def binario_para_inteiro_com_sinal(valor_binario):
    valor_sem_sinal = int(valor_binario, 2)
    bit_sinal = 1 << (NUM_BITS - 1)

    if valor_sem_sinal & bit_sinal:
        return valor_sem_sinal - MODULO

    return valor_sem_sinal


def inteiro_para_binario(valor):
    return format(valor & MASCARA, FORMATO_BINARIO)


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
        raise ZeroDivisionError("Nao e possivel dividir por zero.")

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
        raise ValueError("Nao existe fatorial de numero negativo.")

    resultado = 1

    for fator in range(2, valor + 1):
        resultado = multiplicar(resultado, fator)

    return resultado


def ler_operando(nome):
    while True:
        valor = input(
            f"Digite o operando {nome} em {NUM_BITS} {UNIDADE_BITS}: "
        ).strip()

        if eh_binario(valor):
            return valor, binario_para_inteiro_com_sinal(valor)

        exemplo = inteiro_para_binario(min(3, LIMITE_POSITIVO))
        print(
            "Entrada invalida. Use exatamente "
            f"{NUM_BITS} {UNIDADE_BITS}, por exemplo: {exemplo}."
        )


def ler_operacao():
    operacoes = {"+", "-", "*", "/", "!", "sair"}

    while True:
        operacao = input("Escolha a operacao [+] [-] [*] [/] [!] ou sair: ")
        operacao = operacao.strip().lower()

        if operacao in operacoes:
            return operacao

        print("Operacao invalida.")


def imprimir_cabecalho():
    print("=" * 60)
    print(f"Calculadora binaria de {NUM_BITS} {UNIDADE_BITS} - Raspberry Pi 3")
    print(f"Complemento de 2 | intervalo: {LIMITE_NEGATIVO} ate {LIMITE_POSITIVO}")
    print("=" * 60)


def imprimir_resultado(
    operacao,
    operando_a_binario,
    valor_a,
    operando_b_binario,
    valor_b,
    resultado,
    tempo_operacao_ns,
    resto=None,
):
    resultado_binario = inteiro_para_binario(resultado)
    overflow = ocorreu_overflow(resultado)

    print()
    print("-" * 60)
    print(f"Operacao: {operacao}")
    print(f"A: {operando_a_binario} = {valor_a}")

    if operacao == "!":
        print("B: nao utilizado")
    else:
        print(f"B: {operando_b_binario} = {valor_b}")

    print(f"Resultado decimal completo: {resultado}")
    print(f"Resultado em {NUM_BITS} {UNIDADE_BITS}: {resultado_binario}")
    print(
        "Resultado interpretado em complemento de 2: "
        f"{binario_para_inteiro_com_sinal(resultado_binario)}"
    )
    print(f"Overflow em {NUM_BITS} {UNIDADE_BITS}: {'SIM' if overflow else 'NAO'}")
    print(
        "Tempo para realizar a operacao: "
        f"{tempo_operacao_ns} ns "
        f"({tempo_operacao_ns / 1000:.3f} us | "
        f"{tempo_operacao_ns / 1_000_000:.6f} ms)"
    )

    if resto is not None:
        resto_binario = inteiro_para_binario(resto)
        print(f"Resto decimal: {resto}")
        print(f"Resto em {NUM_BITS} {UNIDADE_BITS}: {resto_binario}")

    print("-" * 60)
    print()


def executar_operacao(operacao):
    operando_a_binario, valor_a = ler_operando("A")
    operando_b_binario = inteiro_para_binario(0)
    valor_b = 0
    resto = None

    if operacao != "!":
        operando_b_binario, valor_b = ler_operando("B")

    inicio_operacao_ns = perf_counter_ns()

    if operacao == "+":
        resultado = valor_a + valor_b
    elif operacao == "-":
        resultado = valor_a - valor_b
    elif operacao == "*":
        resultado = multiplicar(valor_a, valor_b)
    elif operacao == "/":
        resultado, resto = dividir(valor_a, valor_b)
    elif operacao == "!":
        resultado = fatorial(valor_a)
    else:
        raise ValueError("Operacao invalida.")

    tempo_operacao_ns = perf_counter_ns() - inicio_operacao_ns

    imprimir_resultado(
        operacao,
        operando_a_binario,
        valor_a,
        operando_b_binario,
        valor_b,
        resultado,
        tempo_operacao_ns,
        resto,
    )


def main():
    imprimir_cabecalho()

    while True:
        operacao = ler_operacao()

        if operacao == "sair":
            print("Encerrando a calculadora.")
            break

        try:
            executar_operacao(operacao)
        except (ValueError, ZeroDivisionError) as erro:
            print(f"Erro: {erro}")
            print()


if __name__ == "__main__":
    main()
