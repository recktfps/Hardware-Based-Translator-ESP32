#include <Arduino.h>
#include <I2S.h>

// I2S amplifier pins (from wiring above)
const int I2S_SCK  = 27;  // Bit clock (BCLK)
const int I2S_WS   = 25;  // Word select (LRCLK)
const int I2S_SD   = 33;  // Data out (to DIN of amp)

// Audio parameters (must match what server sends)
const int SAMPLE_RATE = 16000;
const int SAMPLE_BITS = 16;
const int CHANNELS = 1;

void setup() {
  Serial.begin(115200);
  // Initialize UART2 
  Serial2.begin(921600);  // must match ESP32 #1 baud

  // Configure I2S for output (Philips, 16-bit, mono)
  I2S.setPins(I2S_SCK, I2S_WS, I2S_SD, -1); 
  // Here, SD is used as data-out (PIN_I2S_SD_OUT), and we have no data-in, so -1 for in pin.
  if(!I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, SAMPLE_BITS, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to begin I2S for output");
    while(1);
  }
  Serial.println("ESP32 #2 ready, waiting for audio packets...");
}

void loop() {
  static enum { WAIT_FOR_SOP, READ_LEN, READ_DATA, READ_CSUM, READ_EOP } state = WAIT_FOR_SOP;
  static uint16_t expectedLen = 0;
  static uint16_t bytesRead = 0;
  static uint8_t checksumCalc = 0;
  static uint8_t *audioBuf = NULL;

  while(Serial2.available()) {
    uint8_t b = Serial2.read();
    switch(state) {
      case WAIT_FOR_SOP:
        if(b == 0xAA) {
          state = READ_LEN;
          expectedLen = 0;
          bytesRead = 0;
          checksumCalc = 0;
          // If a buffer was previously allocated, free it (for next use)
          if(audioBuf) { free(audioBuf); audioBuf = NULL; }
        }
        break;
      case READ_LEN:
        // reading two bytes of length (LSB first)
        expectedLen |= (bytesRead == 0 ? b : b << 8);
        bytesRead++;
        if(bytesRead == 2) {
          // allocate buffer for audio data
          audioBuf = (uint8_t*) malloc(expectedLen);
          if(!audioBuf) {
            Serial.println("Memory alloc failed!");
            state = WAIT_FOR_SOP;
            // Skip the incoming packet if no mem (or use a static buffer as alternative).
          } else {
            state = READ_DATA;
            bytesRead = 0;
            // reset checksumCalc for new data
            checksumCalc = 0;
          }
        }
        break;
      case READ_DATA:
        if(audioBuf && bytesRead < expectedLen) {
          audioBuf[bytesRead++] = b;
          checksumCalc ^= b;
          if(bytesRead == expectedLen) {
            state = READ_CSUM;
          }
        } else {
          // Unexpected condition: no buffer or extra data
        }
        break;
      case READ_CSUM:
        {
          uint8_t receivedCsum = b;
          if(receivedCsum != checksumCalc) {
            Serial.println("Checksum mismatch! Data corrupted");
            // Flush until SOP of next packet
            state = WAIT_FOR_SOP;
            // Send NACK (we define 0x15 as NACK for example)
            Serial2.write(0x15);
            break;
          }
          state = READ_EOP;
        }
        break;
      case READ_EOP:
        if(b != 0x55) {
          Serial.println("End-of-packet marker not found or incorrect!");
          // Data might be misaligned. We reset state.
          state = WAIT_FOR_SOP;
          Serial2.write(0x15); // send NACK
        } else {
          Serial.printf("Received packet: %u bytes, checksum OK\n", expectedLen);
          // 1. Acknowledge reception
          Serial2.write(0x06);  // send ACK (0x06)

          // 2. Play the audio via I2S
          // Note: The data may include a WAV header if ESP32 #1 forwarded it directly.
          // If so, we should skip the 44-byte header before playing PCM.
          uint8_t *pcmData = audioBuf;
          int dataLen = expectedLen;
          if(expectedLen > 44 && strncmp((char*)audioBuf, "RIFF", 4) == 0) {
            pcmData = audioBuf + 44;
            dataLen = expectedLen - 44;
            Serial.println("WAV header detected, skipping 44 bytes");
          }
          // Write PCM data to I2S in chunks
          int bytesToWrite = dataLen;
          int offset = 0;
          while(bytesToWrite > 0) {
            int written = I2S.write(pcmData + offset, bytesToWrite);
            if(written < 0) {
              Serial.println("I2S write error!");
              break;
            }
            bytesToWrite -= written;
            offset += written;
          }
          Serial.println("Audio playback done");
          // Optionally, flush I2S or add a small delay to ensure playback completes
          
          // 3. Free buffer and reset for next packet
          free(audioBuf);
          audioBuf = NULL;
          state = WAIT_FOR_SOP;
        }
        break;
    } // switch
  } // while Serial2.available()

  // You can do other tasks here (in parallel, but typically we just idle waiting for data)
}
