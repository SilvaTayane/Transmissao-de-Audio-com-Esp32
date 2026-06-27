#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#include "audio.h"

RF24 radio(4,5);

const byte endereco[6]="00001";

struct Pacote
{
  uint16_t indice;
  uint8_t dados[30];
};

Pacote pacote;

uint8_t buffer[AUDIO_SIZE];

uint32_t recebidos=0;

void setup()
{
  Serial.begin(115200);

  if(!radio.begin())
  {
    Serial.println("Erro NRF");
    while(1);
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);

  radio.openReadingPipe(1,endereco);
  radio.startListening();

  Serial.println("Aguardando...");
}

void loop()
{
  if(radio.available())
  {
    radio.read(&pacote,sizeof(pacote));

    memcpy(buffer+pacote.indice,
           pacote.dados,
           30);

    recebidos+=30;

    if(recebidos%300==0)
    {
      Serial.print(recebidos);
      Serial.print(" / ");
      Serial.println(AUDIO_SIZE);
    }

    if(recebidos>=AUDIO_SIZE)
    {
      Serial.println("Audio recebido!");

      recebidos=0;
    }
  }
}