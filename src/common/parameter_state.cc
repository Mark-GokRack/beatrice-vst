// Copyright (c) 2024 Project Beatrice

#include "common/parameter_state.h"

#include <memory>
#include <string>

namespace beatrice::common {

ParameterState::ParameterState(const ParameterState& rhs) {
  for (const auto& [key, value] : rhs.states_) {
    const auto [group_id, param_id] = key;
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
            SetValue(group_id, param_id, value);
          } else if constexpr (std::is_same_v<T,  // NOLINT(readability/braces)
                                              std::unique_ptr<std::u8string>>) {
            SetValue(group_id, param_id, *value);
          } else {
            assert(false);
          }
        },
        value);
  }
}

auto ParameterState::operator=(const ParameterState& rhs) -> ParameterState& {
  states_.clear();
  for (const auto& [key, value] : rhs.states_) {
    const auto [group_id, param_id] = key;
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
            SetValue(group_id, param_id, value);
          } else if constexpr (std::is_same_v<T,  // NOLINT(readability/braces)
                                              std::unique_ptr<std::u8string>>) {
            SetValue(group_id, param_id, *value);
          } else {
            assert(false);
          }
        },
        value);
  }
  return *this;
}

void ParameterState::SetDefaultValues(const ParameterSchema& schema) {
  for (const auto& [group_id, group] : schema) {
    for (const auto& [param_id, param] : group) {
      std::visit(
          [&, this](const auto& param) {
            SetValue(group_id, param_id, param.GetDefaultValue());
          },
          param);
    }
  }
}

auto ParameterState::GetValue(const int group_id, const int param_id) const
    -> const ParameterState::Value& {
  return states_.at(std::make_tuple(group_id, param_id));
}

auto ParameterState::Read(std::istream& is) -> int {
  while (true) {
    int group_id;
    int param_id;
    int type_index;
    if (is.read(reinterpret_cast<char*>(&group_id), sizeof(int)).eof()) {
      return 1;
    }
    if (is.read(reinterpret_cast<char*>(&param_id), sizeof(int)).eof()) {
      return 1;
    }
    if (is.read(reinterpret_cast<char*>(&type_index), sizeof(int)).eof()) {
      return 1;
    }
    switch (type_index) {
      case 0: {
        int value;
        if (is.read(reinterpret_cast<char*>(&value), sizeof(int)).eof()) {
          return 1;
        }
        SetValue(group_id, param_id, value);
        break;
      }
      case 1: {
        double value;
        if (is.read(reinterpret_cast<char*>(&value), sizeof(double)).eof()) {
          return 1;
        }
        SetValue(group_id, param_id, value);
        break;
      }
      case 2: {
        int siz;
        if (is.read(reinterpret_cast<char*>(&siz), sizeof(int)).eof()) {
          return 1;
        }
        std::u8string value;
        value.resize(siz);
        if (is.read(reinterpret_cast<char*>(value.data()), siz).eof()) {
          return 1;
        }
        SetValue(group_id, param_id, value);
        break;
      }
      default:
        assert(false);
        return 1;
    }
    if (is.peek() == EOF && is.eof()) {
      return 0;
    }
  }
}

auto ParameterState::ReadOrSetDefault(std::istream& is,
                                      const ParameterSchema& schema) -> int {
  states_.clear();
  SetDefaultValues(schema);
  return Read(is);
}

auto ParameterState::Write(std::ostream& os) const -> int {
  for (const auto& [key, value] : states_) {
    const auto [group_id, param_id] = key;
    const auto type_index = static_cast<int>(value.index());
    os.write(reinterpret_cast<const char*>(&group_id), sizeof(int));
    os.write(reinterpret_cast<const char*>(&param_id), sizeof(int));
    os.write(reinterpret_cast<const char*>(&type_index), sizeof(int));
    if (const auto* const p = std::get_if<int>(&value)) {
      os.write(reinterpret_cast<const char*>(p), sizeof(int));
    } else if (const auto* const p = std::get_if<double>(&value)) {
      os.write(reinterpret_cast<const char*>(p), sizeof(double));
    } else if (const auto* const pp =
                   std::get_if<std::unique_ptr<std::u8string>>(&value)) {
      const auto& p = *pp;
      const auto siz = static_cast<int>(p->size());
      os.write(reinterpret_cast<const char*>(&siz), sizeof(int));
      os.write(reinterpret_cast<const char*>(p->c_str()), siz);
    } else {
      assert(false);
    }
  }
  return 0;
}
}  // namespace beatrice::common
