#include <RadioHead.h>

//---------------------------------------------------------- Priebeh Info (start)
//Po spustení esp8266, sa čaká kedy PulUp tlačidlo "#define SAT_DEP D2" sa uvolní (stav = HIGH) a tým sa spustí loop programs.
//Anténa sa zacne zhaviť po "int DelayAntDep" (2 sek) od Sat_Dep uvolnení tlačidla, na pine "#define Zhav_DEP D3 ". ň
//Keď sa anténa otvorí == "#define ANT_DEP D1" tlacidlo prejde do stave High (PulUp), tak sa spustí "bool LockA" inner bit, aby zabránil inf spstaniu.
//Po otvorení antény sa čaká "int DelayTxStart" (2 sek), kým začne vysielanie úvodnej správy "const char* UvodnaSprava" ("VUT/NANO/RFM69HW"), 
// a to presne "int NumVysMax" krát (10x). Po prekročení "int NumVysMax", zopne sa "bool LockB" a tým sa skončí úvodné vysielanie.
// Nasledne sa po zopnutí "bool LockB" zapne RxTx comunikácia, ktorá len zapína a vypína vnútornú Led.
//........................................................... Priebeh Info (end)

#include <SPI.h>
#include <RH_RF69.h>

// rfm69 pins (CS, DIO0, RST)
#define RFM69_CS      D8
#define RFM69_INT     D1
#define RFM69_RST     D0

// Základné nastavenia rfm69
#define NODEID        1     c:\Users\murad\Documents\Arduino\libraries\RadioHead\RH_RF69.h
#define NETWORKID     100   
#define FREQUENCY     433.0 

RH_RF69 rf69(RFM69_CS, RFM69_INT);

// Vnútorná LED (ak nie je definovaná, tak D4; aktívna LOW)
#ifndef LED_BUILTIN
  #define LED_BUILTIN D4
#endif

// Pini pre 2 senzor. tlacidla (Ant/Sat dep) a pre Zhavenie na ant Dep.
#define ANT_DEP A0 // Vystup, Senzor ci je antena Otvr.
#define SAT_DEP D2 // Výstup, Senzor ci je Sat. Von vo vesmíre.
#define Zhav_DEP D3 // Vstup, Zhavenie na otvr. Anteny

//Dig. Bity a časovače a Spravy
unsigned long StartTime;
bool LockA = false; // Zamiká nekonečné vypínanie zhavenia
bool LockB = false; // Uzamiká a ukončuje úvodne vysielanie po NumVysMax 
int NumVys = 0; // Kolko krát sa už vyslala úvodná správa

 //------------------------------ ----------- Lubovolné nadstavenie (Start) ---------------- ------------------------------
int DelayAntDep = 2000; // v ms, určuje ze za 2 sek sa antena zacne zhavit, po SatDep
int DelayTxStart = 2000; // v ms, určuje ze za 2 sek sa zacne vysielat (uvodna sprava) po skonceni zhavenia
const char* UvodnaSprava = "VUT/NANO/RFM69HW"; // Úvodná sprava, vysiela sa NumVysx
int NumVysMax = 10; // Kolko krát sa vysle úvodná správa
// ------------------------------ ----------- Lubovolné nadstavenie (end)   ----------------- -----------------------------

// Dĺžka trvania bodky v ms
#define DOT_DURATION 200

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
  //digitalWrite(LED_BUILTIN, LOW);  // Zapneme LED (aktívna LOW)
  const char* dot = ".";
  rf69.send((uint8_t*)dot, strlen(dot));
  rf69.waitPacketSent();
  delay(DOT_DURATION);             // Trvanie bodky
  //digitalWrite(LED_BUILTIN, HIGH); // Zhasneme LED
  delay(DOT_DURATION);             // Medzivklad  - časova medzera za (.)
}

void sendDash() {
  //digitalWrite(LED_BUILTIN, LOW);
  const char* dash = "-";
  rf69.send((uint8_t*)dash, strlen(dash));
  rf69.waitPacketSent();
  delay(DOT_DURATION * 2);         // Trvanie čiarky (3x dĺžka bodky)
  //digitalWrite(LED_BUILTIN, HIGH);
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
    Serial.println(code[i]);

    if (code[i] == '.'){
      sendDot();
      }
    if (code[i] == '-'){
      sendDash();
    }
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

  pinMode(Zhav_DEP, OUTPUT); // pre antenu Otvorenie/Zhavenie
  digitalWrite(Zhav_DEP, LOW); // PulDown mod LOW = 0V

  pinMode(ANT_DEP, INPUT_PULLUP); // Tlacidlo, je ant. otvorena? PulIp
  pinMode(SAT_DEP, INPUT_PULLUP); // Tlacidlo, je sat. vypustení? PulUp

  
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

  StartTime = millis();
}

void loop() {
  //Serial.println("LA:" + String(LockA) + "LB:" + String(LockB));
  if (digitalRead(SAT_DEP) == HIGH) { // Loop code sa spustí po uvolnení tlačidla Sat_Dep, to je keď bude HIGH, lebo PulUp mod
      unsigned long currentTime = millis() - StartTime; // loop time, po SatDep

      // --------------------------------------------------------
      // 1. ANT_DEP: Zhav 2s po SatDeploy aby sa otvorila antena
      // --------------------------------------------------------
      if (currentTime > DelayAntDep && digitalRead(ANT_DEP) == LOW) { // Otvranie anteny, keď je Ant_Dep tlac. stlacené (PulUp mod)
        digitalWrite(Zhav_DEP, HIGH);
        Serial.println("Zhav_DEP ON");
      }

      if (analogRead(ANT_DEP) >= 800 and LockA == false) { // Po otvorení anteny, potvrdenie keď tlacidlo ANT_DEP prejde na HIGH
        digitalWrite(Zhav_DEP, LOW);
        LockA = true; // LockA = Aby sa spustila len raz táto funkcia
        StartTime = millis(); // Resetuj StartTime, aby sa vysielať začalo DelayTxStart (sek) po skončení zhavenia, bod 2. Morse Správa
        Serial.println("Zhav_DEP OFF"); 
      }
      // ------------------------------
      // 2. Morse správa: "UvodnaSprava", vyslať NumVysx, DelayTxStart (sek), po tom ako sa skončí zhavenie => LockA== true
      // ------------------------------
      if (LockA == true && currentTime > DelayTxStart && LockB == false) {

        if (currentTime - DelayTxStart >= NumVys*1000){ // Opakuj každú sekundu
        Serial.print("Morse: " + String(UvodnaSprava) + " , NumVys");
        Serial.print(NumVys + 1);
        Serial.println();
        sendMorseMessage(UvodnaSprava); 
        NumVys++;
        }
      }
      
      if(NumVys >= NumVysMax && LockB == false){
        LockB = true;
      }
      
      // ------------------------------
      // 3. Rádio príjem:
      // Ak príde správa "ON" alebo "OFF", ON/OFF LED (LED_BUILTIN), 
      // ------------------------------
      if (rf69.available() && LockB == true) {
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
}
