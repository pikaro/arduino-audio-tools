/**
 * @file player-sd-audiokit.ino
 * @brief see https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-audiokit/player-sd-audiokit/README.md
 * 
 * @author Phil Schatzmann
 * @copyright GPLv3
 */


#include "AudioTools.h"
#include "AudioLibs/AudioKit.h"
#include "AudioLibs/AudioSourceSd.h"
#include "AudioCodecs/CodecMP3Helix.h"

const char *startFilePath="/";
const char* ext="mp3";
AudioSourceSd source(startFilePath, ext, PIN_AUDIO_KIT_SD_CARD_CS);
AudioKitStream kit;
MP3DecoderHelix decoder;  // or change to MP3DecoderMAD
AudioPlayer player(source, kit, decoder);

void setup() {
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // setup output
  auto cfg = kit.defaultConfig(TX_MODE);
  cfg.sd_active = true;
  kit.begin(cfg);


  // setup player
  player.setVolume(0.7);
  player.begin();

  // select file with setPath() or setIndex()
  //player.setPath("/ZZ Top/Unknown Album/Lowrider.mp3");
  //player.setIndex(1); // 2nd file

}

void loop() {
  player.copy();
}