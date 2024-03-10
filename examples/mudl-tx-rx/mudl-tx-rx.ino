#include <mudlink.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// tested on the RP2040 with Earle's core 

// built-ins for some debug 
#define PIN_LED_BLINK PIN_LED_R 
#define PIN_LED_WD PIN_LED_G 
#define PIN_DEBUG_GPIO 3

// display setup, which is wired to the XIAO's labelled SCL/SDA pins 
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT);

MUDL_Link<decltype(Serial1)> mudl(&Serial1, 1000000);

void printToScreen(String message){
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(message);
  display.display();
}

void setup() {
  // setup our LEDs,
  pinMode(PIN_LED_BLINK, OUTPUT);
  pinMode(PIN_LED_WD, OUTPUT);
  pinMode(PIN_DEBUG_GPIO, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  digitalWrite(PIN_LED_BLINK, HIGH);

  // we also need to setup the display: 
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  display.display();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  printToScreen("bonjour");

  // and init our thingy
  mudl.begin();
}

// watchdog blinker 
const uint32_t wdInterval = 1000;
uint32_t lastWd = 0;

// packet stash 
uint8_t rxPacket[255];
uint8_t txPacket[255];

// want to be doing random transmit intervals, 
uint64_t lastTx = 0;
uint64_t txInterval = 100;

// 10ms micros-based-clock to check quality of overclock offsets 
uint64_t clockMeasurementInterval = 10000;
uint64_t lastClockMeasureTick = 0; 

String printStats(MUDLStats stats){
      return 
        "avgttx (us): " + String(stats.averageTotalTransmitTime, 1) + 
        "\navg wire (us): " + String(stats.averageWireTime, 1) + 
        "\navg retry: " + String(stats.averageRetryCount, 6) +
        "\nrx loss:   " + String((float)stats.rxFailureCount / (float)stats.rxSuccessCount, 6) + 
        "\ntx lost / total, rollover: \n" + String(stats.txFailureCount) + " / " + String(stats.txSuccessCount) +
        "\n" + String((uint32_t)(microsBase64() >> 32));
}

void loop() {
  mudl.loop();

  if(mudl.clearToRead()){
    size_t len = mudl.read(rxPacket, 255);
  }

  if(lastTx + txInterval < microsBase64() && mudl.clearToSend()){
    lastTx = microsBase64();
    txInterval = random(256);
    uint8_t txLen = random(64);
    for(uint8_t i = 0; i < txLen; i ++){
      txPacket[i] = random(256);
    }
    mudl.send(txPacket, txLen);
  }

  if(lastClockMeasureTick + clockMeasurementInterval < microsBase64()){
    digitalWrite(PIN_DEBUG_GPIO, !digitalRead(PIN_DEBUG_GPIO));
    lastClockMeasureTick = microsBase64();
  } else if(lastWd + wdInterval < millis()){
    lastWd = millis();
    printToScreen(printStats(mudl.getStats()));
    digitalWrite(PIN_LED_WD, !digitalRead(PIN_LED_WD)); 
  }
}
