#include <SPI.h>        // Knižnica SPI (komunikácia po sériovej zbernici)
#include <RH_RF69.h>    // Knižnica pre prácu s RF69 modulom

// ——— definovanie pinov pre rádio ———
#define RFM69_CS    D8    // SPI chip-select (vyber zariadenia)
#define RFM69_INT   D1    // DIO0 → prerušenie (nepoužíva sa pri vysielaní)
#define RFM69_RST   D0    // reset pinu rádia
#define FREQUENCY   433.0 // frekvencia rádia v MHz

RH_RF69 rf69(RFM69_CS, RFM69_INT); // Vytvorenie objektu pre rádio

// ——— piny pre rozvinutie a vyhrievanie ———
#define ANT_DEP   D2    // HODNOTA HIGH → anténa je rozvinutá
#define SAT_DEP   D3    // HODNOTA HIGH → tlačidlo pre vypustenie satelitu bolo uvoľnené
#define ZHAV_DEP  D4    // výstup, ktorý ovláda vyhrievanie antény

// ——— Časovanie Morseovky (v milisekundách) ———
const uint16_t DOT    = 20;           // bodka
const uint16_t DASH   = DOT * 3;      // čiarka
const uint16_t GAP_E  = DOT;          // medzera medzi bodkami a čiarkami
const uint16_t GAP_L  = DOT * 3;      // medzera medzi písmenami
const uint16_t GAP_W  = DOT * 7;      // medzera medzi slovami

// ——— Úvodná správa a počet opakovaní ———
const char* UvodnaSprava = "VUT/NANO/RFM69HW"; // správa, ktorá sa odošle v Morseovke
const int   NumVysMax    = 10;                 // maximálny počet opakovaní

uint32_t StartTime;
bool     LockA = false, LockB = false;
int      NumVys = 0; // aktuálny počet odoslaní

// ——— Morseova abeceda ———
struct MorseMap { char letter; const char* code; };
MorseMap morseMap[] = {
  {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
  {'E', "."},  {'F', "..-."}, {'G', "--."},  {'H', "...."},
  {'I', ".."}, {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
  {'M', "--"}, {'N', "-."},   {'O', "---"},  {'P', ".--."},
  {'Q', "--.-"},{'R', ".-."}, {'S', "..."},  {'T', "-"},
  {'U', "..-"},{'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
  {'Y', "-.--"},{'Z', "--.."},
  {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},
  {'4', "....-"},{'5', "....."},{'6', "-...."},{'7', "--..."},
  {'8', "---.."},{'9', "----."},{'/', "-..-."}
};

// Funkcia na získanie Morseovho kódu pre daný znak
const char* getMorseCodeFor(char c) {
  c = toupper(c);
  for (auto &m : morseMap)
    if (m.letter == c) return m.code;
  return "";
}

// ——— Zapnutie a vypnutie vysielania ———
void txOn()  { rf69.setModeTx(); } // zapnutie vysielania
void txOff() { rf69.setModeRx(); } // vypnutie (prepnúť rádio späť do prijímacieho režimu)

// ——— Morseove základné prvky ———
void sendDot() {
  txOn(); delay(DOT);  txOff(); // vysielaj krátko
  delay(GAP_E);
}

void sendDash() {
  txOn(); delay(DASH); txOff(); // vysielaj dlho
  delay(GAP_E);
}

void sendLetterGap() {
  delay(GAP_L - GAP_E); // medzera medzi písmenami
}

void sendMorseLetter(char c) {
  const char* code = getMorseCodeFor(c);
  if (!code || *code == '\0') {
    if (c == ' ') delay(GAP_W - GAP_L); // medzera medzi slovami
    return;
  }
  for (size_t i = 0; code[i]; i++) {
    if (code[i] == '.')      sendDot();
    else if (code[i] == '-') sendDash();
  }
  sendLetterGap();
}

void sendMorseMessage(const char* msg) {
  for (size_t i = 0; msg[i]; i++)
    sendMorseLetter(msg[i]);
}

// ——— Inicializácia ———
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(1); }

  // vypni výhrev
  pinMode(ZHAV_DEP, OUTPUT);
  digitalWrite(ZHAV_DEP, LOW);

  // nastav vstupy s pullup odporom
  pinMode(ANT_DEP, INPUT_PULLUP);
  pinMode(SAT_DEP, INPUT_PULLUP);

  // reset rádia
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, HIGH); delay(10);
  digitalWrite(RFM69_RST, LOW);  delay(10);

  // inicializuj rádio
  if (!rf69.init()) {
    Serial.println("RFM69 zlyhala pri inicializácii");
    while (1);
  }
  rf69.setModemConfig(RH_RF69::OOK_Rb1Bw1); // nastavenie OOK modulácie
  rf69.setFrequency(FREQUENCY);
  rf69.setTxPower(5, true); // výstupný výkon 5 dBm, high-power mód

  // prepni rádio do OOK CW módu (nebalíčkový mód)
  rf69.spiWrite(RH_RF69_REG_02_DATAMODUL,    0x0A);          
  uint8_t p1 = rf69.spiRead(RH_RF69_REG_37_PACKETCONFIG1);
  rf69.spiWrite(RH_RF69_REG_37_PACKETCONFIG1, p1 & ~(1<<7)); // kontinuálny mód

  // zúženie šírky pásma na približne 3.9 kHz
  rf69.spiWrite(RH_RF69_REG_19_RXBW,  (0<<4) | 6);
  rf69.spiWrite(RH_RF69_REG_1A_AFCBW, (0<<4) | 6);

  Serial.println("RFM69 pripravené pre CW (nepretržité vysielanie)");
  StartTime = millis();
}

// ——— Hlavný cyklus ———
void loop() {
  // výhrev vypnutý ako predvolený stav
  if (digitalRead(ANT_DEP)==HIGH || digitalRead(SAT_DEP)==HIGH)
    digitalWrite(ZHAV_DEP, LOW);

  // čakaj na vypustenie
  if (digitalRead(SAT_DEP)==LOW) {
    unsigned long t = millis() - StartTime;

    // 1) zapni výhrev, ak anténa ešte nie je otvorená po 2s
    if (t > 2000 && digitalRead(ANT_DEP)==LOW) {
      digitalWrite(ZHAV_DEP, HIGH);
      Serial.println("Výhrev ZAPNUTÝ");
    }

    // keď sa anténa otvorí, vypni výhrev
    if (digitalRead(ANT_DEP)==HIGH && !LockA) {
      digitalWrite(ZHAV_DEP, LOW);
      LockA = true;
      StartTime = millis(); // reset časovania pre Morse
      Serial.println("Výhrev VYPNUTÝ — anténa otvorená");
    }

    // 2) odosielaj Morseovku
    if (LockA && !LockB) {
      if (t > 2000 + NumVys * (DOT + GAP_E)) {
        Serial.printf("Morse #%d\n", NumVys+1);
        sendMorseMessage(UvodnaSprava);
        NumVys++;
        if (NumVys >= NumVysMax)
          LockB = true;
      }
    }
  }
}
