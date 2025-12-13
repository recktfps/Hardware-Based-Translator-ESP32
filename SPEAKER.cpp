/*

FIle: Speaker.cpp 
Functionality: Uses our I2S speaker and amplifier at an amplitude of 3000 to give us speech output. 
Course: CECS 460 
Students: James Henry, Ivan Martinez, Sheesh

*/
#include <Arduino.h>
#include <AudioOutputI2S.h>     // ESP8266Audio
#include <ESP8266SAM_ES.h>      // Spanish SAM voice

// -------------------------
// I2S PINS for MAX98357A
// -------------------------

// D33 ACTS AS INPUT, TURNS LED BLUE 
// WHEN PRESSED, Say command into microphone module 

const int I2S_BCLK = 26;  // BCLK
const int I2S_LRC  = 25;  // LRCLK
const int I2S_DOUT = 27;  // DIN (data)

// -------------------------
// Volume control (quiet)
// 3000 / 32767 ≈ 0.09
// -------------------------
const float DIGITAL_VOLUME = 3000.0f / 32767.0f;

// Objects
AudioOutputI2S *audioOut;
ESP8266SAM_ES sam;

// Serial buffer
const size_t MAX_TEXT = 120;
String inputLine;

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // Create audio output
  audioOut = new AudioOutputI2S();
  audioOut->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audioOut->SetOutputModeMono(true);
  audioOut->SetGain(DIGITAL_VOLUME);
  audioOut->begin();

  // Spanish voice region
  sam.SetRegion(ESP8266SAM_ES::SAMRegion::REGION_ES);

  Serial.println("\nESP32 Spanish TTS Ready");
  Serial.println("Type a line and press ENTER.\n");
}

// ------------------------------------------------------------
// Speak text safely (fixes repeating 'la-la-la-la' bug)
// ------------------------------------------------------------
void speakLine(const String &text) {
  String phrase = text;
  phrase.trim();
  if (phrase.length() == 0) return;

  // Add punctuation so SAM ends with silence instead of looping
  char last = phrase[phrase.length() - 1];
  if (last != '.' && last != '?' && last != '!') {
    phrase += '.';
  }

  // Convert to char[]
  char buf[MAX_TEXT + 2];
  size_t n = phrase.length();
  if (n > MAX_TEXT + 1) n = MAX_TEXT + 1;
  phrase.substring(0, n).toCharArray(buf, n + 1);

  Serial.print("\n[Speaking]: ");
  Serial.println(buf);

  sam.Say(audioOut, buf);

  // Prevent ESP32 I2S DMA from looping last sample
  audioOut->stop();
}

// ------------------------------------------------------------
void loop() {
  // UART echo + line capture
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    // Echo back
    Serial.write(c);

    if (c == '\r') continue;

    if (c == '\n') {
      inputLine.trim();
      speakLine(inputLine);
      inputLine = "";
    } else {
      if (inputLine.length() < MAX_TEXT) {
        inputLine += c;
      }
    }
  }
}
