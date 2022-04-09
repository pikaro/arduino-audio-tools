#pragma once

// Configure FFT to output 16 bit fixed point.
#define FIXED_POINT 16

#include <TensorFlowLite.h>
#include <cmath>
#include <cstdint>
#include "AudioTools/AudioOutput.h"
#include "AudioTools/Buffers.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace audio_tools {

// Forward Declarations
class TfLiteAudioStreamBase;
class TfLiteAbstractRecognizeCommands;

/**
 * @brief Input class which provides the next value if the TfLiteAudioStream is treated as an audio sourcce
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteReader {
  public:
    virtual bool begin(TfLiteAudioStreamBase *parent) = 0;
    virtual int read(int16_t*data, int len) = 0;
};

/**
 * @brief Output class which interprets audio data if TfLiteAudioStream is treated as audio sink
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteWriter {
  public:
    virtual bool begin(TfLiteAudioStreamBase *parent) = 0;
    virtual bool write(const int16_t sample) = 0;
};
/**
 * @brief Error Reporter using the Audio Tools Logger
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteAudioErrorReporter : public tflite::ErrorReporter {
 public:
  virtual ~TfLiteAudioErrorReporter() {}
  virtual int Report(const char* format, va_list args) override {
    int result = snprintf(msg, 200, format, args);
    LOGE(msg);
    return result;
  }

 protected:
  char msg[200];
} my_error_reporter;
tflite::ErrorReporter* error_reporter = &my_error_reporter;

/**
 * @brief Configuration settings for TfLiteAudioStream
 * @author Phil Schatzmann
 * @copyright GPLv3
 */

struct TfLiteConfig {
  friend class TfLiteMicroSpeechRecognizeCommands;
  const unsigned char* model = nullptr;
  TfLiteReader *reader = nullptr;
  TfLiteWriter *writer = nullptr;
  TfLiteAbstractRecognizeCommands *recognizeCommands=nullptr;
  bool useAllOpsResolver = false;
  // callback for command handler
  void (*respondToCommand)(const char* found_command, uint8_t score,
                           bool is_new_command) = nullptr;

  // Create an area of memory to use for input, output, and intermediate arrays.
  // The size of this will depend on the model you’re using, and may need to be
  // determined by experimentation.
  int kTensorArenaSize = 10 * 1024;

  // Keeping these as constant expressions allow us to allocate fixed-sized
  // arrays on the stack for our working memory.

  // The size of the input time series data we pass to the FFT to produce
  // the frequency information. This has to be a power of two, and since
  // we're dealing with 30ms of 16KHz inputs, which means 480 samples, this
  // is the next value.
  // int kMaxAudioSampleSize =  320; //512; // 480
  int sample_rate = 16000;

  // Number of audio channels - is usually 1. If 2 we reduce it to 1 by
  // averaging the 2 channels
  int channels = 1;

  // The following values are derived from values used during model training.
  // If you change the way you preprocess the input, update all these constants.
  int kFeatureSliceSize = 40;
  int kFeatureSliceCount = 49;
  int kFeatureSliceStrideMs = 20;
  int kFeatureSliceDurationMs = 30;

  // number of new slices to collect before evaluating the model
  int kSlicesToProcess = 2;

  // Parameters for RecognizeCommands
  int32_t average_window_duration_ms = 1000;
  uint8_t detection_threshold = 200;
  int32_t suppression_ms = 1500;
  int32_t minimum_count = 3;

  // input for FrontendConfig
  float filterbank_lower_band_limit = 125.0;
  float filterbank_upper_band_limit = 7500.0;
  float noise_reduction_smoothing_bits = 10;
  float noise_reduction_even_smoothing = 0.025;
  float noise_reduction_odd_smoothing = 0.06;
  float noise_reduction_min_signal_remaining = 0.05;
  bool pcan_gain_control_enable_pcan = 1;
  float pcan_gain_control_strength = 0.95;
  float pcan_gain_control_offset = 80.0;
  float pcan_gain_control_gain_bits = 21;
  bool log_scale_enable_log = 1;
  uint8_t log_scale_scale_shift = 6;

  /// Defines the labels
  template<int N>
  void setCategories(const char* (&array)[N]){
    labels = array;
    kCategoryCount = N;
  }
 
  int categoryCount() {
    return kCategoryCount;
  }

  int featureElementCount() { 
    return kFeatureSliceSize * kFeatureSliceCount;
  }

  int audioSampleSize() {
    return kFeatureSliceDurationMs * (sample_rate / 1000);
  }

  int strideSampleSize() {
    return kFeatureSliceStrideMs * (sample_rate / 1000);
  }

  private:
    int  kCategoryCount = 0;
    const char** labels = nullptr;
};

/**
 * @brief Quantizer that helps to quantize and dequantize between float and int8
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfQuantizer {
  public:
    // convert float to int8
    static int8_t quantize(float value, float scale, float zero_point){
      if(scale==0.0&&zero_point==0) return value;
      return value / scale + zero_point;
    }
    // convert int8 to float
    static float dequantize(int8_t value, float scale, float zero_point){
      if(scale==0.0&&zero_point==0) return value;
      return (value - zero_point) * scale;
    }

    static float dequantizeToNewRange(int8_t value, float scale, float zero_point, float new_range){
      float deq = (static_cast<float>(value) - zero_point) * scale;
      return clip(deq * new_range, new_range);
    }

    static float clip(float value, float range){
      if (value>=0.0){
        return value > range ? range : value;
      } else {
        return -value < -range ? -range : value;
      }
    }
};

/** 
 * @brief Partial implementation of std::dequeue, just providing the functionality
 * that's needed to keep a record of previous neural network results over a
 * short time period, so they can be averaged together to produce a more
 * accurate overall prediction. This doesn't use any dynamic memory allocation
 * so it's a better fit for microcontroller applications, but this does mean
 * there are hard limits on the number of results it can store.
 */
class TfLiteResultsQueue {
 public:
  TfLiteResultsQueue()  = default;

  void begin(int categoryCount){
    LOGD(LOG_METHOD);
    kCategoryCount = categoryCount;
    for (int j=0;j<kMaxResults;j++){
      results_[j].setCategoryCount(categoryCount);
    }
  }

  // Data structure that holds an inference result, and the time when it
  // was recorded.
  struct Result {
    Result()  = default;
    // Copy constructor to prevent multi heap poisoning
    Result (const Result &old_obj){
      setCategoryCount(old_obj.kCategoryCount);
      time_ = old_obj.time_;
      memmove(scores,old_obj.scores,kCategoryCount);
    }
    ~Result()  {
      if (scores!=nullptr) delete []scores;
      scores = nullptr;
    }
    Result(int categoryCount, int32_t time, int8_t* input_scores) : time_(time) {
      setCategoryCount(categoryCount);
      for (int i = 0; i < kCategoryCount; ++i) {
        scores[i] = input_scores[i];
      }
    }
    void setCategoryCount(int count){
      kCategoryCount = count;
      this->scores = new int8_t[count];
    }
    int kCategoryCount=0;
    int32_t time_=0;
    int8_t *scores=nullptr;
  };

  int size() { return size_; }
  bool empty() { return size_ == 0; }
  Result& front() { return results_[front_index_]; }
  Result& back() {
    int back_index = front_index_ + (size_ - 1);
    if (back_index >= kMaxResults) {
      back_index -= kMaxResults;
    }
    return results_[back_index];
  }

  void push_back(const Result& entry) {
    if (size() >= kMaxResults) {
      LOGE("Couldn't push_back latest result, too many already!");
      return;
    }
    size_ += 1;
    back() = entry;
  }

  Result pop_front() {
    if (size() <= 0) {
      LOGE("Couldn't pop_front result, none present!");
      return Result();
    }
    Result result = front();
    front_index_ += 1;
    if (front_index_ >= kMaxResults) {
      front_index_ = 0;
    }
    size_ -= 1;
    return result;
  }

  // Most of the functions are duplicates of dequeue containers, but this
  // is a helper that makes it easy to iterate through the contents of the
  // queue.
  Result& from_front(int offset) {
    if ((offset < 0) || (offset >= size_)) {
      LOGE("Attempt to read beyond the end of the queue!");
      offset = size_ - 1;
    }
    int index = front_index_ + offset;
    if (index >= kMaxResults) {
      index -= kMaxResults;
    }
    return results_[index];
  }

 private:
  static constexpr int kMaxResults = 50;
  Result results_[kMaxResults];
  int kCategoryCount=0;
  int front_index_=0;
  int size_=0;
};

/**
 * @brief Base class for implementing different primitive decoding models on top
 * of the instantaneous results from running an audio recognition model on a
 * single window of samples.
 */
class TfLiteAbstractRecognizeCommands {
 public:
  virtual TfLiteStatus processLatestResults(const TfLiteTensor* latest_results,
                                            const int32_t current_time_ms,
                                            const char** found_command,
                                            uint8_t* score,
                                            bool* is_new_command) = 0;

  virtual bool begin(TfLiteConfig cfg) = 0;
};

/**
 * @brief This class is designed to apply a very primitive decoding model on top
 * of the instantaneous results from running an audio recognition model on a
 * single window of samples. It applies smoothing over time so that noisy
 * individual label scores are averaged, increasing the confidence that apparent
 * matches are real. To use it, you should create a class object with the
 * configuration you want, and then feed results from running a TensorFlow model
 * into the processing method. The timestamp for each subsequent call should be
 * increasing from the previous, since the class is designed to process a stream
 * of data over time.
 */

class TfLiteMicroSpeechRecognizeCommands : public TfLiteAbstractRecognizeCommands {
 public:
  // labels should be a list of the strings associated with each one-hot score.
  // The window duration controls the smoothing. Longer durations will give a
  // higher confidence that the results are correct, but may miss some commands.
  // The detection threshold has a similar effect, with high values increasing
  // the precision at the cost of recall. The minimum count controls how many
  // results need to be in the averaging window before it's seen as a reliable
  // average. This prevents erroneous results when the averaging window is
  // initially being populated for example. The suppression argument disables
  // further recognitions for a set time after one has been triggered, which can
  // help reduce spurious recognitions.

  TfLiteMicroSpeechRecognizeCommands() {
    previous_top_label_ = "silence";
    previous_top_label_time_ = std::numeric_limits<int32_t>::min();
  }

  /// Setup parameters from config
  bool begin(TfLiteConfig cfg) override {
    LOGD(LOG_METHOD);
    this->cfg = cfg;
    kCategoryCount = cfg.categoryCount();
    if (kCategoryCount == 0) {
      LOGE("kCategoryCount must not be 0");
      return false;
    }
    if (cfg.labels == nullptr) {
      LOGE("config.labels not defined");
      return false;
    }
    previous_results_.begin(kCategoryCount);
    started = true;
    return true;
  }

  // Call this with the results of running a model on sample data.
  virtual TfLiteStatus processLatestResults(const TfLiteTensor* latest_results,
                                            const int32_t current_time_ms,
                                            const char** found_command,
                                            uint8_t* score,
                                            bool* is_new_command) override {
    LOGD(LOG_METHOD);
    if (!started) {
      LOGE("TfLiteMicroSpeechRecognizeCommands not started");
      return kTfLiteError;
    }
    if ((latest_results->dims->size != 2) ||
        (latest_results->dims->data[0] != 1) ||
        (latest_results->dims->data[1] != kCategoryCount)) {
      LOGE(
          "The results for recognition should contain %d "
          "elements, but there are "
          "%d in an %d-dimensional shape",
          kCategoryCount, (int)latest_results->dims->data[1],
          (int)latest_results->dims->size);
      return kTfLiteError;
    }

    if (latest_results->type != kTfLiteInt8) {
      LOGE("The results for recognition should be int8 elements, but are %d",
           (int)latest_results->type);
      return kTfLiteError;
    }

    if ((!previous_results_.empty()) &&
        (current_time_ms < previous_results_.front().time_)) {
      LOGE("Results must be in increasing time order: timestamp  %d < %d",
           (int)current_time_ms, (int)previous_results_.front().time_);
      return kTfLiteError;
    }

    // Add the latest results to the head of the queue.
    previous_results_.push_back({kCategoryCount, current_time_ms, latest_results->data.int8});

    // Prune any earlier results that are too old for the averaging window.
    const int64_t time_limit = current_time_ms - cfg.average_window_duration_ms;
    while ((!previous_results_.empty()) &&
           previous_results_.front().time_ < time_limit) {
      previous_results_.pop_front();
    }

    // If there are too few results, assume the result will be unreliable
    // and bail.
    const int64_t how_many_results = previous_results_.size();
    const int64_t earliest_time = previous_results_.front().time_;
    const int64_t samples_duration = current_time_ms - earliest_time;
    if ((how_many_results < cfg.minimum_count) ||
        (samples_duration < (cfg.average_window_duration_ms / 4))) {
      *found_command = previous_top_label_;
      *score = 0;
      *is_new_command = false;
      return kTfLiteOk;
    }

    // Calculate the average score across all the results in the window.
    int32_t average_scores[kCategoryCount];
    for (int offset = 0; offset < previous_results_.size(); ++offset) {
      auto previous_result = previous_results_.from_front(offset);
      const int8_t* scores = previous_result.scores;
      for (int i = 0; i < kCategoryCount; ++i) {
        if (offset == 0) {
          average_scores[i] = scores[i] + 128;
        } else {
          average_scores[i] += scores[i] + 128;
        }
      }
    }
    for (int i = 0; i < kCategoryCount; ++i) {
      average_scores[i] /= how_many_results;
    }

    // Find the current highest scoring category.
    int current_top_index = 0;
    int32_t current_top_score = 0;
    for (int i = 0; i < kCategoryCount; ++i) {
      if (average_scores[i] > current_top_score) {
        current_top_score = average_scores[i];
        current_top_index = i;
      }
    }
    const char* current_top_label = cfg.labels[current_top_index];

    // If we've recently had another label trigger, assume one that occurs
    // too soon afterwards is a bad result.
    int64_t time_since_last_top;
    if ((previous_top_label_ == cfg.labels[0]) ||
        (previous_top_label_time_ == std::numeric_limits<int32_t>::min())) {
      time_since_last_top = std::numeric_limits<int32_t>::max();
    } else {
      time_since_last_top = current_time_ms - previous_top_label_time_;
    }
    if ((current_top_score > cfg.detection_threshold) &&
        ((current_top_label != previous_top_label_) ||
         (time_since_last_top > cfg.suppression_ms))) {
      previous_top_label_ = current_top_label;
      previous_top_label_time_ = current_time_ms;
      *is_new_command = true;
    } else {
      *is_new_command = false;
    }
    *found_command = current_top_label;
    *score = current_top_score;

    return kTfLiteOk;
  }

 protected:
  // Configuration
  TfLiteConfig cfg;
  int kCategoryCount=0;
  bool started = false;

  // Working variables
  TfLiteResultsQueue previous_results_;
  const char* previous_top_label_;
  int32_t previous_top_label_time_;
};

/**
 * @brief Astract TfLiteAudioStream to provide access to TfLiteAudioStream for
 * Reader and Writers
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteAudioStreamBase : public AudioStreamX {
  public:
    virtual void setInterpreter(tflite::MicroInterpreter* p_interpreter) = 0;
    virtual TfLiteConfig defaultConfig() = 0;
    virtual bool begin(TfLiteConfig config) = 0;
    virtual int availableToWrite() = 0;

    /// process the data in batches of max kMaxAudioSampleSize.
    virtual size_t write(const uint8_t* audio, size_t bytes)= 0;
    virtual tflite::MicroInterpreter& interpreter()= 0;

    /// Provides the TfLiteConfig information
    virtual TfLiteConfig &config()= 0;

    /// Provides access to the model input buffer
    virtual int8_t*  modelInputBuffer()= 0;
};

/**
 * @brief TfLiteMicroSpeachWriter for Audio Data
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteMicroSpeachWriter : public TfLiteWriter {
 public:
  TfLiteMicroSpeachWriter() = default;

  ~TfLiteMicroSpeachWriter() {
    if (p_buffer != nullptr) delete p_buffer;
    if (p_audio_samples != nullptr) delete p_audio_samples;
  }

  /// Call begin before starting the processing
  virtual bool begin(TfLiteAudioStreamBase *parent) {
    LOGD(LOG_METHOD);
    this->parent = parent;
    cfg = parent->config();
    current_time = 0;
    kMaxAudioSampleSize = cfg.audioSampleSize();
    kStrideSampleSize = cfg.strideSampleSize();
    kKeepSampleSize = kMaxAudioSampleSize - kStrideSampleSize;

    if (!setup_recognizer()) {
      LOGE("setup_recognizer");
      return false;
    }

    // setup FrontendConfig
    TfLiteStatus init_status = initializeMicroFeatures();
    if (init_status != kTfLiteOk) {
      return false;
    }

    // Allocate ring buffer
    if (p_buffer == nullptr) {
      p_buffer = new audio_tools::RingBuffer<int16_t>(kMaxAudioSampleSize);
      LOGD("Allocating buffer for %d samples", kMaxAudioSampleSize);
    }

    // Initialize the feature data to default values.
    if (p_feature_data == nullptr) {
      p_feature_data = new int8_t[cfg.featureElementCount()];
      memset(p_feature_data, 0, cfg.featureElementCount());
    }

    // allocate p_audio_samples
    if (p_audio_samples == nullptr) {
      p_audio_samples = new int16_t[kMaxAudioSampleSize];
      memset(p_audio_samples, 0, kMaxAudioSampleSize * sizeof(int16_t));
    }

    return true;
  }

  virtual bool write(int16_t sample) {
    LOGD(LOG_METHOD);
    if (!write1(sample)){
      // determine time
      current_time += cfg.kFeatureSliceStrideMs;
      // determine slice
      total_slice_count++;
      
      int8_t* feature_buffer = addSlice();
      if (total_slice_count >= cfg.kSlicesToProcess) {
        processSlices(feature_buffer);
        // reset total_slice_count
        total_slice_count = 0;
      }
    }
    return true;
  }

 protected:
  TfLiteConfig cfg;
  TfLiteAudioStreamBase *parent=nullptr;
  int8_t* p_feature_data = nullptr;
  int16_t* p_audio_samples = nullptr;
  audio_tools::RingBuffer<int16_t>* p_buffer = nullptr;
  FrontendState g_micro_features_state;
  FrontendConfig config;
  int kMaxAudioSampleSize;
  int kStrideSampleSize;
  int kKeepSampleSize;
  int16_t last_value;
  int8_t channel = 0;
  int32_t current_time = 0;
  int16_t total_slice_count = 0;

  virtual bool setup_recognizer() {
      // setup default p_recognizer if not defined
      if (cfg.recognizeCommands == nullptr) {
        static TfLiteMicroSpeechRecognizeCommands static_recognizer;
        cfg.recognizeCommands = &static_recognizer;
      }
      return cfg.recognizeCommands->begin(cfg);
  }

  /// Processes a single sample 
  virtual bool write1(const int16_t sample) {
    if (cfg.channels == 1) {
      p_buffer->write(sample);
    } else {
      if (channel == 0) {
        last_value = sample;
        channel = 1;
      } else
        // calculate avg of 2 channels and convert it to int8_t
        p_buffer->write(((sample / 2) + (last_value / 2)));
      channel = 0;
    }
    return p_buffer->availableForWrite() > 0;
  }

  // If we can avoid recalculating some slices, just move the existing
  // data up in the spectrogram, to perform something like this: last time
  // = 80ms          current time = 120ms
  // +-----------+             +-----------+
  // | data@20ms |         --> | data@60ms |
  // +-----------+       --    +-----------+
  // | data@40ms |     --  --> | data@80ms |
  // +-----------+   --  --    +-----------+
  // | data@60ms | --  --      |  <empty>  |
  // +-----------+   --        +-----------+
  // | data@80ms | --          |  <empty>  |
  // +-----------+             +-----------+
  virtual int8_t* addSlice() {
    LOGD(LOG_METHOD);
    // shift p_feature_data by one slice one one
    memmove(p_feature_data, p_feature_data + cfg.kFeatureSliceSize,
            (cfg.kFeatureSliceCount - 1) * cfg.kFeatureSliceSize);

    // copy data from buffer to p_audio_samples
    int audio_samples_size =
        p_buffer->readArray(p_audio_samples, kMaxAudioSampleSize);

    // check size
    if (audio_samples_size != kMaxAudioSampleSize) {
      LOGE("audio_samples_size=%d != kMaxAudioSampleSize=%d",
           audio_samples_size, kMaxAudioSampleSize);
    }

    // keep some data to be reprocessed - move by kStrideSampleSize
    p_buffer->writeArray(p_audio_samples + kStrideSampleSize, kKeepSampleSize);

    //  the new slice data will always be stored at the end
    int8_t* new_slice_data =
        p_feature_data + ((cfg.kFeatureSliceCount - 1) * cfg.kFeatureSliceSize);
    size_t num_samples_read = 0;
    if (generateMicroFeatures(p_audio_samples, audio_samples_size,
                              new_slice_data, cfg.kFeatureSliceSize,
                              &num_samples_read) != kTfLiteOk) {
      LOGE("Error generateMicroFeatures");
    }
    // printFeatures();
    return p_feature_data;
  }

  // Process multiple slice of audio data 
  virtual bool processSlices(int8_t* feature_buffer) {
    LOGI("->slices: %d", total_slice_count);
    // Copy feature buffer to input tensor
    memcpy(parent->modelInputBuffer(), feature_buffer, cfg.featureElementCount());

    // Run the model on the spectrogram input and make sure it succeeds.
    TfLiteStatus invoke_status = parent->interpreter().Invoke();
    if (invoke_status != kTfLiteOk) {
      LOGE("Invoke failed");
      return false;
    }

    // Obtain a pointer to the output tensor
    TfLiteTensor* output = parent->interpreter().output(0);

    // Determine whether a command was recognized
    const char* found_command = nullptr;
    uint8_t score = 0;
    bool is_new_command = false;

    TfLiteStatus process_status = cfg.recognizeCommands->processLatestResults(
        output, current_time, &found_command, &score, &is_new_command);
    if (process_status != kTfLiteOk) {
      LOGE("TfLiteMicroSpeechRecognizeCommands::processLatestResults() failed");
      return false;
    }
    // Do something based on the recognized command. The default
    // implementation just prints to the error console, but you should replace
    // this with your own function for a real application.
    respondToCommand(found_command, score, is_new_command);
    return true;
  }

  /// For debugging: print feature matrix
  void printFeatures() {
    for (int i = 0; i < cfg.kFeatureSliceCount; i++) {
      for (int j = 0; j < cfg.kFeatureSliceSize; j++) {
        Serial.print(p_feature_data[(i * cfg.kFeatureSliceSize) + j]);
        Serial.print(" ");
      }
      Serial.println();
    }
    Serial.println("------------");
  }

  virtual TfLiteStatus initializeMicroFeatures() {
    LOGD(LOG_METHOD);
    config.window.size_ms = cfg.kFeatureSliceDurationMs;
    config.window.step_size_ms = cfg.kFeatureSliceStrideMs;
    config.filterbank.num_channels = cfg.kFeatureSliceSize;
    config.filterbank.lower_band_limit = cfg.filterbank_lower_band_limit;
    config.filterbank.upper_band_limit = cfg.filterbank_upper_band_limit;
    config.noise_reduction.smoothing_bits = cfg.noise_reduction_smoothing_bits;
    config.noise_reduction.even_smoothing = cfg.noise_reduction_even_smoothing;
    config.noise_reduction.odd_smoothing = cfg.noise_reduction_odd_smoothing;
    config.noise_reduction.min_signal_remaining = cfg.noise_reduction_min_signal_remaining;
    config.pcan_gain_control.enable_pcan = cfg.pcan_gain_control_enable_pcan;
    config.pcan_gain_control.strength = cfg.pcan_gain_control_strength;
    config.pcan_gain_control.offset = cfg.pcan_gain_control_offset ;
    config.pcan_gain_control.gain_bits = cfg.pcan_gain_control_gain_bits;
    config.log_scale.enable_log = cfg.log_scale_enable_log;
    config.log_scale.scale_shift = cfg.log_scale_scale_shift;
    if (!FrontendPopulateState(&config, &g_micro_features_state,
                               cfg.sample_rate)) {
      LOGE("frontendPopulateState() failed");
      return kTfLiteError;
    }
    return kTfLiteOk;
  }

  virtual TfLiteStatus generateMicroFeatures(const int16_t* input,
                                             int input_size, int8_t* output,
                                             int output_size,
                                             size_t* num_samples_read) {
    LOGD(LOG_METHOD);
    const int16_t* frontend_input = input;

    // Apply FFT
    FrontendOutput frontend_output = FrontendProcessSamples(
        &g_micro_features_state, frontend_input, input_size, num_samples_read);

    // Check size
    if (output_size != frontend_output.size) {
      LOGE("output_size=%d, frontend_output.size=%d", output_size,
           frontend_output.size);
    }

    // printf("input_size: %d, num_samples_read: %d,output_size: %d,
    // frontend_output.size:%d \n", input_size, *num_samples_read, output_size,
    // frontend_output.size);

    // // check generated features
    // if (input_size != *num_samples_read){
    //   LOGE("audio_samples_size=%d vs num_samples_read=%d", input_size,
    //   *num_samples_read);
    // }

    for (size_t i = 0; i < frontend_output.size; ++i) {
      // These scaling values are derived from those used in input_data.py in
      // the training pipeline. The feature pipeline outputs 16-bit signed
      // integers in roughly a 0 to 670 range. In training, these are then
      // arbitrarily divided by 25.6 to get float values in the rough range of
      // 0.0 to 26.0. This scaling is performed for historical reasons, to match
      // up with the output of other feature generators. The process is then
      // further complicated when we quantize the model. This means we have to
      // scale the 0.0 to 26.0 real values to the -128 to 127 signed integer
      // numbers. All this means that to get matching values from our integer
      // feature output into the tensor input, we have to perform: input =
      // (((feature / 25.6) / 26.0) * 256) - 128 To simplify this and perform it
      // in 32-bit integer math, we rearrange to: input = (feature * 256) /
      // (25.6 * 26.0) - 128
      constexpr int32_t value_scale = 256;
      constexpr int32_t value_div =
          static_cast<int32_t>((25.6f * 26.0f) + 0.5f);
      int32_t value =
          ((frontend_output.values[i] * value_scale) + (value_div / 2)) /
          value_div;
      value -= 128;
      if (value < -128) {
        value = -128;
      }
      if (value > 127) {
        value = 127;
      }
      output[i] = value;
    }

    return kTfLiteOk;
  }

  /// Overwrite this method to implement your own handler or provide callback
  virtual void respondToCommand(const char* found_command, uint8_t score,
                                bool is_new_command) {
    if (cfg.respondToCommand != nullptr) {
      cfg.respondToCommand(found_command, score, is_new_command);
    } else {
      LOGD(LOG_METHOD);
      if (is_new_command) {
        char buffer[80];
        sprintf(buffer, "Result: %s, score: %d, is_new: %s", found_command,
                score, is_new_command ? "true" : "false");
        Serial.println(buffer);
      }
    }
  }
};

/**
 * @brief Generate a sine output from a model that was trained on the sine method.
 * (=hello_world)
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteSineReader : public TfLiteReader {
  public: TfLiteSineReader(int16_t range=32767, float increment=0.01 ){
    this->increment = increment;
    this->range = range;
  }

  virtual bool begin(TfLiteAudioStreamBase *parent) override {
    // setup on first call
      p_interpreter = &parent->interpreter();
      input = p_interpreter->input(0);
      output = p_interpreter->output(0);
      channels = parent->config().channels;
      return true;
  }

  virtual int read(int16_t*data, int sampleCount) override {
    LOGD(LOG_METHOD);
    float two_pi = 2 * PI;
    for (int j=0; j<sampleCount; j+=channels){
      // Quantize the input from floating-point to integer
      input->data.int8[0] = TfQuantizer::quantize(actX,input->params.scale, input->params.zero_point);
      
      // Invoke TF Model
      TfLiteStatus invoke_status = p_interpreter->Invoke();

      // Check the result
      if(kTfLiteOk!= invoke_status){
        LOGE("invoke_status not ok");
        return j;
      }
      if(kTfLiteInt8 != output->type){
        LOGE("Output type is not kTfLiteInt8");
        return j;
      }

      // Dequantize the output and convet it to int32 range
      data[j] = TfQuantizer::dequantizeToNewRange(output->data.int8[0], output->params.scale, output->params.zero_point, range);
      // printf("%d\n", data[j]);  // for debugging using the Serial Plotter
      LOGD("%f->%d / %d->%d",actX, input->data.int8[0], output->data.int8[0], data[j]);
      for (int i=1;i<channels;i++){
          data[j+i] = data[j];
          LOGD("generate data for channels");
      }
      // Increment X
      actX += increment;
      if (actX>two_pi){
        actX-=two_pi;
      }
    }
    return sampleCount;
  }

  protected:
    float actX=0;
    float increment=0.1;
    int16_t range=0;
    int channels;
    TfLiteTensor* input = nullptr;
    TfLiteTensor* output = nullptr;
    tflite::MicroInterpreter* p_interpreter = nullptr;
};

/**
 * @brief TfLiteAudioStream which uses Tensorflow Light to analyze the data. If it is used as a generator (where we read audio data) 
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class TfLiteAudioStream : public TfLiteAudioStreamBase {
 public:
  TfLiteAudioStream() {}
  ~TfLiteAudioStream() {
    if (p_tensor_arena != nullptr) delete[] p_tensor_arena;
  }


  /// Optionally define your own p_interpreter
  void setInterpreter(tflite::MicroInterpreter* p_interpreter) {
    LOGD(LOG_METHOD);
    this->p_interpreter = p_interpreter;
  }

  // Provides the default configuration
  virtual TfLiteConfig defaultConfig() override {
    TfLiteConfig def;
    return def;
  }

  /// Start the processing
  virtual bool begin(TfLiteConfig config) override {
    LOGD(LOG_METHOD);
    cfg = config;
   
    // alloatme memory
    p_tensor_arena = new uint8_t[cfg.kTensorArenaSize];

    if (cfg.categoryCount()>0){

      // setup the feature provider
      if (!setupWriter()) {
        LOGE("setupWriter");
        return false;
      }
    } else {
      LOGW("categoryCount=%d", cfg.categoryCount());
    }

    // Map the model into a usable data structure. This doesn't involve any
    // copying or parsing, it's a very lightweight operation.
    if (!setModel(cfg.model)) {
      return false;
    }

    if (!setupInterpreter()) {
      return false;
    }

    // Allocate memory from the p_tensor_arena for the model's tensors.
    LOGI("AllocateTensors");
    TfLiteStatus allocate_status = p_interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
      LOGE("AllocateTensors() failed");
      return false;
    }

    // Get information about the memory area to use for the model's input.
    LOGI("Get Input");
    p_tensor = p_interpreter->input(0);
    if (cfg.categoryCount()>0){
      if ((p_tensor->dims->size != 2) || (p_tensor->dims->data[0] != 1) ||
          (p_tensor->dims->data[1] !=
          (cfg.kFeatureSliceCount * cfg.kFeatureSliceSize)) ||
          (p_tensor->type != kTfLiteInt8)) {
        LOGE("Bad input tensor parameters in model");
        return false;
      }
    }

    LOGI("Get Buffer");
    p_tensor_buffer = p_tensor->data.int8;
    if (p_tensor_buffer == nullptr) {
      LOGE("p_tensor_buffer is null");
      return false;
    }

    // setup reader
    if (cfg.reader!=nullptr){
      cfg.reader->begin(this);
    }

    // all good if we made it here
    is_setup = true;
    LOGI("done");
    return true;
  }

  /// Constant streaming
  virtual int availableToWrite() override { return DEFAULT_BUFFER_SIZE; }

  /// process the data in batches of max kMaxAudioSampleSize.
  virtual size_t write(const uint8_t* audio, size_t bytes) override {
    LOGD(LOG_METHOD);
    if (cfg.writer==nullptr){
      LOGE("cfg.output is null");
      return 0;
    }
    int16_t* samples = (int16_t*)audio;
    int16_t sample_count = bytes / 2;
    for (int j = 0; j < sample_count; j++) {
      cfg.writer->write(samples[j]);
    }
    return bytes;
  }

  /// We can provide only some audio data when cfg.input is defined
  virtual int available() override { return cfg.reader != nullptr ? DEFAULT_BUFFER_SIZE : 0; }

  /// provide audio data with cfg.input 
  virtual size_t readBytes(uint8_t *data, size_t len) override {
    LOGD(LOG_METHOD);
    if (cfg.reader!=nullptr){
      return cfg.reader->read((int16_t*)data, (int) len/sizeof(int16_t)) * sizeof(int16_t);
    }else {
      return 0;
    }
  }

  /// Provides the tf lite interpreter
  tflite::MicroInterpreter& interpreter() override {
    return *p_interpreter;
  }

  /// Provides the TfLiteConfig information
  TfLiteConfig &config() override {
    return cfg;
  }

  /// Provides access to the model input buffer
  int8_t*  modelInputBuffer() override {
    return p_tensor_buffer;
  }

 protected:
  const tflite::Model* p_model = nullptr;
  tflite::MicroInterpreter* p_interpreter = nullptr;
  TfLiteTensor* p_tensor = nullptr;
  bool is_setup = false;
  TfLiteConfig cfg;
  // Create an area of memory to use for input, output, and intermediate
  // arrays. The size of this will depend on the model you're using, and may
  // need to be determined by experimentation.
  uint8_t* p_tensor_arena = nullptr;
  int8_t* p_tensor_buffer = nullptr;

  virtual bool setModel(const unsigned char* model) {
    LOGD(LOG_METHOD);
    p_model = tflite::GetModel(model);
    if (p_model->version() != TFLITE_SCHEMA_VERSION) {
      LOGE(
          "Model provided is schema version %d not equal "
          "to supported version %d.",
          p_model->version(), TFLITE_SCHEMA_VERSION);
      return false;
    }
    return true;
  }

  virtual bool setupWriter() {
    if (cfg.writer == nullptr) {
      static TfLiteMicroSpeachWriter writer;
      cfg.writer = &writer;
    }
    return cfg.writer->begin(this);
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  virtual bool setupInterpreter() {
    if (p_interpreter == nullptr) {
      LOGI(LOG_METHOD);
      if (cfg.useAllOpsResolver) {
        tflite::AllOpsResolver resolver;
        static tflite::MicroInterpreter static_interpreter(
            p_model, resolver, p_tensor_arena, cfg.kTensorArenaSize,
            error_reporter);
        p_interpreter = &static_interpreter;
      } else {
        // NOLINTNEXTLINE(runtime-global-variables)
        static tflite::MicroMutableOpResolver<4> micro_op_resolver(
            error_reporter);
        if (micro_op_resolver.AddDepthwiseConv2D() != kTfLiteOk) {
          return false;
        }
        if (micro_op_resolver.AddFullyConnected() != kTfLiteOk) {
          return false;
        }
        if (micro_op_resolver.AddSoftmax() != kTfLiteOk) {
          return false;
        }
        if (micro_op_resolver.AddReshape() != kTfLiteOk) {
          return false;
        }
        // Build an p_interpreter to run the model with.
        static tflite::MicroInterpreter static_interpreter(
            p_model, micro_op_resolver, p_tensor_arena, cfg.kTensorArenaSize,
            error_reporter);
        p_interpreter = &static_interpreter;
      }
    }
    return true;
  }
};

}  // namespace audio_tools