/*

File: microphone.cpp
Functionality: Press the button, and the onboard LED turns blue. This means the microphone is listening for audio/input. This speech wave gets encoded and sent to node #2 to translate to spanish. 
Group: James Henry, Ivan Martinez, Sheesh
Class: CECS 460 - System on Chip Design

*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "driver/i2s.h"

// ------------------------------
// WiFi Settings
// ------------------------------
const char* ssid = "WIFI NAME";
const char* password = "PASSWORD";

// Flask server IP (your computer)
String serverURL = "IP FOR SERVER";
// ------------------------------
// Microphone Pins (INMP441)
// ------------------------------
#define I2S_BCLK       26
#define I2S_LRCLK      25
#define I2S_DIN_MIC    22

#define BUTTON_PIN     32
#define LED_PIN        2

// ------------------------------
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = -1,
    .data_in_num = I2S_DIN_MIC
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ------------------------------
// RECORD AUDIO INTO BUFFER
// ------------------------------
#define RECORD_TIME 2       // seconds
#define SAMPLE_RATE 16000
#define NUM_SAMPLES (RECORD_TIME * SAMPLE_RATE)

int16_t audioBuffer[NUM_SAMPLES];

void recordAudio() {
  size_t bytes_read = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    i2s_read(I2S_NUM_0, &audioBuffer[i], sizeof(int16_t), &bytes_read, portMAX_DELAY);
  }
}

// ------------------------------
// SEND WAV TO SERVER
// ------------------------------
void sendToServer() {
  HTTPClient http;

  Serial.println("Sending audio to server...");

  http.begin(serverURL);
  http.addHeader("Content-Type", "audio/wav");

  int httpCode = http.POST((uint8_t*)audioBuffer, sizeof(audioBuffer));

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("Server response: ");
    Serial.println(payload);
  } else {
    Serial.print("POST failed: ");
    Serial.println(httpCode);
  }

  http.end();
}

// ------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  i2s_install();

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ------------------------------
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Recording...");
    digitalWrite(LED_PIN, HIGH);

    recordAudio();
    sendToServer();

    digitalWrite(LED_PIN, LOW);
    delay(2000);
  }
}
