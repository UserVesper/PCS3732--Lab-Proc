#include <Adafruit_NeoPixel.h>

#define PINO_LED      8  
#define NUMERO_LEDS   1  

// Configura a biblioteca NeoPixel
Adafruit_NeoPixel led(NUMERO_LEDS, PINO_LED, NEO_GRB + NEO_KHZ800);

void setup() {
  led.begin(); 
}

void loop() {
  // led.Color(Vermelho, Verde, Azul) -> Valores de 0 a 255
  led.setPixelColor(0, led.Color(0, 255, 0)); // Liga apenas o Verde
  led.show();                                 // Envia o comando para o LED acender
  delay(3000);                                

  //  SINAL AMARELO 
  led.setPixelColor(0, led.Color(255, 200, 0)); // Mistura Vermelho + Verde = Amarelo
  led.show();
  delay(1000);                               

  // SINAL VERMELHO 
  led.setPixelColor(0, led.Color(255, 0, 0)); // Liga apenas o Vermelho
  led.show();
  delay(4000);                                
}
