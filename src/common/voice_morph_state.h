// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_VOICE_MORPH_STATE_H_
#define BEATRICE_COMMON_VOICE_MORPH_STATE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "common/model_config.h"

namespace beatrice::common {

static constexpr auto kMaxNVoiceMorphMarkers = 8;
static constexpr auto kDefaultNVoiceMorphMarkers = 4;
static constexpr auto kVoiceMorphFalloffMin = 0.0f;
static constexpr auto kVoiceMorphFalloffMax = 4.0f;
static constexpr auto kVoiceMorphFalloffStep = 0.1f;
static constexpr auto kVoiceMorphFalloffDivisions = 40;
static constexpr auto kVoiceMorphFalloffDefault = 2.0f;
static constexpr auto kVoiceMorphWeightThreshold = 0.01f;

static_assert(kDefaultNVoiceMorphMarkers <= kMaxNVoiceMorphMarkers);
static_assert(kDefaultNVoiceMorphMarkers <= static_cast<int>(kMaxNSpeakers));

struct VoiceMorphMarker {
  int voice_id = 0;
  float x = 0.5f;
  float y = 0.5f;
};

struct VoiceMorphState {
  float cursor_x = 0.5f;
  float cursor_y = 0.5f;
  float falloff = kVoiceMorphFalloffDefault;
  std::array<VoiceMorphMarker, kMaxNVoiceMorphMarkers> markers = {
      VoiceMorphMarker{.voice_id = 0, .x = 0.18f, .y = 0.5f},
      VoiceMorphMarker{.voice_id = 1, .x = 0.82f, .y = 0.5f},
      VoiceMorphMarker{.voice_id = 2, .x = 0.5f, .y = 0.18f},
      VoiceMorphMarker{.voice_id = 3, .x = 0.5f, .y = 0.82f},
  };
  int marker_count = kDefaultNVoiceMorphMarkers;

  [[nodiscard]] auto CalculateMarkerWeights() const
      -> std::array<float, kMaxNVoiceMorphMarkers>;
  [[nodiscard]] auto CalculateWeights() const
      -> std::array<float, kMaxNSpeakers>;
  void SetMarkerWeights(
      const std::array<float, kMaxNVoiceMorphMarkers>& marker_weights);
};

inline auto VoiceMorphState::CalculateMarkerWeights() const
    -> std::array<float, kMaxNVoiceMorphMarkers> {
  auto marker_weights = std::array<float, kMaxNVoiceMorphMarkers>{};
  constexpr auto kEpsilon = 0.0008f;
  if (falloff <= kVoiceMorphFalloffMin) {
    const auto weight = 1.0f / static_cast<float>(marker_count);
    std::fill_n(marker_weights.begin(), marker_count, weight);
    return marker_weights;
  }

  auto total = 0.0f;
  for (auto i = 0; i < marker_count; ++i) {
    const auto& marker = markers[i];
    const auto dx = cursor_x - marker.x;
    const auto dy = cursor_y - marker.y;
    const auto distance_squared = dx * dx + dy * dy;
    marker_weights[i] = 1.0f / std::pow(distance_squared + kEpsilon, falloff);
    total += marker_weights[i];
  }
  for (auto i = 0; i < marker_count; ++i) {
    marker_weights[i] /= total;
  }
  return marker_weights;
}

inline auto VoiceMorphState::CalculateWeights() const
    -> std::array<float, kMaxNSpeakers> {
  auto weights = std::array<float, kMaxNSpeakers>{};
  const auto marker_weights = CalculateMarkerWeights();
  for (auto i = 0; i < marker_count; ++i) {
    const auto voice_id =
        std::clamp(markers[i].voice_id, 0, static_cast<int>(kMaxNSpeakers) - 1);
    weights[voice_id] += marker_weights[i];
  }
  return weights;
}

inline void VoiceMorphState::SetMarkerWeights(
    const std::array<float, kMaxNVoiceMorphMarkers>& marker_weights) {
  constexpr auto kCenter = 0.5f;
  constexpr auto kMaxRadius = 0.4f;
  constexpr auto kEpsilon = 0.0008f;
  cursor_x = kCenter;
  cursor_y = kCenter;
  if (marker_count <= 0) {
    return;
  }
  if (marker_count == 1) {
    markers[0].x = kCenter;
    markers[0].y = kCenter;
    return;
  }

  auto total_weight = 0.0f;
  auto min_positive_weight = 0.0f;
  for (auto i = 0; i < marker_count; ++i) {
    const auto weight = std::max(marker_weights[i], 0.0f);
    total_weight += weight;
    if (weight > 0.0f &&
        (min_positive_weight == 0.0f || weight < min_positive_weight)) {
      min_positive_weight = weight;
    }
  }
  for (auto i = 0; i < marker_count; ++i) {
    const auto angle =
        static_cast<float>(2.0 * std::numbers::pi * i / marker_count);
    auto radius = kMaxRadius;
    const auto weight = std::max(marker_weights[i], 0.0f);
    if (falloff > kVoiceMorphFalloffMin && total_weight > 0.0f &&
        weight > 0.0f) {
      const auto scale = (kMaxRadius * kMaxRadius + kEpsilon) *
                         std::pow(min_positive_weight, 1.0f / falloff);
      const auto radius_squared =
          scale * std::pow(weight, -1.0f / falloff) - kEpsilon;
      radius = std::sqrt(std::max(radius_squared, 0.0f));
    }
    markers[i].x = kCenter + radius * std::cos(angle);
    markers[i].y = kCenter + radius * std::sin(angle);
  }
}

[[nodiscard]] inline auto PrepareVoiceMorphWeights(
    std::array<float, kMaxNSpeakers> weights, const int speaker_count)
    -> std::array<float, kMaxNSpeakers> {
  if (speaker_count <= 0) {
    return {};
  }
  const auto count = std::min(speaker_count, static_cast<int>(kMaxNSpeakers));
  for (auto i = count; i < static_cast<int>(kMaxNSpeakers); ++i) {
    weights[count - 1] += weights[i];
  }
  std::fill(weights.begin() + count, weights.end(), 0.0f);
  for (auto i = 0; i < count; ++i) {
    if (weights[i] < kVoiceMorphWeightThreshold) {
      weights[i] = 0.0f;
    }
  }
  return weights;
}

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_VOICE_MORPH_STATE_H_
