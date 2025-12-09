#include <WiFi.h>
#include <HTTPClient.h>
#include "driver/i2s.h"

// -------------------- WiFi --------------------
const char* WIFI_SSID = "Ivan";
const char* WIFI_PASS = "basuraway";

// -------------------- Server --------------------
const char* SERVER_URL = "http://10.39.36.213:5001/translate";

// -------------------- I2S MIC CONFIG --------------------
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define SAMPLE_BITS     I2S_BITS_PER_SAMPLE_32BIT

#define I2S_BCLK        26
#define I2S_LRCLK       25
#define I2S_DIN_MIC     22   // INMP441 SD pin

// 3 seconds of audio
#define RECORD_TIME_SEC 3
#define NUM_SAMPLES     (SAMPLE_RATE * RECORD_TIME_SEC)

int32_t* sample_buffer;

// -------------------- UART TO ESP32 #2 --------------------
#define UART_TX 17      // TX2
#define UART_RX 16      // RX2

HardwareSerial SerialTwo(2);

// -------------------- WAV HEADER FUNCTION --------------------
void build_wav_header(uint8_t* header, uint32_t data_size) {
  uint32_t file_size = data_size + 36;
  uint32_t byte_rate = SAMPLE_RATE * 2; // 16-bit mono

  memcpy(header, "RIFF", 4);
  memcpy(header + 4, &file_size, 4);
  memcpy(header + 8, "WAVEfmt ", 8);

  uint32_t fmt_size = 16;
  uint16_t format = 1;      // PCM
  uint16_t channels = 1;
  uint16_t bits = 16;
  uint16_t block_align = 2;

  memcpy(header + 16, &fmt_size, 4);
  memcpy(header + 20, &format, 2);
  memcpy(header + 22, &channels, 2);
  memcpy(header + 24, &SAMPLE_RATE, 4);
  memcpy(header + 28, &byte_rate, 4);
  memcpy(header + 32, &block_align, 2);
  memcpy(header + 34, &bits, 2);

  memcpy(header + 36, "data", 4);
  memcpy(header + 40, &data_size, 4);
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);

  SerialTwo.begin(921600, SERIAL_8N1, UART_RX, UART_TX);
  Serial.println("UART link initialized.");

  // Allocate sample buffer
  sample_buffer = (int32_t*) malloc(NUM_SAMPLES * sizeof(int32_t));

  // ---------- I2S Microphone ----------
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = SAMPLE_BITS,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = -1,
    .data_in_num = I2S_DIN_MIC
  };

  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);

  // ---------- WiFi ----------
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
}

// -------------------- LOOP --------------------
void loop() {
  Serial.println("Recording...");
  size_t total_bytes = NUM_SAMPLES * sizeof(int32_t);
  size_t bytes_read = 0;

  i2s_read(I2S_PORT, sample_buffer, total_bytes, &bytes_read, portMAX_DELAY);

  Serial.println("Processing audio...");

  uint32_t pcm_bytes = NUM_SAMPLES * 2;
  uint8_t* pcm_data = (uint8_t*) malloc(pcm_bytes);

  for (int i = 0; i < NUM_SAMPLES; i++) {
    int32_t s = sample_buffer[i] >> 8;
    int16_t pcm = (int16_t) s;
    pcm_data[i * 2]     = pcm & 0xFF;
    pcm_data[i * 2 + 1] = pcm >> 8;
  }

  uint8_t wav_header[44];
  build_wav_header(wav_header, pcm_bytes);

  uint32_t wav_size = 44 + pcm_bytes;
  uint8_t* wav_file = (uint8_t*) malloc(wav_size);
  memcpy(wav_file, wav_header, 44);
  memcpy(wav_file + 44, pcm_data, pcm_bytes);

  free(pcm_data);

  // ---------- HTTP POST ----------
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "audio/wav");

  Serial.println("Sending to server...");
  int code = http.POST(wav_file, wav_size);

  if (code != 200) {
    Serial.printf("Server error: %d\n", code);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  const int MAX_AUDIO = 150000;
  uint8_t* rx = (uint8_t*) malloc(MAX_AUDIO);
  int read_bytes = stream->readBytes(rx, MAX_AUDIO);
  http.end();

  Serial.printf("Received %d bytes\n", read_bytes);

  // ---------- SEND OVER UART ----------
  uint16_t payload_len = read_bytes;
  uint8_t checksum = 0;
  for (int i = 0; i < payload_len; i++) checksum ^= rx[i];

  SerialTwo.write(0xAA);               
  SerialTwo.write(payload_len & 0xFF);
  SerialTwo.write(payload_len >> 8);
  SerialTwo.write(rx, payload_len);
  SerialTwo.write(checksum);
  SerialTwo.write(0x55);

  Serial.println("Sent to ESP32 #2.");

  free(rx);
  free(wav_file);

  delay(5000);
}
