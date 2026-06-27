/*
  ============================================================
  RECEPTOR DE ÁUDIO - ESP32 + nRF24L01 + DAC (DMA) + GF1002
  Core 3.x — payload fixo + CRC16 + clock adaptativo
  ============================================================
  GPIO25 --> R  (GF1002)      nRF24L01:
  GND    --> G  (GF1002)      VCC-->3.3V  | CE-->GPIO4
  5V     --> +POWER           GND-->GND   | CSN-->GPIO5
  GND    --> -POWER           SCK-->GPIO18| MOSI-->GPIO23
  ROUT   --> Alto-falante     MISO-->GPIO19
  ============================================================
*/

#include <SPI.h>
#include <RF24.h>
#include "driver/dac_continuous.h"

// ---- nRF24 ----
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "AUDIO";

// ---- Parâmetros de áudio ----
#define SAMPLE_RATE      8000
#define DAC_RATE         32000
#define UPSAMPLE_FACTOR  4
#define PAYLOAD_SIZE     32
#define OUT_BLOCK        32
#define OUT_UP           (OUT_BLOCK * UPSAMPLE_FACTOR)
#define BASE_INTERVAL_US 4000UL
#define TARGET_SAMPLES   1600    // 200ms de buffer alvo
#define MAX_CORRECTION   40

// ---- Limite de pacotes por iteração do loop ----
// Evita ler lixo infinito se o rádio travar
#define MAX_PKT_PER_LOOP 8

// ---- DAC DMA ----
dac_continuous_handle_t dacHandle = NULL;

// ---- Ring buffer ----
#define RING_SIZE  8192
uint8_t ring[RING_SIZE];
int writePos        = 0;
int readPos         = 0;
int bufferedSamples = 0;

uint8_t outBuf[OUT_UP];

// ---- Estatísticas ----
uint32_t pktReceived  = 0;
uint32_t pktDropped   = 0;
uint32_t underruns    = 0;
uint32_t pktThisWindow = 0;   // pacotes nos últimos 5s

bool pushPacket(uint8_t* data) {
  if (bufferedSamples + PAYLOAD_SIZE > RING_SIZE) return false;
  for (int i = 0; i < PAYLOAD_SIZE; i++) {
    ring[writePos] = data[i];
    writePos = (writePos + 1) % RING_SIZE;
  }
  bufferedSamples += PAYLOAD_SIZE;
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== RECEPTOR DE AUDIO nRF24 ===");

  // ---- DAC DMA ----
  dac_continuous_config_t dacCfg = {
    .chan_mask   = DAC_CHANNEL_MASK_CH0,
    .desc_num    = 4,
    .buf_size    = 512,
    .freq_hz     = DAC_RATE,
    .offset      = 0,
    .clk_src     = DAC_DIGI_CLK_SRC_DEFAULT,
    .chan_mode    = DAC_CHANNEL_MODE_SIMUL,
  };
  esp_err_t err = dac_continuous_new_channels(&dacCfg, &dacHandle);
  if (err != ESP_OK) {
    Serial.printf("ERRO DAC: 0x%x\n", err);
    while (1) delay(500);
  }
  dac_continuous_enable(dacHandle);
  Serial.println("DAC OK");

  // ---- nRF24 ----
  if (!radio.begin()) {
    Serial.println("ERRO: nRF24L01 nao encontrado!");
    while (1) delay(500);
  }
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(108);
  radio.setCRCLength(RF24_CRC_16);   // CRC 16 bits rejeita pacotes corrompidos
  radio.setAutoAck(false);
  radio.setPayloadSize(PAYLOAD_SIZE);
  // SEM enableDynamicPayloads — incompatível com AutoAck false
  radio.openReadingPipe(1, ADDRESS);
  radio.startListening();

  Serial.println("nRF24L01 OK");
  radio.printPrettyDetails();

  // Pré-buffer com silêncio
  memset(ring, 0x80, RING_SIZE);
  bufferedSamples = TARGET_SAMPLES;
  writePos = TARGET_SAMPLES % RING_SIZE;
  readPos  = 0;
  Serial.printf("Pre-buffer: %d amostras (%dms)\n",
                TARGET_SAMPLES, TARGET_SAMPLES * 1000 / SAMPLE_RATE);
  Serial.println("Aguardando pacotes...");
}

void loop() {
  // ---- 1) Recebe pacotes (máx MAX_PKT_PER_LOOP por iteração) ----
  uint8_t rxBuf[PAYLOAD_SIZE];
  int pktsLidos = 0;
  while (radio.available() && pktsLidos < MAX_PKT_PER_LOOP) {
    radio.read(rxBuf, PAYLOAD_SIZE);
    pktReceived++;
    pktThisWindow++;
    pktsLidos++;
    if (!pushPacket(rxBuf)) pktDropped++;
  }

  // ---- 2) DAC com clock adaptativo ----
  static uint32_t tNextDac  = 0;
  static int32_t  correction = 0;

  uint32_t now = micros();
  if ((int32_t)(now - tNextDac) >= 0) {
    int error = bufferedSamples - TARGET_SAMPLES;
    correction = -(error / 8);
    if (correction >  MAX_CORRECTION) correction =  MAX_CORRECTION;
    if (correction < -MAX_CORRECTION) correction = -MAX_CORRECTION;
    tNextDac += BASE_INTERVAL_US + correction;

    int out = 0;
    if (bufferedSamples >= OUT_BLOCK) {
      for (int i = 0; i < OUT_BLOCK; i++) {
        uint8_t s = ring[readPos];
        readPos = (readPos + 1) % RING_SIZE;
        for (int j = 0; j < UPSAMPLE_FACTOR; j++) outBuf[out++] = s;
      }
      bufferedSamples -= OUT_BLOCK;
    } else {
      memset(outBuf, 0x80, OUT_UP);
      underruns++;
    }

    size_t written = 0;
    dac_continuous_write(dacHandle, outBuf, OUT_UP, &written, 50);
  }

  // ---- 3) Log a cada 5s ----
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 5000) {
    lastLog = millis();
    float bufMs = (float)bufferedSamples / SAMPLE_RATE * 1000.0f;
    Serial.printf("[STATUS] RX: %lu (+%lu/5s) | Drop: %lu | Underrun: %lu | Buffer: %d (%.0fms) | Corr: %+ldus\n",
                  pktReceived, pktThisWindow, pktDropped, underruns,
                  bufferedSamples, bufMs, correction);
    pktThisWindow = 0;
  }
}
