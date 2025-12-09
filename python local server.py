from flask import Flask, request, make_response
import whisper
from googletrans import Translator
from gtts import gTTS
from pydub import AudioSegment
import ssl
import certifi

# Fix SSL certificate verification
ssl._create_default_https_context = ssl._create_unverified_context

app = Flask(__name__)

print("Loading Whisper model...")
model = whisper.load_model("base")
translator = Translator()

@app.route('/translate', methods=['POST'])
def translate_audio():

    audio_bytes = request.data
    if not audio_bytes:
        return "No data", 400

    with open("input.wav", "wb") as f:
        f.write(audio_bytes)

    # ---------- Whisper STT ----------
    print("Running Whisper...")
    res = model.transcribe("input.wav")
    original = res["text"]
    print("STT:", original)

    # ---------- Translation ----------
    translated = translator.translate(original, dest="es").text
    print("Translated:", translated)

    # ---------- TTS ----------
    tts = gTTS(translated, lang="es")
    tts.save("tts.mp3")

    # Convert MP3 → WAV (16kHz mono)
    audio = AudioSegment.from_mp3("tts.mp3")
    audio = audio.set_frame_rate(16000).set_channels(1)
    audio.export("output.wav", format="wav")

    out_bytes = open("output.wav", "rb").read()
    response = make_response(out_bytes)
    response.headers.set("Content-Type", "audio/wav")

    return response

app.run(host="0.0.0.0", port=5001)
