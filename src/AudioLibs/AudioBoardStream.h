#pragma once

#include "AudioConfig.h"
#include "AudioLibs/I2SCodecStream.h"
#include "AudioTools/AudioActions.h"

namespace audio_tools {
class AudioBoardStream;
AudioBoardStream *selfAudioBoard = nullptr;

/**
 * @brief New functionality which replaces the AudioKitStream that is based on
 * the legicy AudioKit library. This functionality uses the new
 * arduino-audio-driver library! It is the same as I2SCodecStream extended by
 * some AudioActions and some method calls to determine defined pin values.
 * @ingroup io
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class AudioBoardStream : public I2SCodecStream {
public:
  /**
   * @brief Default constructor: for available AudioBoard values check audioboard
   * variables in https://pschatzmann.github.io/arduino-audio-driver/html/group__audio__driver.html
   * Further information can be found in https://github.com/pschatzmann/arduino-audio-driver/wiki
   */
  AudioBoardStream(audio_driver::AudioBoard &board) : I2SCodecStream(board) {
    selfAudioBoard = this;
  }

  bool begin() override {
    if (is_default_actions && getPins().hasPins())
      setupActions();
    return I2SCodecStream::begin();
  }

  bool begin(I2SCodecConfig cfg) override { return I2SCodecStream::begin(cfg); }

  bool begin(I2SCodecConfig cfg, bool defaultActionActive) { 
    setDefaultActionsActive(defaultActionActive);
    return I2SCodecStream::begin(cfg);
  }

  /**
   * @brief Process input keys and pins
   *
   */
  void processActions() {
    //  TRACED();
    actions.processActions();
    yield();
  }

  /**
   * @brief Defines a new action that is executed when the indicated pin is
   * active
   *
   * @param pin
   * @param action
   * @param ref
   */
  void addAction(int pin, void (*action)(bool, int, void *),
                 void *ref = nullptr) {
    TRACEI();
    // determine logic from config
    AudioActions::ActiveLogic activeLogic = getActionLogic(pin);
    actions.add(pin, action, activeLogic, ref);
  }

  /**
   * @brief Defines a new action that is executed when the indicated pin is
   * active
   *
   * @param pin
   * @param action
   * @param activeLogic
   * @param ref
   */
  void addAction(int pin, void (*action)(bool, int, void *),
                 AudioActions::ActiveLogic activeLogic, void *ref = nullptr) {
    TRACEI();
    actions.add(pin, action, activeLogic, ref);
  }

  /// Provides access to the AudioActions
  AudioActions &audioActions() { return actions; }

  /**
   * @brief Relative volume control
   *
   * @param vol
   */
  void incrementVolume(int vol) {
    volume_value += vol;
    LOGI("incrementVolume: %d -> %d", vol, volume_value);
    setVolume(volume_value);
  }

  /**
   * @brief Increase the volume
   *
   */
  static void actionVolumeUp(bool, int, void *) {
    TRACEI();
    selfAudioBoard->incrementVolume(+2);
  }

  /**
   * @brief Decrease the volume
   *
   */
  static void actionVolumeDown(bool, int, void *) {
    TRACEI();
    selfAudioBoard->incrementVolume(-2);
  }

  /**
   * @brief Toggle start stop
   *
   */
  static void actionStartStop(bool, int, void *) {
    TRACEI();
    selfAudioBoard->active = !selfAudioBoard->active;
    selfAudioBoard->setActive(selfAudioBoard->active);
  }

  /**
   * @brief Start
   *
   */
  static void actionStart(bool, int, void *) {
    TRACEI();
    selfAudioBoard->active = true;
    selfAudioBoard->setActive(selfAudioBoard->active);
  }

  /**
   * @brief Stop
   */
  static void actionStop(bool, int, void *) {
    TRACEI();
    selfAudioBoard->active = false;
    selfAudioBoard->setActive(selfAudioBoard->active);
  }

  /**
   * @brief Switch off the PA if the headphone in plugged in
   * and switch it on again if the headphone is unplugged.
   * This method complies with the
   */
  static void actionHeadphoneDetection(bool, int, void *) {
    if (selfAudioBoard->pinHeadphoneDetect() >= 0) {

      // detect changes
      bool isConnected = selfAudioBoard->headphoneStatus();
      if (selfAudioBoard->headphoneIsConnected != isConnected) {
        selfAudioBoard->headphoneIsConnected = isConnected;

        // update if things have stabilized
        bool powerActive = !isConnected;
        LOGW("Headphone jack has been %s",
             isConnected ? "inserted" : "removed");
        selfAudioBoard->setSpeakerActive(powerActive);
      }
    }
    yield();
  }

  /**
   * @brief  Get the gpio number for auxin detection
   *
   * @return  -1      non-existent
   *          Others  gpio number
   */
  GpioPin pinAuxin() { return getPinID(PinFunction::AUXIN_DETECT); }

  /**
   * @brief  Get the gpio number for headphone detection
   *
   * @return  -1      non-existent
   *          Others  gpio number
   */
  GpioPin pinHeadphoneDetect() { return getPinID(PinFunction::HEADPHONE_DETECT); }

  /**
   * @brief  Get the gpio number for PA enable
   *
   * @return  -1      non-existent
   *          Others  gpio number
   */
  GpioPin pinPaEnable() { return getPinID(PinFunction::PA); }

  //   /**
  //    * @brief  Get the gpio number for adc detection
  //    *
  //    * @return  -1      non-existent
  //    *          Others  gpio number
  //    */
  //   GpioPin pinAdcDetect() { return getPin(AUXIN_DETECT); }

  /**
   * @brief  Get the record-button id for adc-button
   *
   * @return  -1      non-existent
   *          Others  button id
   */
  GpioPin pinInputRec() { return getPinID(PinFunction::KEY, 1); }

  /**
   * @brief  Get the number for mode-button
   *
   * @return  -1      non-existent
   *          Others  number
   */
  GpioPin pinInputMode() { return getPinID(PinFunction::KEY, 2); }

  /**
   * @brief Get number for set function
   *
   * @return -1       non-existent
   *         Others   number
   */
  GpioPin pinInputSet() { return getPinID(PinFunction::KEY, 4); }

  /**
   * @brief Get number for play function
   *
   * @return -1       non-existent
   *         Others   number
   */
  GpioPin pinInputPlay() { return getPinID(PinFunction::KEY, 3); }

  /**
   * @brief number for volume up function
   *
   * @return -1       non-existent
   *         Others   number
   */
  GpioPin pinVolumeUp() { return getPinID(PinFunction::KEY, 6); }

  /**
   * @brief Get number for volume down function
   *
   * @return -1       non-existent
   *         Others   number
   */
  GpioPin pinVolumeDown() { return getPinID(PinFunction::KEY, 5); }

  /**
   * @brief Get LED pin
   *
   * @return -1       non-existent
   *         Others   gpio number
   */
  GpioPin pinLed(int idx) { return getPinID(PinFunction::LED, idx); }

  /// the same as setPAPower()
  void setSpeakerActive(bool active) { setPAPower(active); }

  /**
   * @brief Returns true if the headphone was detected
   *
   * @return true
   * @return false
   */
  bool headphoneStatus() {
    int headphoneGpioPin = pinHeadphoneDetect();
    return headphoneGpioPin > 0 ? !digitalRead(headphoneGpioPin) : false;
  }

  /**
   * @brief The oposite of setMute(): setActive(true) calls setMute(false)
   */
  void setActive(bool active) { setMute(!active); }


  /**
   * @brief Defines if we set up the default actions
   */
  void setDefaultActionsActive(bool active){
    is_default_actions = active;
  }

protected:
  AudioActions actions;
  int volume_value = 40;
  bool headphoneIsConnected = false;
  bool active = true;
  bool is_default_actions = true;

  /// Determines the action logic (ActiveLow or ActiveTouch) for the pin
  AudioActions::ActiveLogic getActionLogic(int pin) {
    auto opt = board().getPins().getPin(pin);
    PinLogic logic = PinLogic::Input;
    if (opt)
      logic = opt.value().pin_logic;
    switch (logic) {
    case PinLogic::Input:
    case PinLogic::InputActiveLow:
      return AudioActions::ActiveLow;
    case PinLogic::InputActiveHigh:
      return AudioActions::ActiveHigh;
    case PinLogic::InputActiveTouch:
      return AudioActions::ActiveTouch;
    default:
      return AudioActions::ActiveLow;
    }
  }

  /// Setup the supported default actions (volume, inpu_mode, headphone detection)
  void setupActions() {
    TRACEI();
    GpioPin sd_cs = -1;
    auto sd_opt = getPins().getSPIPins(PinFunction::SD);
    if (sd_opt) {
      sd_cs = sd_opt.value().cs;
    } else {
      // no spi -> no sd
      LOGI("No sd defined -> sd_active=false")
      cfg.sd_active = false;
    }
    // pin conflicts for pinInputMode() with the SD CS pin for AIThinker and
    // buttons
    int input_mode = pinInputMode();
    if (input_mode != -1 && (input_mode != sd_cs || !cfg.sd_active))
      addAction(pinInputMode(), actionStartStop);

    // pin conflicts with AIThinker A101: key6 and headphone detection
    int head_phone = pinHeadphoneDetect();
    if (head_phone != -1 && (getPinID(PinFunction::KEY, 6) != head_phone)) {
      actions.add(pinHeadphoneDetect(), actionHeadphoneDetection,
                  AudioActions::ActiveChange);
    }

    // pin conflicts with SD Lyrat SD CS GpioPin and buttons / Conflict on Audiokit
    // V. 2957
    int vol_up = pinVolumeUp();
    int vol_down = pinVolumeDown();
    if ((vol_up != -1 && vol_down!=-1) && (!cfg.sd_active ||
        (vol_down != sd_cs && vol_up != sd_cs))) {
      LOGD("actionVolumeDown")
      addAction(vol_down, actionVolumeDown);
      LOGD("actionVolumeUp")
      addAction(vol_up, actionVolumeUp);
    } else {
      LOGW("Volume Buttons ignored because of conflict: %d ", pinVolumeDown());
    }
  }
};

} // namespace audio_tools
