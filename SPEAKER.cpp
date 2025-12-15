/*

FIle: Speaker.cpp 
Functionality: Uses our I2S speaker and amplifier at an amplitude of 3000 to give us speech output. 
Designed a Google Translate Flask server that decodes input text into Spanish. This program lets us 
hear that audio.
Course: CECS 460 
Students: James Henry, Ivan Martinez, Sheesh

*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <AudioOutputI2S.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourcePROGMEM.h>

/*

Note: Plug into COM7 (right side) and flash program to set up the wifi. 
Once it is set up, we can type 't' in the terminal or 'p' to hear the audio. 
Depends on the Node 1 running it's script first. 

*/
// ------------------------------
// WIFI SETTINGS
// ------------------------------
const char* ssid = "THE NETWORK NAME"
const char* password = "PASSWORD OF NETWORK";

// Whisper server WAV endpoint
String serverURL = "CHANGE TO WHATEVER PYTHON GIVES IN CLASS";

// ------------------------------
// I2S PINS FOR MAX98357A
// ------------------------------
#define I2S_BCLK 26
#define I2S_LRCLK 25
#define I2S_DOUT 27

// ------------------------------
// AUDIO OBJECTS
// ------------------------------
AudioOutputI2S *out;
AudioGeneratorWAV *wav;
AudioFileSourcePROGMEM *file;

// Dynamic buffer
uint8_t* audioBuffer = nullptr;
int audioLength = 0;

// ------------------------------
// Setup
// ------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  // WIFI connect
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi CONNECTED!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // I2S setup
  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
  out->SetOutputModeMono(true);
  out->SetGain(0.1f);  // quiet & safe volume

  Serial.println("Node 2 Ready. Type 'p' to play translation or 't' for tone test.");
}

// ------------------------------
// Request WAV audio
// ------------------------------
void requestAudio() {
  HTTPClient http;

  Serial.println("Requesting translated audio...");
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST("{\"request\":\"speak\"}");

  if (httpCode == 200) {
    WiFiClient* stream = http.getStreamPtr();
    audioLength = stream->available();

    Serial.print("Received audio size: ");
    Serial.println(audioLength);

    if (audioLength <= 44) {
      Serial.println("ERROR: Invalid or empty WAV.");
      return;
    }

    // Free existing buffer
    if (audioBuffer != nullptr) free(audioBuffer);

    audioBuffer = (uint8_t*) malloc(audioLength);
    if (!audioBuffer) {
      Serial.println("ERROR: Memory allocation failed!");
      return;
    }

    stream->readBytes(audioBuffer, audioLength);
    Serial.println("Audio received OK!");

  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
  }

  http.end();
}

// ------------------------------
// Play WAV audio
// ------------------------------
void playAudio() {
  if (!audioBuffer || audioLength < 44) {
    Serial.println("ERROR: No audio loaded.");
    return;
  }

  Serial.println("Starting playback...");

  file = new AudioFileSourcePROGMEM(audioBuffer, audioLength);
  wav = new AudioGeneratorWAV();

  wav->begin(file, out);

  while (wav->isRunning()) {
    if (!wav->loop()) {
      wav->stop();
      break;
    }
  }

  Serial.println("Playback ended.");
}

// ------------------------------
// Loop
// ------------------------------
void loop() {

  if (Serial.available()) {
    char c = Serial.read();

    // FETCH & PLAY TTS FROM SERVER
    if (c == 'p') {
      requestAudio();
      playAudio();
    }

    // SAFE CONTINUOUS TEST TONE (Amplitude = 3000)
    if (c == 't') {
      Serial.println("Playing tone (3000 amplitude) continuously...");

      while (true) {
        for (int i = 0; i < 16000; i++) {
          float theta = 2.0f * 3.14159f * 440.0f * i / 16000.0f;
          int16_t sample = (int16_t)(sin(theta) * 3000);

          int16_t stereoSample[2] = { sample, sample };
          out->ConsumeSample(stereoSample);
        }
      }
    }
  }
}

