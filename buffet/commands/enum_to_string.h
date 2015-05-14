// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_ENUM_TO_STRING_H_
#define BUFFET_COMMANDS_ENUM_TO_STRING_H_

#include <string>

namespace buffet {

// Helps to map enumeration to stings and back.
//
// Usage example:
// .h file:
// enum class MyEnum { kV1, kV2 };
// std::string ToString(MyEnum id);
// bool FromString(const std::string& str, MyEnum* id);
//
// .cc file:
// template <>
// const EnumToString<MyEnum>::Map EnumToString<MyEnum>::kMap[] = {
//     {MyEnum::kV1, "v1"},
//     {MyEnum::kV2, "v2"},
// };
//
// std::string ToString(MyEnum id) {
//   return EnumToString<MyEnum>::FindNameById(id);
// }
//
// bool FromString(const std::string& str, MyEnum* id) {
//   return EnumToString<MyEnum>::FindIdByName(str, id));
// }
template <typename T>
class EnumToString final {
 public:
  static std::string FindNameById(T id) {
    for (const Map& m : kMap) {
      if (m.id == id) {
        CHECK(m.name);
        return m.name;
      }
    }
    NOTREACHED();
    return std::string();
  }

  static bool FindIdByName(const std::string& name, T* id) {
    for (const Map& m : kMap) {
      if (m.name && m.name == name) {
        *id = m.id;
        return true;
      }
    }
    return false;
  }

 private:
  struct Map {
    const T id;
    const char* const name;
  };

  static const Map kMap[];
};

}  // namespace buffet

#endif  // BUFFET_COMMANDS_ENUM_TO_STRING_H_
