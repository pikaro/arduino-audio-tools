/**
 * @file example-serial-receive.ino
 * @author Phil Schatzmann
 * @brief Receiving audio via BLE and writing to I2S
 * @version 0.1
 * @date 2022-03-09
 *
 * @copyright Copyright (c) 2022
 */


#include "AudioTools.h"
//#include "AudioLibs/AudioKit.h"
#include "AudioCodecs/CodecADPCM.h" // https://github.com/pschatzmann/adpcm
#include "Sandbox/BLE/AudioBLE.h"

AudioInfo info(8000, 1, 16);
AudioBLEClient ble;
I2SStream i2s;
ADPCMDecoder adpcm(AV_CODEC_ID_ADPCM_IMA_WAV);
EncodedAudioStream decoder(&i2s, &adpcm);
StreamCopy copier(decoder, ble);

void setup() {
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // start BLE client - wait at most 10 minutes
  ble.begin("ble-send", 60*10);

  // start decoder
  decoder.begin(info);

  // start I2S
  auto config = i2s.defaultConfig(TX_MODE);
  config.copyFrom(info);
  i2s.begin(config);  

  Serial.println("started...");
}

void loop() {
  if (ble)
    copier.copy();
}
