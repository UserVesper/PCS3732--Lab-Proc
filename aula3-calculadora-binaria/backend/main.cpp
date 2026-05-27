const int LEDS[] = {12,13,14,15}

void handleCalc(){
    String paramA = "0011";
    String paramB = "0101";
    int valueA = strtol(paramA.c_str(), NULL, 2);
    int valueB = strtol(paramB.c_str(), NULL, 2);

    int result = valueA + valueB;
    result = result & 0x0F;
    
    digitalWrite(LEDS[0], result & 0x01);
    digitalWrite(LEDS[1], (result >> 1) & 0x01); 
    digitalWrite(LEDS[2], (result >> 2) & 0x01); 
    digitalWrite(LEDS[3], (result >> 3) & 0x01);

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
}

void loop() {

}