/*
  ============================================================
  RECEPTOR DE ÁUDIO - ESP32 + nRF24L01 + DAC + GF1002
  ============================================================
  Recebe pacotes de áudio e reproduz pelo DAC no GPIO25

  Conexões DAC -> Amplificador GF1002:
    GPIO25 --> R  (entrada direita)
    GND    --> G  (terra sinal)
    5V     --> +POWER
    GND    --> -POWER
    ROUT   --> Alto-falante +
    GND    --> Alto-falante -

  Conexões nRF24L01:
    VCC   --> 3.3V
    GND   --> GND
    CE    --> GPIO4
    CSN   --> GPIO5
    SCK   --> GPIO18
    MOSI  --> GPIO23
    MISO  --> GPIO19
  ============================================================
*/

#include <SPI.h>
#include <RF24.h>

// ---- nRF24 ----
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "AUDIO";

// ---- Áudio ----
#define SAMPLE_RATE   8000      // Deve ser igual ao transmissor e ao converter.py
#define PAYLOAD_SIZE  32
#define DAC_PIN       25        // DAC1 do ESP32

// ---- Buffer circular ----
// 1024 amostras = ~128ms de buffer. Absorve variações de latência do rádio.
#define RING_SIZE  1024
uint8_t  ring[RING_SIZE];
volatile int writePos        = 0;
volatile int readPos         = 0;
volatile int bufferedSamples = 0;

// ---- Timer de reprodução ----
hw_timer_t*  playTimer = NULL;
portMUX_TYPE mux       = portMUX_INITIALIZER_UNLOCKED;

// ---- Estatísticas ----
uint32_t pktReceived = 0;
uint32_t pktDropped  = 0;
uint32_t underruns   = 0;   // Momentos em que o buffer esvaziou

// ---- ISR: toca uma amostra a cada 125 µs (8000 Hz) ----
void IRAM_ATTR onPlayTimer() {
  portENTER_CRITICAL_ISR(&mux);
  if (bufferedSamples > 0) {
    dacWrite(DAC_PIN, ring[readPos]);
    readPos = (readPos + 1) % RING_SIZE;
    bufferedSamples--;
  } else {
    dacWrite(DAC_PIN, 0x80);  // Silêncio (nível DC central)
    underruns++;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

// ---- Adiciona payload ao buffer circular ----
bool pushPacket(uint8_t* data) {
  if (bufferedSamples + PAYLOAD_SIZE > RING_SIZE) {
    return false;  // Buffer cheio — descarta pacote
  }
  for (int i = 0; i < PAYLOAD_SIZE; i++) {
    ring[writePos] = data[i];
    writePos = (writePos + 1) % RING_SIZE;
  }
  portENTER_CRITICAL(&mux);
  bufferedSamples += PAYLOAD_SIZE;
  portEXIT_CRITICAL(&mux);
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== RECEPTOR DE ÁUDIO nRF24 ===");

  if (!radio.begin()) {
    Serial.println("ERRO: nRF24L01 não encontrado!");
    while (1) delay(500);
  }

  // DEVE ser idêntico ao transmissor
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(108);
  radio.setPayloadSize(PAYLOAD_SIZE);
  radio.setCRCLength(RF24_CRC_8);
  radio.setAutoAck(false);
  radio.openReadingPipe(1, ADDRESS);
  radio.startListening();

  Serial.println("nRF24L01 OK");

  // Pré-preenche o buffer com silêncio para evitar underrun inicial
  // ~200ms de silêncio antes de começar a tocar
  int preFill = (SAMPLE_RATE / 5);   // 1600 amostras... mas o ring tem 1024
  if (preFill > RING_SIZE / 2) preFill = RING_SIZE / 2;
  memset(ring, 0x80, RING_SIZE);
  bufferedSamples = preFill;
  writePos = preFill;
  readPos  = 0;

  // Timer: 8000 Hz = 125 µs por amostra
  playTimer = timerBegin(1, 80, true);   // Timer 1, prescaler 80 → 1 µs/tick
  timerAttachInterrupt(playTimer, &onPlayTimer, true);
  timerAlarmWrite(playTimer, 125, true); // Alarme a cada 125 µs
  timerAlarmEnable(playTimer);

  Serial.println("Reprodução iniciada (8000 Hz)");
  Serial.println("Aguardando pacotes...");
}

void loop() {
  uint8_t rxBuf[PAYLOAD_SIZE];

  // Drena todos os pacotes disponíveis no rádio
  while (radio.available()) {
    radio.read(rxBuf, PAYLOAD_SIZE);
    pktReceived++;
    if (!pushPacket(rxBuf)) {
      pktDropped++;
    }
  }

  // Status a cada 5 segundos
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[STATUS] Recebidos: %lu | Descartados: %lu | Underruns: %lu | Buffer: %d/%d\n",
                  pktReceived, pktDropped, underruns, bufferedSamples, RING_SIZE);
  }
}
