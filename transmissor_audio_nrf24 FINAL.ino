/*
  ============================================================
  TRANSMISSOR DE ÁUDIO - ESP32 + nRF24L01
  Fonte: array audio_data[] gerado pelo converter.py
  ============================================================
  Coloque na mesma pasta que audio.h

  nRF24L01:
  VCC-->3.3V | GND-->GND | CE-->GPIO4  | CSN-->GPIO5
  SCK-->GPIO18| MOSI-->GPIO23| MISO-->GPIO19
  ============================================================
*/

#include <SPI.h>
#include <RF24.h>
#include "audio.h"

#define CE_PIN       4
#define CSN_PIN      5
RF24 radio(CE_PIN, CSN_PIN);
const byte ADDRESS[6] = "AUDIO";

#define SAMPLE_RATE    8000
#define PAYLOAD_SIZE   32

// Tempo ideal entre pacotes em microssegundos
// 32 amostras a 8000 Hz = 4000 µs
// Subtrai ~200 µs para compensar o tempo do radio.write()
#define PACKET_INTERVAL_US  3800UL

void setup() {
  Serial.begin(115200);
  Serial.println("=== TRANSMISSOR DE AUDIO ===");
  Serial.printf("Amostras    : %u\n", audio_len);
  Serial.printf("Duracao     : %.1f s\n", (float)audio_len / SAMPLE_RATE);
  Serial.printf("Pacotes     : %u\n", (audio_len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE);

  if (!radio.begin()) {
    Serial.println("ERRO: nRF24L01 nao encontrado!");
    while (1) delay(500);
  }

  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(108);
  radio.setPayloadSize(PAYLOAD_SIZE);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(false);
  radio.setRetries(0, 0);
  radio.openWritingPipe(ADDRESS);
  radio.stopListening();

  Serial.println("nRF24L01 OK");
  Serial.println("Iniciando em 2s...");
  delay(2000);
}

void loop() {
  uint8_t txBuf[PAYLOAD_SIZE];
  uint32_t pos         = 0;
  uint32_t packetCount = 0;
  uint32_t tStart      = millis();
  uint32_t tNext       = micros();   // Próximo envio agendado

  Serial.println("Transmitindo...");

  while (pos < audio_len) {
    // Aguarda o momento certo (sem bloquear com delay fixo)
    while ((int32_t)(micros() - tNext) < 0) {
      // busy-wait preciso
    }
    tNext += PACKET_INTERVAL_US;

    // Monta payload
    uint16_t n = PAYLOAD_SIZE;
    if (pos + n > audio_len) {
      n = audio_len - pos;
      memset(txBuf, 0x80, PAYLOAD_SIZE);
    }
    for (uint16_t i = 0; i < n; i++) {
      txBuf[i] = pgm_read_byte(&audio_data[pos + i]);
    }

    radio.write(txBuf, PAYLOAD_SIZE);
    pos += n;
    packetCount++;

    // Log a cada 500 pacotes
    if (packetCount % 500 == 0) {
      float progresso = (float)pos / audio_len * 100.0f;
      Serial.printf("  %.1f%% — pacote %lu / %lu\n",
                    progresso, packetCount,
                    (uint32_t)((audio_len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE));
    }
  }

  Serial.printf("Concluido em %lu ms — %lu pacotes\n",
                millis() - tStart, packetCount);
  Serial.println("Repetindo em 1s...\n");
  delay(1000);
}