// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PROCESSOR_CORE_0_H_
#define BEATRICE_COMMON_PROCESSOR_CORE_0_H_

#include <vector>

#include "beatricelib/beatrice.h"

// Beatrice
#include "common/gain.h"
#include "common/processor_core.h"
#include "common/resample.h"

namespace beatrice::common {

// 2.0.0-alpha.2 用の信号処理クラス
class ProcessorCore0 : public ProcessorCoreBase {
 public:
  inline explicit ProcessorCore0(const double sample_rate)
      : ProcessorCoreBase(),
        any_freq_in_out_(sample_rate),
        phone_extractor_(Beatrice20a2_CreatePhoneExtractor()),
        pitch_estimator_(Beatrice20a2_CreatePitchEstimator()),
        waveform_generator_(Beatrice20a2_CreateWaveformGenerator()),
        gain_(),
        phone_context_(Beatrice20a2_CreatePhoneContext1()),
        pitch_context_(Beatrice20a2_CreatePitchContext1()),
        waveform_context_(Beatrice20a2_CreateWaveformContext1()),
        input_gain_context_(sample_rate),
        output_gain_context_(sample_rate) {}
  inline ~ProcessorCore0() override {
    Beatrice20a2_DestroyPhoneExtractor(phone_extractor_);
    Beatrice20a2_DestroyPitchEstimator(pitch_estimator_);
    Beatrice20a2_DestroyWaveformGenerator(waveform_generator_);
    Beatrice20a2_DestroyPhoneContext1(phone_context_);
    Beatrice20a2_DestroyPitchContext1(pitch_context_);
    Beatrice20a2_DestroyWaveformContext1(waveform_context_);
  }
  [[nodiscard]] auto GetVersion() const -> int override;
  auto Process(const float* input, float* output,
               int n_samples) -> int override;
  auto ResetContext() -> int override;
  auto LoadModel(const ModelConfig& /*config*/,
                 const std::filesystem::path& /*file*/) -> int override;
  auto SetSampleRate(double /*sample_rate*/) -> int override;
  auto SetTargetSpeaker(int /*target_speaker*/) -> int override;
  auto SetFormantShift(double /*formant_shift*/) -> int override;
  auto SetPitchShift(double /*pitch_shift*/) -> int override;
  auto SetInputGain(double /*input_gain*/) -> int override;
  auto SetOutputGain(double /*output_gain*/) -> int override;
  auto SetSpeakerMergeRatio(
    int /*num of targets*/, int* /*target indices*/, double* /*merge ratio*/
  ) -> int override;

 private:
  class ConvertWithModelBlockSize {
   public:
    ConvertWithModelBlockSize() = default;
    void operator()(const float* const input, float* const output,
                    ProcessorCore0& processor_core) const {
      processor_core.Process1(input, output);
    }
  };

  std::filesystem::path model_file_;
  int target_speaker_ = 0;
  double formant_shift_ = 0.0;
  double pitch_shift_ = 0.0;
  int n_speakers_ = 0;

  resampler::AnyFreqInOut<ConvertWithModelBlockSize> any_freq_in_out_;

  // モデル
  Beatrice20a2_PhoneExtractor* phone_extractor_;
  Beatrice20a2_PitchEstimator* pitch_estimator_;
  Beatrice20a2_WaveformGenerator* waveform_generator_;
  std::vector<float> speaker_embeddings_;
  std::vector<float> formant_shift_embeddings_;
  Gain gain_;
  // 状態
  Beatrice20a2_PhoneContext1* phone_context_;
  Beatrice20a2_PitchContext1* pitch_context_;
  Beatrice20a2_WaveformContext1* waveform_context_;
  Gain::Context input_gain_context_;
  Gain::Context output_gain_context_;

  inline auto IsLoaded() -> bool { return !model_file_.empty(); }
  void Process1(const float* input, float* output);
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_CORE_0_H_
