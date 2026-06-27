import wave
import numpy as np

ARQUIVO = "audios/audioteste.wav"

with wave.open(ARQUIVO, "rb") as wav:
    canais = wav.getnchannels()
    bits = wav.getsampwidth()
    taxa = wav.getframerate()
    total = wav.getnframes()

    print("Canais:", canais)
    print("Bits:", bits * 8)
    print("Taxa:", taxa)
    print("Amostras:", total)

    audio = wav.readframes(total)

dados = np.frombuffer(audio, dtype=np.uint8)

with open("audio.h", "w") as f:
    f.write("#ifndef AUDIO_H\n")
    f.write("#define AUDIO_H\n\n")

    f.write(f"const unsigned int audio_len = {len(dados)};\n")
    f.write("const uint8_t audio_data[] PROGMEM = {\n")

    for i, b in enumerate(dados):
        f.write(f"0x{b:02X},")
        if (i + 1) % 16 == 0:
            f.write("\n")

    f.write("\n};\n\n#endif")

print("Arquivo audio.h gerado com sucesso!")