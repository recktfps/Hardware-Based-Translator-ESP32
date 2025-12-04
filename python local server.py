# pip install flask             # Web framework for HTTP server
# pip install openai-whisper    # Whisper ASR (Speech-to-Text) model by OpenAI:contentReference[oaicite:2]{index=2}
# pip install googletrans==4.0.0-rc1   # Googletrans translation library (free)
# pip install gtts              # Google Text-to-Speech for TTS
# pip install pydub             # For audio format conversion (MP3 to WAV)

from flask import Flask, request, send_file, jsonify, make_response
import whisper
from googletrans import Translator
from gtts import gTTS
from pydub import AudioSegment
import os, io, base64, time

# Load models at startup
model = whisper.load_model("base")  # or "small", etc., loads Whisper STT model:contentReference[oaicite:3]{index=3}
translator = Translator()          # Googletrans translator

app = Flask(__name__)

@app.route('/translate', methods=['POST'])
def translate_route():
    # 1. Receive audio from ESP32 (raw bytes)
    audio_bytes = request.data  # Expecting raw WAV/PCM data in request body
    if not audio_bytes:
        return "No audio received", 400

    # Optionally save the input for debugging
    with open("input.wav", "wb") as f:
        f.write(audio_bytes)

    start_time = time.time()
    # 2. Speech-to-Text with Whisper
    # Ensure the audio is in a common format (e.g., 16 kHz mono WAV). 
    # If the ESP32 sent raw PCM, we might need to add a WAV header. 
    # Here we assume ESP32 sends a WAV file bytes.
    result = model.transcribe("input.wav")  # transcribe audio file:contentReference[oaicite:4]{index=4}
    original_text = result["text"]
    src_lang = result.get("language", "")  # detected language code, e.g., 'en'
    print(f"Whisper STT result: {original_text} (lang={src_lang})")

    # 3. Translation
    target_lang = "es"  # e.g., translate to Spanish; adjust as needed or make configurable
    try:
        trans = translator.translate(original_text, dest=target_lang)  # translate text:contentReference[oaicite:5]{index=5}
        translated_text = trans.text
    except Exception as e:
        print("Translation failed:", e)
        return "Translation error", 500
    print(f"Translated to ({target_lang}): {translated_text}")

    # 4. Text-to-Speech
    try:
        tts = gTTS(text=translated_text, lang=target_lang, slow=False)  # synthesize:contentReference[oaicite:6]{index=6}
        tts.save("output.mp3")
    except Exception as e:
        print("gTTS failed:", e)
        return "TTS error", 500

    # Convert MP3 to WAV (PCM 16 kHz mono) for ESP32 playback
    audio = AudioSegment.from_mp3("output.mp3")
    audio = audio.set_frame_rate(16000).set_channels(1)
    audio.export("output.wav", format="wav")
    # Read the WAV data
    out_bytes = open("output.wav","rb").read()
    duration = time.time() - start_time
    print(f"Total processing time: {duration:.2f}s")

    # 5. Send back the WAV audio to ESP32
    response = make_response(out_bytes)
    response.headers.set('Content-Type', 'audio/wav')
    return response
