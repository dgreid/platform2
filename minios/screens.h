// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_H_
#define MINIOS_SCREENS_H_

#include <string>

#include <base/files/file.h>
#include <base/strings/string_split.h>
#include <gtest/gtest_prod.h>

namespace screens {

extern const char kScreens[];

class Screens {
 public:
  Screens() = default;
  virtual ~Screens() = default;
  // Not copyable or movable.
  Screens(const Screens&) = delete;
  Screens& operator=(const Screens&) = delete;

  // Loads token constants for screen placement, checks whether locale is read
  // from right to left and whether device is detachable.
  bool Init();

  // Show dynamic text using pre-rendered glyphs. Colors 'white', 'grey' and
  // 'black'. Returns true on success.
  bool ShowText(const std::string& text,
                int glyph_offset_h,
                int glyph_offset_v,
                const std::string& color);

  // Uses frecon to show image given a full file path. Returns true on success.
  bool ShowImage(const base::FilePath& image_name, int offset_x, int offset_y);

  // Uses frecon to show a box. Color should be given as a hex string. Returns
  // true on success.
  bool ShowBox(int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color);

  // Shows message image at the given offset. All message tokens are in
  // `/etc/screens`. Falls back to English if chosen locale is not available.
  bool ShowMessage(const std::string& message_token,
                   int offset_x,
                   int offset_y);

  // Shows title and uses title offsets.
  void Instructions(const std::string& message_token);

  // Shows the title and corresponding description using offsets from
  // `constants` to place.
  void InstructionsWithTitle(const std::string& message_token);

  // Override the root directory for testing. Default is '/'. Updates screens
  // path.
  void SetRootForTest(const std::string& test_root);

  // Override the current locale without using the language menu.
  void SetLanguageForTest(const std::string& test_locale);

  // Override whether current language is marked as being read from right to
  // left. Does not change language.
  void SetLocaleRtlForTest(bool is_rtl);

 private:
  FRIEND_TEST(ScreensTest, ReadDimension);
  FRIEND_TEST(ScreensTest, GetDimension);

  // Read dimension constants for current locale into memory. Must be updated
  // every time the language changes.
  void ReadDimensionConstants();

  // Gets the height or width of an image given the token. Returns -1 on error.
  int GetDimension(const std::string& token);

  // Whether the locale is read from right to left.
  bool right_to_left_{false};

  // Key value pairs that store token name and measurements.
  base::StringPairs image_dimensions_;

  // Default root directory.
  base::FilePath root_{"/"};

  // Default screens path, set in init.
  base::FilePath screens_path_;

  // Default and fall back locale directory.
  std::string locale_{"en-US"};
};

}  // namespace screens

#endif  // MINIOS_SCREENS_H_