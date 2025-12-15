from flask import Flask, request, jsonify, send_file
import whisper
from googletrans import Translator
from gtts import gTTS
import numpy as np
import io
import os
from pydub import AudioSegment
import wave

app = Flask(__name__)

# -----------------------------
# Load Whisper (Speech to Text)
# -----------------------------
print("Loading Whisper BASE model...")
model = whisper.load_model("base")
print("Model loaded.")

translator = Translator()
SAMPLE_RATE = 16000


# -----------------------------
# Convert raw PCM to float numpy
# -----------------------------
def pcm16_to_float32(pcm_bytes):
    pcm_data = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32)
    return pcm_data / 32768.0


# -----------------------------
# SPEECH → TEXT ENDPOINT
# -----------------------------
@app.route("/stt", methods=["POST"])
def stt():
    audio_bytes = request.data

    if not audio_bytes:
        return jsonify({"error": "No audio received"}), 400

    # Convert PCM → float32 for Whisper
    audio_np = pcm16_to_float32(audio_bytes)

    print("Running Whisper STT...")
    result = model.transcribe(audio_np, fp16=False)
    text = result["text"].strip()

    print("Recognized:", text)

    # Save text for Node 2
    with open("last_text.txt", "w") as f:
        f.write(text)

    return jsonify({"text": text})


# -----------------------------
# TEXT → SPANISH SPEECH ENDPOINT
# -----------------------------
@app.route("/translate", methods=["POST"])
def translate():
    if not os.path.isfile("last_text.txt"):
        return "No text available", 400

    # Read English text
    with open("last_text.txt", "r") as f:
        english = f.read().strip()

    print("Translating:", english)
    spanish = translator.translate(english, dest="es").text
    print("Spanish:", spanish)

    # -----------------------------
    # Generate MP3 using gTTS
    # -----------------------------
    tts = gTTS(spanish, lang="es")
    tts.save("tts.mp3")

    # -----------------------------
    # Convert MP3 → WAV using pydub
    # -----------------------------
    audio = AudioSegment.from_mp3("tts.mp3")
    audio = audio.set_frame_rate(16000).set_channels(1)

    # Export WAV to memory buffer
    wav_bytes = io.BytesIO()
    audio.export(wav_bytes, format="wav")

    wav_bytes.seek(0)

    return send_file(
        wav_bytes,
        mimetype="audio/wav",
        as_attachment=False,
        download_name="output.wav"
    )


# -----------------------------
# RUN SERVER
# -----------------------------
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
