/*
  ============================================================
  TRANSMISSOR DE ÁUDIO - ESP32 + nRF24L01
  Fonte: array audio_data[] gerado pelo converter.py
  ============================================================
  Coloque este arquivo na mesma pasta que audio.h

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
#include "audio.h"   // <-- gerado pelo converter.py

// ---- nRF24 ----
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "AUDIO";

// ---- Configurações de áudio ----
// Ajuste SAMPLE_RATE para o mesmo valor usado no converter.py
#define SAMPLE_RATE   8000
#define PAYLOAD_SIZE  32

// Intervalo entre pacotes:
// 32 amostras / 8000 Hz = 4000 µs por pacote
#define PACKET_INTERVAL_US  (PAYLOAD_SIZE * 1000000UL / SAMPLE_RATE)

void setup() {
  Serial.begin(115200);
  Serial.println("=== TRANSMISSOR DE ÁUDIO (audio.h) ===");
  Serial.printf("Total de amostras: %u\n", audio_len);
  Serial.printf("Duração estimada: %.1f s\n", (float)audio_len / SAMPLE_RATE);
  Serial.printf("Intervalo entre pacotes: %lu µs\n", PACKET_INTERVAL_US);

  if (!radio.begin()) {
    Serial.println("ERRO: nRF24L01 não encontrado!");
    while (1) delay(500);
  }

  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(108);
  radio.setPayloadSize(PAYLOAD_SIZE);
  radio.setCRCLength(RF24_CRC_8);
  radio.setAutoAck(false);
  radio.setRetries(0, 0);
  radio.openWritingPipe(ADDRESS);
  radio.stopListening();

  Serial.println("nRF24L01 OK");
  Serial.println("Iniciando transmissão em 3 segundos...");
  delay(3000);
}

void loop() {
  uint8_t txBuffer[PAYLOAD_SIZE];
  uint32_t pos = 0;
  uint32_t packetCount = 0;
  uint32_t startTime = millis();

  Serial.println("Transmitindo...");

  while (pos < audio_len) {
    uint32_t tStart = micros();

    uint16_t bytesToSend = PAYLOAD_SIZE;
    if (pos + bytesToSend > audio_len) {
      bytesToSend = audio_len - pos;
      memset(txBuffer, 0x80, PAYLOAD_SIZE);  // Silêncio no final
    }

    // Lê do PROGMEM para o buffer
    for (uint16_t i = 0; i < bytesToSend; i++) {
      txBuffer[i] = pgm_read_byte(&audio_data[pos + i]);
    }

    radio.write(txBuffer, PAYLOAD_SIZE);
    pos += bytesToSend;
    packetCount++;

    // Timing preciso para manter o sample rate correto
    uint32_t elapsed = micros() - tStart;
    if (elapsed < PACKET_INTERVAL_US) {
      delayMicroseconds(PACKET_INTERVAL_US - elapsed);
    }
  }

  uint32_t totalMs = millis() - startTime;
  Serial.printf("Concluido! %lu pacotes em %lu ms\n", packetCount, totalMs);
  Serial.println("Repetindo em 2 segundos...\n");
  delay(2000);
}
