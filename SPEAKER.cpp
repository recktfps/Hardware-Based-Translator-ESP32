/*

FIle: Speaker.cpp 
Functionality: Uses our I2S speaker and amplifier at an amplitude of 3000 to give us speech output. 
Designed a Google Translate Flask server that decodes input text into Spanish. This program lets us 
hear that audio. This version lets the speaker play LIVE audio instead of being button triggered.
Course: CECS 460 
Students: James Henry, Ivan Martinez, Sheesh



*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// ------------------------------
// WIFI SETTINGS
// ------------------------------
const char* ssid     = "WIFI";
const char* password = "PW";

// Whisper server WAV → TTS endpoint
String serverURL = "http://nnn.nnn.nn.nnn:nnnn/translate";

// -------------------------
// I2S PINS for MAX98357A
// -------------------------
const int I2S_BCLK = 26;  // BCLK
const int I2S_LRC  = 25;  // LRCLK (WS)
const int I2S_DOUT = 27;  // DIN (data)

// -------------------------
// Tone settings
// -------------------------
const int   SAMPLE_RATE = 16000;   // 16 kHz sample rate
const float TONE_FREQ   = 440.0;   // Frequency of beep (Hz)
const int   AMPLITUDE   = 3000;    // Requested amplitude

// -------------------------
// I2S Configuration
// -------------------------
i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 6,
  .dma_buf_len = 60,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .fixed_mclk = 0
};

// I2S Pin mapping
i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_BCLK,
  .ws_io_num = I2S_LRC,
  .data_out_num = I2S_DOUT,
  .data_in_num = I2S_PIN_NO_CHANGE
};

// ------------------------------
// Audio buffer for translation
// ------------------------------
uint8_t* audioBuffer = nullptr;
int      audioLength = 0;

// ------------------------------
// Simple hash to detect new audio
// ------------------------------
uint32_t simpleHash(const uint8_t* data, size_t len) {
  uint32_t h = 5381;
  for (size_t i = 0; i < len; i++) {
    h = ((h << 5) + h) + data[i]; // h * 33 + data[i]
  }
  return h;
}

// ------------------------------
// Test tone (same style as before)
// ------------------------------
void playTestTone(float seconds) {
  Serial.println("Playing test tone...");

  const int totalSamples = (int)(SAMPLE_RATE * seconds);
  float phase = 0.0f;

  for (int i = 0; i < totalSamples; i++) {
    int16_t sample;

    // SAME square-wave logic as our original script
    if (sin(phase) > 0)
      sample = AMPLITUDE;
    else
      sample = -AMPLITUDE;

    phase += 2.0f * PI * TONE_FREQ / SAMPLE_RATE;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;

    size_t bytes_written;
    i2s_write(I2S_NUM_0, &sample, sizeof(sample), &bytes_written, portMAX_DELAY);
  }

  Serial.println("Test tone done.");
}

// ------------------------------
// Request WAV from server
// Returns true if we got valid audio
// and outputs a hash of the PCM data.
// ------------------------------
bool requestAudio(uint32_t &outHash) {
  HTTPClient http;

  Serial.println("Checking server for translation...");
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST("{\"request\":\"speak\"}");

  if (httpCode != 200) {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient *stream = http.getStreamPtr();

  Serial.print("HTTP 200 OK, content length: ");
  Serial.println(contentLength);

  if (contentLength <= 44) {
    Serial.println("ERROR: Invalid or too-small WAV.");
    http.end();
    return false;
  }

  // Free old buffer
  if (audioBuffer != nullptr) {
    free(audioBuffer);
    audioBuffer = nullptr;
  }

  audioBuffer = (uint8_t*)malloc(contentLength);
  if (!audioBuffer) {
    Serial.println("ERROR: malloc failed.");
    http.end();
    return false;
  }

  // Read all data
  int bytesRead = 0;
  while (bytesRead < contentLength) {
    int r = stream->readBytes(audioBuffer + bytesRead, contentLength - bytesRead);
    if (r <= 0) {
      Serial.println("ERROR: Stream read failed.");
      break;
    }
    bytesRead += r;
  }

  audioLength = bytesRead;
  Serial.print("Received bytes: ");
  Serial.println(audioLength);

  http.end();

  if (audioLength <= 44) {
    Serial.println("ERROR: Not enough data after read.");
    return false;
  }

  // Hash only the PCM portion (skip 44-byte WAV header)
  const int WAV_HEADER_SIZE = 44;
  uint8_t* pcmData = audioBuffer + WAV_HEADER_SIZE;
  int      pcmBytes = audioLength - WAV_HEADER_SIZE;

  outHash = simpleHash(pcmData, pcmBytes);
  return true;
}

// ------------------------------
// Play audio from current buffer
// ------------------------------
void playAudioFromBuffer() {
  if (!audioBuffer || audioLength <= 44) {
    Serial.println("ERROR: No valid audio in buffer.");
    return;
  }

  Serial.println("Playing translated audio...");

  const int WAV_HEADER_SIZE = 44;
  uint8_t* pcmData = audioBuffer + WAV_HEADER_SIZE;
  int      pcmBytes = audioLength - WAV_HEADER_SIZE;

  size_t bytes_written = 0;
  i2s_write(I2S_NUM_0, pcmData, pcmBytes, &bytes_written, portMAX_DELAY);

  Serial.print("PCM bytes written: ");
  Serial.println(bytes_written);
  Serial.println("Translation playback done.");
}

// ------------------------------
// Setup
// ------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  // WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi CONNECTED!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // I2S init (same as our working tone script)
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  Serial.println("Node 2 Ready.");
  Serial.println("Type 't' for test tone. Translations will play automatically.");
}

// ------------------------------
// Loop
// ------------------------------
void loop() {
  // Manual test tone (for sanity check)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't') {
      playTestTone(3.0f);   // 3-second test tone
    }
  }

  // --- Auto-poll server for LAST translation and play ONCE ---

  static uint32_t lastHash = 0;
  static bool haveLastHash = false;
  static unsigned long lastCheckMs = 0;
  const unsigned long CHECK_INTERVAL_MS = 1000;  // poll once per second

  unsigned long now = millis();
  if (now - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = now;

    uint32_t newHash = 0;
    if (requestAudio(newHash)) {
      if (!haveLastHash || newHash != lastHash) {
        Serial.println("New translation detected. Playing once.");
        lastHash = newHash;
        haveLastHash = true;
        playAudioFromBuffer();
      } else {
        Serial.println("Same translation as last time, not playing.");
      }
    }
  }
}
