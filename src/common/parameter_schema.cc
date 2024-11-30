// Copyright (c) 2024 Project Beatrice

#include "common/parameter_schema.h"

#include <exception>
#include <filesystem>

#include "common/controller_core.h"
#include "common/processor_core.h"
#include "common/processor_proxy.h"
#include "toml11/single_include/toml.hpp"

namespace beatrice::common {

using std::operator""s;

static constexpr auto kMaxAbsPitchShift = 24.0;

// パラメータの追加には以下 3 箇所の変更が必要
// * parameter_schema.h, parameter_schema.cc (メタデータの設定)
// * processor_core.h, processor_core_*.cc   (信号処理)
// * editor.cc                               (GUI)

// パラメータ ID に対して、そのパラメータはどのような名前で、
// どのような値域を持ち、どのような操作に対応するのかを保持する。
const ParameterSchema kSchema = [] {
  auto schema = ParameterSchema({
      {ParameterID::kModel,
       StringParameter(
           u8"Model"s, u8""s, false,
           [](ControllerCore& controller, const std::u8string& value) {
             ModelConfig model_config;
             try {
               const auto toml_data = toml::parse(std::filesystem::path(value));
               model_config = toml::get<ModelConfig>(toml_data);
             } catch (const toml::file_io_error& e) {
               return ErrorCode::kFileOpenError;
             } catch (const toml::syntax_error& e) {
               return ErrorCode::kTOMLSyntaxError;
             } catch (const std::exception& e) {
               return ErrorCode::kUnknownError;
             }

             // Voice
             controller.parameter_state_.SetValue(ParameterID::kVoice, 0);
             controller.updated_parameters_.push_back(ParameterID::kVoice);
             // FormantShift
             controller.parameter_state_.SetValue(ParameterID::kFormantShift,
                                                  0.0);
             controller.updated_parameters_.push_back(
                 ParameterID::kFormantShift);
             // MergedVoiceIndex
             
             auto merged_voice_id = kMaxNSpeakers;
             for ( auto i = 0; i < kMaxNSpeakers; ++i ) {
                const auto& voice = model_config.voices[i];
                if (voice.name.empty() && voice.description.empty() &&
                    voice.portrait.path.empty() && voice.portrait.description.empty()) {
                  merged_voice_id = i;
                  break;
                }
             }
             
             controller.parameter_state_.SetValue(ParameterID::kMergedVoiceIndex, (double)merged_voice_id);
             controller.updated_parameters_.push_back(ParameterID::kMergedVoiceIndex);
             if( merged_voice_id < kMaxNSpeakers ){
              double average_merge_pitch = 0;
              for (auto i = 0; i < merged_voice_id; ++i) {
                average_merge_pitch += model_config.voices[i].average_pitch;
              }
              average_merge_pitch /= merged_voice_id;
              model_config.voices[merged_voice_id].average_pitch = average_merge_pitch;
             }
             // AverageTargetPitches
             for (auto i = 0; i < kMaxNSpeakers; ++i) {
               controller.parameter_state_.SetValue(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                       i),
                   model_config.voices[i].average_pitch);
               controller.updated_parameters_.push_back(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                       i));
             }
             // kVoiceMergeWeights
             for (auto i = 0; i < kMaxNSpeakers; ++i) {
               controller.parameter_state_.SetValue(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kVoiceMergeWeight) +
                       i), 0.0);
               controller.updated_parameters_.push_back(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kVoiceMergeWeight) +
                       i));
             }
   
             const auto average_target_pitch =
                 model_config.voices[0].average_pitch;
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift =
                     std::clamp(average_target_pitch - average_source_pitch,
                                -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {  // PitchShift 固定
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const std::u8string& value) {
             return vc.LoadModel(value);
           })},
      {ParameterID::kVoice,
       ListParameter(
           u8"Voice"s,
           [] {
             auto v = std::vector<std::u8string>();
             for (auto i = 0; i < kMaxNSpeakers; ++i) {
               const auto i_ascii = std::to_string(i);
               const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
               v.push_back(u8"ID "s + i_u8);
             }
             return v;
           }(),
           0, u8"Voi"s, parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const int value) {
             if (value < 0 || value >= kMaxNSpeakers) {
               return ErrorCode::kSpeakerIDOutOfRange;
             }
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     value)));
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift =
                     std::clamp(average_target_pitch + formant_shift -
                                    average_source_pitch,
                                -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {  // PitchShift 固定
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch + formant_shift - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const int value) {
             return vc.GetCore()->SetTargetSpeaker(value);
           })},
      {ParameterID::kFormantShift,
       NumberParameter(
           u8"FormantShift"s, 0.0, -2.0, 2.0, u8"semitones"s, 8, u8"For"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const double value) {
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift = std::clamp(
                     average_target_pitch + value - average_source_pitch,
                     -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch + value - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetFormantShift(value);
           })},
      {ParameterID::kPitchShift,
       NumberParameter(
           u8"PitchShift"s, 0.0, -kMaxAbsPitchShift, kMaxAbsPitchShift,
           u8"semitones"s, 48 * 8, u8"Pit"s, parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const double value) {
             // AverageSourcePitch を変更する
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             // clamp するべき？
             const auto average_source_pitch =
                 average_target_pitch + formant_shift - value;
             controller.parameter_state_.SetValue(
                 ParameterID::kAverageSourcePitch, average_source_pitch);
             controller.updated_parameters_.push_back(
                 ParameterID::kAverageSourcePitch);
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetPitchShift(value);
           })},
      {ParameterID::kAverageSourcePitch,
       NumberParameter(
           u8"AverageSourcePitch"s, 52.0, 0.0, 128.0, u8""s, 128 * 8,
           u8"SrcPit"s, parameter_flag::kNoFlags,
           [](ControllerCore& controller, const double value) {
             // PitchShift を変更する
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             const auto pitch_shift =
                 std::clamp(average_target_pitch + formant_shift - value,
                            -kMaxAbsPitchShift, kMaxAbsPitchShift);
             controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                  pitch_shift);
             controller.updated_parameters_.push_back(ParameterID::kPitchShift);
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetAverageSourcePitch(value);
           })},
      {ParameterID::kLock,
       ListParameter(
           u8"Lock"s, {u8"AverageSourcePitch"s, u8"PitchShift"s}, 0, u8"Loc"s,
           parameter_flag::kIsList,
           [](ControllerCore&, int) { return ErrorCode::kSuccess; },
           [](ProcessorProxy&, int) { return ErrorCode::kSuccess; })},
      {ParameterID::kInputGain,
       NumberParameter(
           u8"InputGain"s, 0.0, -60.0, 20.0, u8"dB"s, 0, u8"Gain/In"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetInputGain(value);
           })},
      {ParameterID::kOutputGain,
       NumberParameter(
           u8"OutputGain"s, 0.0, -60.0, 20.0, u8"dB"s, 0, u8"Gain/Out"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetOutputGain(value);
           })},
      {ParameterID::kIntonationIntensity,
       NumberParameter(
           u8"IntonationIntensity"s, 1.0, -1.0, 3.0, u8""s, 40, u8"Inton"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetIntonationIntensity(value);
           })},
      {ParameterID::kPitchCorrection,
       NumberParameter(
           u8"PitchCorrection"s, 0.0, 0.0, 1.0, u8""s, 10, u8"PitCor"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetPitchCorrection(value);
           })},
      {ParameterID::kPitchCorrectionType,
       ListParameter(
           u8"PitchCorrectionType"s, {u8"Hard 0"s, u8"Hard 1"s}, 0, u8"CorTyp"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, int) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const int value) {
             return vc.GetCore()->SetPitchCorrectionType(value);
           })},
      {ParameterID::kMergedVoiceIndex,
        NumberParameter(
            u8"MergedVoiceIndex"s, kMaxNSpeakers, 0.0, kMaxNSpeakers, u8""s, kMaxNSpeakers, u8"MrgID"s,
            parameter_flag::kIsReadOnly | parameter_flag::kIsHidden,
            [](ControllerCore&, double) { return ErrorCode::kSuccess; },
            [](ProcessorProxy&, double) { return ErrorCode::kSuccess; }
        )},
  });

  for (auto i = 0; i < kMaxNSpeakers; ++i) {
    const auto i_ascii = std::to_string(i);
    const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kAverageTargetPitchBase) + i),
        NumberParameter(
            u8"Speaker "s + i_u8, 60.0, 0.0, 128.0, u8""s, 128 * 8, u8"TgtPit"s,
            parameter_flag::kIsReadOnly | parameter_flag::kIsHidden,
            [](ControllerCore&, double) { return ErrorCode::kSuccess; },
            [](ProcessorProxy&, double) { return ErrorCode::kSuccess; }));
  }
  for (auto i = 0; i < kMaxNSpeakers; ++i) {
    const auto i_ascii = std::to_string(i);
    const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kVoiceMergeWeight) + i),
        NumberParameter(
            u8"Voice "s + i_u8 + u8"'s Weight"s , 0.0, 0.0, 1.0, u8""s, 100, u8"VcWght"s,
            parameter_flag::kCanAutomate,
            [](ControllerCore& controller, double value) {
              /*
              // マージの比率に応じて AverageTargetPitchBase も変える？
              // ここまでする必要ってあるのかな？
              auto merged_voice_id = static_cast<int>( std::get<double>(
                controller.parameter_state_.GetValue( ParameterID::kMergedVoiceIndex )) );
              double weighted_average_pitch = 0.0;
              double simple_average_pitch = 0.0;
              double sum_weights = 0.0;
              for( auto i = 0; i < merged_voice_id; i++){
                auto weight = std::get<double>(
                  controller.parameter_state_.GetValue(static_cast<ParameterID>(
                    static_cast<int>(ParameterID::kVoiceMergeWeight) + i)));
                auto avg_pitch = std::get<double>(
                  controller.parameter_state_.GetValue(static_cast<ParameterID>(
                    static_cast<int>(ParameterID::kAverageTargetPitchBase) + i)));
                weighted_average_pitch += weight * avg_pitch;
                simple_average_pitch += avg_pitch;
                sum_weights += weight;
              }
              if( sum_weights > 0 ){
                weighted_average_pitch /= sum_weights;
              }else{
                weighted_average_pitch = simple_average_pitch / merged_voice_id;
              }
              controller.parameter_state_.SetValue(static_cast<ParameterID>(
                static_cast<int>(ParameterID::kAverageTargetPitchBase) + merged_voice_id),
                weighted_average_pitch);
              */
              return ErrorCode::kSuccess;
            },
            // ここ、NumberParameter::processor_set_value_ が関数ポインタのままだと
            // キャプチャ付きのラムダ式を格納できなかった。
            // std::function を用いる定義に書き直すと格納できるようになる。
            [i](ProcessorProxy& vc, double value) { 
              return vc.GetCore()->SetSpeakerMergeWeight( i, value );
            }
        ));
  }
  for (auto i = 0; i < kMaxNSpeakers; ++i) {
    const auto i_ascii = std::to_string(i);
    const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kVoiceMergeLabels) + i),
        StringParameter(
            u8"Voice "s + i_u8 + u8"'s Weight"s,
            u8""s, true,
            [](ControllerCore& controller, const std::u8string& value) {
              return ErrorCode::kSuccess;
            },
            [](ProcessorProxy& vc, const std::u8string& value) { 
              return ErrorCode::kSuccess;
            }
        ));
  }

  return schema;
}();
}  // namespace beatrice::common
