/*

File: microphone.cpp
Functionality: Press the button, and the onboard LED turns blue. This means the microphone is listening for audio/input. This speech wave gets encoded and sent to node #2 to translate to spanish. 
Group: James Henry, Ivan Martinez, Sheesh
Class: CECS 460 - System on Chip Deisng 

*/

#include <Arduino.h>
#include "driver/i2s.h"

// ------------------------------
// User pins
// ------------------------------
#define I2S_BCLK       26 // SCK
#define I2S_LRCLK      25 // WS
#define I2S_DIN_MIC    22 // SD

#define BUTTON_PIN     32      // button to GND
#define LED_PIN        2       // onboard LED (assume GPIO2)

// ------------------------------
// I2S CONFIG FOR INMP441 MIC
// ------------------------------
void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // L/R pin grounded
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 128,
    .use_apll = false,
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = -1,         // we only RECEIVE (mic)
    .data_in_num = I2S_DIN_MIC
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ----------------------------------------------------------
// Read a single audio sample & convert to readable amplitude
// ----------------------------------------------------------
int32_t readMicSample() {
  int32_t sample = 0;
  size_t bytesRead = 0;

  i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytesRead, portMAX_DELAY);

  // INMP441 outputs 24-bit data inside 32-bit word
  sample = sample >> 8;   // convert 32→24 bit

  return abs(sample);
}

// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // button to GND
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  i2s_install();

  Serial.println("INMP441 MIC + BUTTON READY.");
  Serial.println("Hold button to record, release to stop.");
}

// ----------------------------------------------------------
void loop() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed) {
    // Turn LED BLUE (if normal LED → just ON)
    digitalWrite(LED_PIN, HIGH);

    // Read mic amplitude continuously
    int32_t amp = readMicSample();
    Serial.println(amp);   // send amplitude to terminal
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}
