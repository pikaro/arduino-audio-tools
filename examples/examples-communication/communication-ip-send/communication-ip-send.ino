/**
 * @file example-serial-send.ino
 * @author Phil Schatzmann
 * @brief Sending audio over serial
 * @version 0.1
 * @date 2022-03-09
 *
 * @copyright Copyright (c) 2022
 *
 * ATTENTION: DRAFT - not tested yet
 */

#include "AudioTools.h"
#include <WiFi.h>

uint16_t sample_rate = 22050;
uint8_t channels = 2;  // The stream will have 2 channels
SineWaveGenerator<int16_t> sineWave( 32000);  // subclass of SoundGenerator with max amplitude of 32000
GeneratedSoundStream<int16_t> sound( sineWave);  // Stream generated from sine wave
WiFiClient client;                  
TimedStream clientTimed(client);
StreamCopy copier(clientTimed, sound, 256);  // copies sound into i2s
const char *ssid = "ssid";
const char *password = "password";
const char *client_address = "192.168.1.39"; // update based on your receive ip
uint16_t port = 80;


void setup() {
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // connect to WIFI
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println();
  Serial.println(WiFi. localIP());

  // Try to connect to ip / port
  client.setNoDelay(true);
  while (!client.connect(client_address, port)) {
    Serial.println("trying to connect...");
    delay(5000);
  }

  // Setup sine wave
  sineWave.begin(channels, sample_rate, N_B4);
  Serial.println("started...");
}

void loop() { 
    copier.copy();
    client.flush();
}