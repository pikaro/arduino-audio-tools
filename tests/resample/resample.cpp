// Simple wrapper for Arduino sketch to compilable with cpp in cmake
#include "Arduino.h"
#include "AudioTools.h"
#include "AudioExperiments/Resample.h"

uint16_t sample_rate=44100;
uint8_t channels = 2;                                      // The stream will have 2 channels 
SineWaveGenerator<int16_t> sineWave(32000);                // subclass of SoundGenerator with max amplitude of 32000
GeneratedSoundStream<int16_t> sound(sineWave);             // Stream generated from sine wave
CsvStream<int16_t> csv(Serial, channels);                            // Output to Serial
Resample<int16_t> out(csv, channels, 2);                   // We double the output sample rate
StreamCopy copier(out, sound);                             // copies sound to out

// Arduino Setup
void setup(void) {  
  // Open Serial 
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Warning);

  // Define CSV Output
  auto config = csv.defaultConfig();
  config.sample_rate = sample_rate; 
  config.channels = channels;
  csv.begin(config);

  // Setup sine wave
  sineWave.begin(channels, sample_rate, N_B4);
  Serial.println("started...");
}

// Arduino loop - copy sound to out 
void loop() {
  copier.copy();
  Serial.println("----");
}
