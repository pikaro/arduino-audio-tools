/**
 * @file streams-synthbasic-audiokit.ino
 * @author Phil Schatzmann
 * @brief see https://www.pschatzmann.ch/home/2021/12/17/ai-thinker-audio-kit-building-a-simple-synthesizer-with-the-audiotools-library/)
 * @copyright GPLv3
 * 
 */
#include "AudioTools.h"
#include "AudioLibs/AudioKit.h"

AudioKitStream kit;
SineWaveGenerator<int16_t> sine;
GeneratedSoundStream<int16_t> in(sine); 
StreamCopy copier(kit, in); 

void actionKeyOn(bool active, int pin, void* ptr){
  int freq = *((int*)ptr);
  sine.setFrequency(freq);
  in.begin();
}

void actionKeyOff(bool active, int pin, void* ptr){
  in.end();
}

// We want to play some notes on the AudioKit keys 
void setupActions(){
  // assign buttons to notes
  auto act_low = AudioActions::ActiveLow;
  static int note[] = {N_C3, N_D3, N_E3, N_F3, N_G3, N_A3}; // frequencies
  kit.audioActions().add(PIN_KEY1, actionKeyOn, actionKeyOff, act_low, &(note[0])); // C3
  kit.audioActions().add(PIN_KEY2, actionKeyOn, actionKeyOff, act_low, &(note[1])); // D3
  kit.audioActions().add(PIN_KEY3, actionKeyOn, actionKeyOff, act_low, &(note[2])); // E3
  kit.audioActions().add(PIN_KEY4, actionKeyOn, actionKeyOff, act_low, &(note[3])); // F3
  kit.audioActions().add(PIN_KEY5, actionKeyOn, actionKeyOff, act_low, &(note[4])); // G3
  kit.audioActions().add(PIN_KEY6, actionKeyOn, actionKeyOff, act_low, &(note[5])); // A3
}

void setup() {
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial,AudioLogger::Info);

  // Setup output
  auto cfg = kit.defaultConfig(TX_MODE);
  kit.begin(cfg);

  // Setup sound generation based on AudioKit settins
  in.begin(cfg);

  // activate keys
  setupActions();

}

// copy the data
void loop() {
  copier.copy();
  kit.processActions();
}
