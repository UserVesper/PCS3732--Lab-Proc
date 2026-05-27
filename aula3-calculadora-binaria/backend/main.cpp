
#include <WebServer.h>
#include <WiFi.h>

const int LEDS[] = {12,13,14,15};

WebServer server(80);

void handleCalc(){
    if(server.hasArg("a") && server.hasArg("b") && server.hasArg("op")){

        String paramA = server.arg("a");
        String paramB = server.arg("b");
        String op = server.arg("op");

        int valueA = strtol(paramA.c_str(), NULL, 2);
        int valueB = strtol(paramB.c_str(), NULL, 2);

        int result = (op == "add") ? (valueA + valueB) : (valueA - valueB);
        result = result & 0x0F;
        
        digitalWrite(LEDS[0], result & 0x01);
        digitalWrite(LEDS[1], (result >> 1) & 0x01); 
        digitalWrite(LEDS[2], (result >> 2) & 0x01); 
        digitalWrite(LEDS[3], (result >> 3) & 0x01);

        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Faltam parâmetros");
    }
}

void setup() {
pinMode(LEDS[0], OUTPUT);
pinMode(LEDS[1], OUTPUT);
pinMode(LEDS[2], OUTPUT);
pinMode(LEDS[3], OUTPUT);
digitalWrite(LEDS[0], LOW);
digitalWrite(LEDS[1], LOW);
digitalWrite(LEDS[2], LOW);
digitalWrite(LEDS[3], LOW);

WiFi.softAO("Calculadora_ESP32");

server.on("/calc", handleCalc);
server.begin();
}

void loop() {
server.handleClient();
}