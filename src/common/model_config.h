// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_MODEL_CONFIG_H_
#define BEATRICE_COMMON_MODEL_CONFIG_H_

#include <array>
#include <stdexcept>
#include <string>

#include "toml11/single_include/toml.hpp"

namespace beatrice::common {

static constexpr auto kMaxNSpeakers = 256;

// モデル情報の TOML ファイルを読み込むための構造体
struct ModelConfig {
  struct Model {
    std::string version;
    std::u8string name;
    std::u8string description;
    [[nodiscard]] auto VersionInt() const -> int {
      if (version == "2.0.0-alpha.2") {
        return 0;
      } else if (version == "2.0.0-beta.1") {
        return 1;
      } else {
        return -1;
      }
    }
  } model;
  struct Voice {
    struct Portrait {
      std::u8string path;
      std::u8string description;
    };
    std::u8string name;
    std::u8string description;
    double average_pitch;
    Portrait portrait;
  };
  std::array<Voice, kMaxNSpeakers> voices;
};
}  // namespace beatrice::common

namespace toml {

using beatrice::common::ModelConfig;

template <>
struct from<ModelConfig::Model> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Model {
    const auto name = find<std::string>(v, "name");
    const auto description = find<std::string>(v, "description");
    return ModelConfig::Model{
        .version = find<std::string>(v, "version"),
        .name = std::u8string(name.begin(), name.end()),
        .description = std::u8string(description.begin(), description.end())};
  }
};
template <>
struct from<ModelConfig::Voice::Portrait> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Voice::Portrait {
    const auto path = find<std::string>(v, "path");
    const auto description = find<std::string>(v, "description");
    return ModelConfig::Voice::Portrait{
        .path = std::u8string(path.begin(), path.end()),
        .description = std::u8string(description.begin(), description.end())};
  }
};

template <>
struct from<ModelConfig::Voice> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Voice {
    const auto name = find<std::string>(v, "name");
    const auto description = find<std::string>(v, "description");
    return ModelConfig::Voice{
        .name = std::u8string(name.begin(), name.end()),
        .description = std::u8string(description.begin(), description.end()),
        .average_pitch = find<double>(v, "average_pitch"),
        .portrait = get<ModelConfig::Voice::Portrait>(find(v, "portrait"))};
  }
};
template <>
struct from<ModelConfig> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig {
    auto target_speakers =
        std::array<ModelConfig::Voice, beatrice::common::kMaxNSpeakers>();
    for (const auto& [key, value] : find<toml::table>(v, "voice")) {
      const auto id = std::stoi(key);
      if (id < 0 || id >= beatrice::common::kMaxNSpeakers) {
        throw std::out_of_range("speaker id out of range");
      }
      target_speakers[id] = get<ModelConfig::Voice>(value);
    }
    return ModelConfig{.model = find<ModelConfig::Model>(v, "model"),
                       .voices = target_speakers};
  }
};
}  // namespace toml

#endif  // BEATRICE_COMMON_MODEL_CONFIG_H_