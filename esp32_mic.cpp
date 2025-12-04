#include <WiFi.h>
#include <HTTPClient.h>
#include <I2S.h>        // ESP32 I2S library
// UART Serial2 is available by default via HardwareSerial

// WiFi credentials
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

// Server endpoint
const char* SERVER_URL = "http://192.168.x.y:5000/translate";  // replace with PC IP

// I2S microphone pins (from wiring above)
const int I2S_SCK  = 26;  // Bit clock
const int I2S_WS   = 22;  // Word select (LR clock)
const int I2S_SD   = 21;  // Data in (from mic)

// Audio settings
const int SAMPLE_RATE = 16000;
const int SAMPLE_BITS = 16;  // bits per sample
const int RECORD_TIME_SEC = 3;  // record 3 seconds of audio
const int NUM_SAMPLES = SAMPLE_RATE * RECORD_TIME_SEC;  // e.g., 48000 samples for 3 sec

// Buffer for audio data
int16_t *audioBuffer;

void setup() {
  Serial.begin(115200);
  // Initialize UART2 for inter-ESP32 comm (Serial2 uses default pins 16,17)
  Serial2.begin(921600);  // high baud for faster xfer (match on other side)
  
  // Allocate audio buffer
  audioBuffer = (int16_t*) malloc(NUM_SAMPLES * sizeof(int16_t));
  if(!audioBuffer) {
    Serial.println("Failed to allocate audio buffer");
  }

  // I2S Microphone configuration
  I2S.setPins(I2S_SCK, I2S_WS, -1, I2S_SD); 
  // We use -1 for dout because this device is input only; `I2S_SD` is used as data-in.
  if(!I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, SAMPLE_BITS, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to start I2S!");
    while(1);
  }

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi...");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
}

void loop() {
  // 1. Wait or trigger recording (could be a button press or serial command)
  Serial.println("Recording for 3 seconds...");
  size_t bytesRead = 0;
  size_t totalRead = 0;
  // Flush any old data
  I2S.read(audioBuffer, I2S.available()); 

  // Record loop: read samples for RECORD_TIME_SEC
  unsigned long recordStart = millis();
  while(totalRead < NUM_SAMPLES * sizeof(int16_t)) {
    // Read up to remaining bytes
    int bytesToRead = (NUM_SAMPLES * sizeof(int16_t)) - totalRead;
    bytesRead = I2S.read((uint8_t*)audioBuffer + totalRead, bytesToRead);
    if(bytesRead > 0) totalRead += bytesRead;
    if(millis() - recordStart > RECORD_TIME_SEC*1000UL) break; // safety timeout
  }
  I2S.end();  // stop I2S to finish capture (or reuse it if needed later)
  Serial.printf("Recording complete, %u bytes captured\n", totalRead);

  // Basic processing (optional): e.g., a simple high-pass filter to remove DC, etc.
  // (Not implemented here, but you could iterate over audioBuffer and filter noise.)

  // 2. Prepare WAV header (44 bytes) and combine with audio data if needed
  // For simplicity, assume 16-bit mono PCM. We'll prepare a minimal WAV header:
  uint32_t dataSize = totalRead; 
  uint32_t sampleRate = SAMPLE_RATE;
  uint16_t bitsPerSample = SAMPLE_BITS;
  uint16_t numChannels = 1;
  uint32_t byteRate = sampleRate * numChannels * bitsPerSample/8;
  uint16_t blockAlign = numChannels * bitsPerSample/8;
  // WAV header fields
  char wavHeader[44];
  memcpy(wavHeader, "RIFF", 4);
  uint32_t chunkSize = 36 + dataSize;
  memcpy(wavHeader+4, &chunkSize, 4);
  memcpy(wavHeader+8, "WAVE", 4);
  memcpy(wavHeader+12, "fmt ", 4);
  uint32_t subchunk1Size = 16;
  memcpy(wavHeader+16, &subchunk1Size, 4);
  uint16_t audioFormat = 1; // PCM
  memcpy(wavHeader+20, &audioFormat, 2);
  memcpy(wavHeader+22, &numChannels, 2);
  memcpy(wavHeader+24, &sampleRate, 4);
  memcpy(wavHeader+28, &byteRate, 4);
  memcpy(wavHeader+32, &blockAlign, 2);
  memcpy(wavHeader+34, &bitsPerSample, 2);
  memcpy(wavHeader+36, "data", 4);
  memcpy(wavHeader+40, &dataSize, 4);

  // 3. Send audio to server via HTTP POST
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "audio/wav");
  
  // We will send the header and data in one request. We can do this by first combining them.
  uint8_t *postData = (uint8_t*) malloc(44 + dataSize);
  memcpy(postData, wavHeader, 44);
  memcpy(postData+44, audioBuffer, dataSize);
  
  Serial.println("Uploading audio to server...");
  unsigned long httpStart = millis();
  int httpResponseCode = http.POST(postData, 44 + dataSize);
  free(postData);
  if(httpResponseCode != 200) {
    Serial.printf("Server returned %d!\n", httpResponseCode);
    String err = http.getString();
    Serial.println("Error response: " + err);
    http.end();
    // Handle error (retry or break)
    delay(5000);
    return;
  }
  // 4. Receive response audio
  WiFiClient * stream = http.getStreamPtr();
  int totalLen = http.getSize(); // may be -1 if Transfer-Encoding: chunked
  Serial.printf("Response content length: %d\n", totalLen);
  
  // Read response data into buffer
  const size_t MAX_RESP = 100000; // 100 KB buffer
  static uint8_t *respBuf = (uint8_t*) malloc(MAX_RESP);
  size_t bytesReadResp = 0;
  while(http.connected() && (totalLen < 0 || bytesReadResp < totalLen)) {
    if(stream->available()) {
      int len = stream->readBytes(respBuf + bytesReadResp, MAX_RESP - bytesReadResp);
      if(len > 0) bytesReadResp += len;
      // If response is chunked (totalLen==-1), break on some condition (e.g., if we see WAV "RIFF" end).
    }
  }
  http.end();
  unsigned long httpTime = millis() - httpStart;
  Serial.printf("Received %u bytes audio from server in %.2f s\n", bytesReadResp, httpTime/1000.0);

  // Optionally, parse any text (not applicable in our raw response approach, but if we had JSON, parse here)

  // 5. Send audio to ESP32 #2 via UART
  // Construct UART packet with SOP, length, data, checksum, EOP
  uint16_t audioLen = bytesReadResp;
  uint8_t csum = 0;
  for(size_t i=0; i<audioLen; ++i) {
    csum ^= respBuf[i];
  }
  Serial2.write(0xAA);                         // SOP
  Serial2.write((uint8_t)(audioLen & 0xFF));   // LEN LSB
  Serial2.write((uint8_t)(audioLen >> 8));     // LEN MSB
  Serial2.write(respBuf, audioLen);            // Data
  Serial2.write(csum);                        // Checksum
  Serial2.write(0x55);                        // EOP
  Serial.printf("Sent %u bytes to ESP32 #2 over UART\n", audioLen);
  
  // 6. (Optional) Wait for ACK
  unsigned long t0 = millis();
  while(millis() - t0 < 2000) {
    if(Serial2.available()) {
      int b = Serial2.read();
      if(b == 0x06) { // let's say 0x06 is ACK
        Serial.println("ACK received from ESP32 #2");
        break;
      } else {
        Serial.printf("Received unexpected 0x%02X from ESP32 #2\n", b);
      }
    }
  }
  
  Serial.println("Translation cycle complete.\n");
  delay(5000);  // wait before next cycle (or wait for new trigger)
}
