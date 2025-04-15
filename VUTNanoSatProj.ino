//----------------------------------------------------------
//Po 20 sekundách od spustenia, zhavič na D3 otvorí antenu a po 5 s od otvorenuia začne vysielať správu Morse: "VUT/NANO/RFM69HW" celkom 10-krát.
//Morseov kód je vysielaný pomocou funkcií, ktoré mapujú jednotlivé znaky na bodky a čiarky a odosielajú ich cez rádio, správa sa da jednodučsie meniť. 
//Okrem toho kód monitoruje prichádzajúce rádiové správy, a ak prijme "ON" alebo "OFF", upraví stav vnútornej LED.
//...........................................................
#include <SPI.h>
#include <RH_RF69.h>

// rfm69 pins (CS, DIO0, RST)
#define RFM69_CS      D8
#define RFM69_INT     D1
#define RFM69_RST     D0

// Základné nastavenia rfm69
#define NODEID        1     
#define NETWORKID     100   
#define FREQUENCY     433.0 

RH_RF69 rf69(RFM69_CS, RFM69_INT);

// Vnútorná LED (ak nie je definovaná, tak D4; aktívna LOW)
#ifndef LED_BUILTIN
  #define LED_BUILTIN D4
#endif

// Antena otvorenie, ktorá sa aktivuje 20 s po štarte
#define LED_D3 D3

// Dĺžka trvania bodky v ms
#define DOT_DURATION 200

// Počet odoslaných Morse správ
int transmissions = 0;

// ------------------------------
// Morseov kód – tabuľka pre mapovanie znakov
// ------------------------------
struct MorseMapping {
  char letter;
  const char *code;
};

MorseMapping morseMap[] = {
  {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
  {'E', "."},    {'F', "..-."}, {'G', "--."},  {'H', "...."},
  {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
  {'M', "--"},   {'N', "-."},   {'O', "---"},  {'P', ".--."},
  {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
  {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
  {'Y', "-.--"}, {'Z', "--.."},
  {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},
  {'4', "....-"},{'5', "....."},{'6', "-...."},{'7', "--..."},
  {'8', "---.."},{'9', "----."},
  {'/', "-..-."}
};

const char* getMorseCodeFor(char c) {
  c = toupper(c);
  int mapSize = sizeof(morseMap) / sizeof(morseMap[0]);
  for (int i = 0; i < mapSize; i++) {
    if (morseMap[i].letter == c)
      return morseMap[i].code;
  }
  return "";
}

// ------------------------------
// Funkcie pre vysielanie Morseovho signálu
// ------------------------------
void sendDot() {
  digitalWrite(LED_BUILTIN, LOW);  // Zapneme LED (aktívna LOW)
  const char* dot = ".";
  rf69.send((uint8_t*)dot, strlen(dot));
  rf69.waitPacketSent();
  delay(DOT_DURATION);             // Trvanie bodky
  digitalWrite(LED_BUILTIN, HIGH); // Zhasneme LED
  delay(DOT_DURATION);             // Medzivklad  - časova medzera za (.)
}

void sendDash() {
  digitalWrite(LED_BUILTIN, LOW);
  const char* dash = "-";
  rf69.send((uint8_t*)dash, strlen(dash));
  rf69.waitPacketSent();
  delay(DOT_DURATION * 3);         // Trvanie čiarky (3x dĺžka bodky)
  digitalWrite(LED_BUILTIN, HIGH);
  delay(DOT_DURATION);             // Medzivklad - časova medzera za (-)
}

void sendLetterGap() {
  delay(DOT_DURATION * 2);         // Medzera medzi písmenami (spolu 3 DOT_DURATION)
}

void sendMorseLetter(char c) {
  const char* code = getMorseCodeFor(c);
  if (strlen(code) == 0) {         // Ak ide o medzeru, čakáme dlhšie (7 DOT_DURATION)
    if (c == ' ') delay(DOT_DURATION * 7);
    return;
  }
  for (int i = 0; code[i] != '\0'; i++) {
    if (code[i] == '.')
      sendDot();
    else if (code[i] == '-')
      sendDash();
  }
  sendLetterGap();
}

void sendMorseMessage(const char *msg) {
  for (int i = 0; msg[i] != '\0'; i++) {
    sendMorseLetter(msg[i]);
  }
}

// ------------------------------
// Inicializácia a hlavná slučka
// ------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(1); }
  

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED vnutorna je off (aktívna LOW)
  pinMode(LED_D3, OUTPUT); // pre antenu otvorenie
  digitalWrite(LED_D3, LOW);
  
  // Reset RFM69 
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);
  digitalWrite(RFM69_RST, HIGH);
  delay(10);
  digitalWrite(RFM69_RST, LOW);
  delay(10);
  
  // Inicializácia rfm69
  if (!rf69.init()) {
    Serial.println("RFM69 init fail");
    while (1);
  }
  if (!rf69.setFrequency(FREQUENCY)) {
    Serial.println("Freq fail");
  }
  rf69.setTxPower(14, true);
  Serial.println("RFM69 OK");
}

void loop() {
  unsigned long currentTime = millis();

  // ------------------------------
  // 1. LED_D3: Zapni 20 s po štarte
  // ------------------------------
  if (currentTime > 20000 && digitalRead(LED_D3) == LOW) {
    digitalWrite(LED_D3, HIGH);
    Serial.println("LED_D3 ON");
  }
  
  // ------------------------------
  // 2. Morse správa: "VUT/NANO/RFM69HW", vyslať 10x, začať po 25 s
  // ------------------------------
  if (currentTime > 25000 && transmissions < 10) {
    Serial.print("Morse: VUT/NANO/RFM69HW - ");
    Serial.print(transmissions + 1);
    Serial.println();
    sendMorseMessage("VUT/NANO/RFM69HW"); // Tu sa da meniť počiatočná správa -----------------*****--------------
    transmissions++;
    delay(1000);
  }
  
  // ------------------------------
  // 3. Rádio príjem:
  // Ak príde správa "ON" alebo "OFF", ON/OFF LED (LED_BUILTIN)
  // ------------------------------
  if (rf69.available()) {
    uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf69.recv(buf, &len)) {
      buf[len] = 0;
      Serial.print("Prijaté: ");
      Serial.println((char*)buf);
      String msg = String((char*)buf);
      msg.trim();
      if (msg.equalsIgnoreCase("ON")) {
        digitalWrite(LED_BUILTIN, LOW);  // Zapni LED (aktívna LOW)
        Serial.println("LED_BUILTIN ON");
      } else if (msg.equalsIgnoreCase("OFF")) {
        digitalWrite(LED_BUILTIN, HIGH); // Vypni LED
        Serial.println("LED_BUILTIN OFF");
      }
    }
  }
}
