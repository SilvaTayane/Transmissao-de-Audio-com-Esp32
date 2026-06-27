import wave
import numpy as np

ARQUIVO = "audios/audioteste.wav"

with wave.open(ARQUIVO, 'rb') as wav:

    canais = wav.getnchannels()
    taxa = wav.getframerate()
    largura = wav.getsampwidth()
    total = wav.getnframes()

    print("Canais :", canais)
    print("Taxa   :", taxa)
    print("Bits   :", largura * 8)

    audio = wav.readframes(total)

# -------------------------
# converte para numpy
# -------------------------

if largura == 1:
    dados = np.frombuffer(audio, dtype=np.uint8)

elif largura == 2:
    dados = np.frombuffer(audio, dtype=np.int16)

    # 16 bits -> 8 bits
    dados = ((dados.astype(np.int32) + 32768) >> 8).astype(np.uint8)

else:
    raise Exception("Formato nao suportado")

# -------------------------
# stereo -> mono
# -------------------------

if canais == 2:
    dados = dados.reshape(-1, 2)
    dados = dados.mean(axis=1).astype(np.uint8)

# -------------------------
# reduz para 8000 Hz
# -------------------------

if taxa != 8000:

    fator = taxa / 8000

    indices = np.arange(0, len(dados), fator)

    indices = indices.astype(int)

    dados = dados[indices]

print()

print("Total de bytes:", len(dados))

# -------------------------
# gera audio.h
# -------------------------

with open("audio.h", "w") as f:

    f.write("#ifndef AUDIO_H\n")
    f.write("#define AUDIO_H\n\n")

    f.write(f"const unsigned int audio_len = {len(dados)};\n\n")

    f.write("const uint8_t audio_data[] PROGMEM = {\n")

    for i, b in enumerate(dados):

        f.write(f"0x{b:02X},")

        if (i + 1) % 16 == 0:
            f.write("\n")

    f.write("\n};\n\n")

    f.write("#endif\n")

print("audio.h criado com sucesso!")