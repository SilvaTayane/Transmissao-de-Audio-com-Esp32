/*
  ============================================================
  RECEPTOR DE ÁUDIO - ESP32 + nRF24L01 + DAC (DMA) + GF1002
  Core 3.x — dac_continuous com upsample 8kHz -> 16kHz
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

// ---- Áudio ----
#define SAMPLE_RATE      8000    // Taxa real do audio.h
#define DAC_RATE         32000   // Taxa enviada ao DAC (múltiplo de SAMPLE_RATE)
#define UPSAMPLE_FACTOR  (DAC_RATE / SAMPLE_RATE)  // = 4 (cada amostra repetida 4x)
#define PAYLOAD_SIZE     32

// ---- DAC DMA ----
dac_continuous_handle_t dacHandle = NULL;

// ---- Ring buffer (amostras originais 8kHz) ----
#define RING_SIZE  4096          // ~512ms a 8kHz
uint8_t  ring[RING_SIZE];
volatile int writePos        = 0;
volatile int readPos         = 0;
volatile int bufferedSamples = 0;

// ---- Buffer de saída upsampled (para o DMA) ----
// Bloco de 32 amostras originais → 128 amostras para o DAC
#define OUT_BLOCK  32
#define OUT_UP     (OUT_BLOCK * UPSAMPLE_FACTOR)
uint8_t outBuf[OUT_UP];

// ---- Estatísticas ----
uint32_t pktReceived = 0;
uint32_t pktDropped  = 0;
uint32_t underruns   = 0;

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
  Serial.println("=== RECEPTOR DE AUDIO nRF24 (DAC DMA 32kHz) ===");
  Serial.printf("Audio real   : %d Hz\n", SAMPLE_RATE);
  Serial.printf("DAC clock    : %d Hz (upsample x%d)\n", DAC_RATE, UPSAMPLE_FACTOR);

  // ---- DAC contínuo com DMA ----
  dac_continuous_config_t dacCfg = {
    .chan_mask   = DAC_CHANNEL_MASK_CH0,   // GPIO25
    .desc_num    = 8,
    .buf_size    = 2048,
    .freq_hz     = DAC_RATE,               // 32000 Hz (dentro do range suportado)
    .offset      = 0,
    .clk_src     = DAC_DIGI_CLK_SRC_DEFAULT,
    .chan_mode    = DAC_CHANNEL_MODE_SIMUL,
  };
  esp_err_t err = dac_continuous_new_channels(&dacCfg, &dacHandle);
  if (err != ESP_OK) {
    Serial.printf("ERRO DAC: 0x%x — tente mudar DAC_RATE para 44100\n", err);
    while (1) delay(500);
  }
  dac_continuous_enable(dacHandle);
  Serial.println("DAC DMA OK");

  // ---- nRF24 ----
  if (!radio.begin()) {
    Serial.println("ERRO: nRF24L01 nao encontrado!");
    while (1) delay(500);
  }
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(108);
  radio.setPayloadSize(PAYLOAD_SIZE);
  radio.setCRCLength(RF24_CRC_8);
  radio.setAutoAck(false);
  radio.openReadingPipe(1, ADDRESS);
  radio.startListening();
  Serial.println("nRF24L01 OK");

  // Pré-preenche com silêncio
  memset(ring, 0x80, RING_SIZE);
  bufferedSamples = RING_SIZE / 4;
  writePos = RING_SIZE / 4;
  readPos  = 0;

  Serial.println("Aguardando pacotes...");
}

void loop() {
  uint8_t rxBuf[PAYLOAD_SIZE];

  // ---- Recebe pacotes ----
  while (radio.available()) {
    radio.read(rxBuf, PAYLOAD_SIZE);
    pktReceived++;
    if (!pushPacket(rxBuf)) pktDropped++;
  }

  // ---- Envia ao DAC em blocos upsampled ----
  while (bufferedSamples >= OUT_BLOCK) {
    // Lê OUT_BLOCK amostras do ring e repete cada uma UPSAMPLE_FACTOR vezes
    int out = 0;
    for (int i = 0; i < OUT_BLOCK; i++) {
      uint8_t s = ring[readPos];
      readPos = (readPos + 1) % RING_SIZE;
      for (int j = 0; j < UPSAMPLE_FACTOR; j++) {
        outBuf[out++] = s;
      }
    }
    bufferedSamples -= OUT_BLOCK;

    size_t written = 0;
    esp_err_t e = dac_continuous_write(dacHandle, outBuf, OUT_UP, &written, 20);
    if (e != ESP_OK || written < OUT_UP) underruns++;
  }

  // Silêncio se buffer vazio
  if (bufferedSamples == 0) {
    memset(outBuf, 0x80, OUT_UP);
    size_t written = 0;
    dac_continuous_write(dacHandle, outBuf, OUT_UP, &written, 10);
    underruns++;
  }

  // ---- Log a cada 5s ----
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 5000) {
    lastLog = millis();
    float bufMs = (float)bufferedSamples / SAMPLE_RATE * 1000.0f;
    Serial.printf("[STATUS] Recebidos: %lu | Descartados: %lu | Underruns: %lu | Buffer: %d amostras (%.0f ms)\n",
                  pktReceived, pktDropped, underruns, bufferedSamples, bufMs);
  }
}
