// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

namespace screens {

const char kScreens[] = "etc/screens";

namespace {
constexpr char kConsole0[] = "dev/pts/0";

// TODO(vyshu): Get this from frecon.
constexpr int kFreconScalingFactor = 1;
// TODO(vyshu): Get this from frecon print-resolution.
constexpr int kCanvasSize = 1080;
constexpr int kMonospaceGlyphHeight = 20;
constexpr int kMonospaceGlyphWidth = 10;
constexpr int kDefaultMessageWidth = 720;

constexpr int kNewLineChar = 10;
}  // namespace

bool Screens::Init() {
  // TODO(vyshu): Query system to check to set rtl and detachable values.

  screens_path_ = root_.Append(screens::kScreens);
  // TODO(vyshu): Change constants.sh and lang_constants.sh to simple text file.
  ReadDimensionConstants();
  return true;
}

bool Screens::ShowText(const std::string& text,
                       int glyph_offset_h,
                       int glyph_offset_v,
                       const std::string& color) {
  base::FilePath glyph_dir = screens_path_.Append("glyphs").Append(color);
  const int kTextStart = glyph_offset_h;

  for (const auto& chr : text) {
    int char_num = static_cast<int>(chr);
    base::FilePath chr_file_path =
        glyph_dir.Append(base::NumberToString(char_num) + ".png");
    if (char_num == kNewLineChar) {
      glyph_offset_v += kMonospaceGlyphHeight;
      glyph_offset_h = kTextStart;
    } else {
      int offset_rtl = right_to_left_ ? -glyph_offset_h : glyph_offset_h;
      if (!ShowImage(chr_file_path, offset_rtl, glyph_offset_v)) {
        LOG(ERROR) << "Failed to show image " << chr_file_path << " for text "
                   << text;
        return false;
      }
      glyph_offset_h += kMonospaceGlyphWidth;
    }
  }
  return true;
}

bool Screens::ShowImage(const base::FilePath& image_name,
                        int offset_x,
                        int offset_y) {
  if (right_to_left_)
    offset_x = -offset_x;
  std::string command = base::StringPrintf(
      "\033]image:file=%s;offset=%d,%d;scale=%d/a", image_name.value().c_str(),
      offset_x, offset_y, kFreconScalingFactor);
  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write " << image_name << "  to console.";
    return false;
  }

  return true;
}

bool Screens::ShowBox(int offset_x,
                      int offset_y,
                      int size_x,
                      int size_y,
                      const std::string& color) {
  size_x = std::max(size_x, 1);
  size_y = std::max(size_y, 1);
  if (right_to_left_)
    offset_x = -offset_x;

  std::string command = base::StringPrintf(
      "\033]box:color=%s;size=%d,%d;offset=%d,%d;scale=%d\a", color.c_str(),
      size_x, size_y, offset_x, offset_y, kFreconScalingFactor);

  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write show box command to console.";
    return false;
  }

  return true;
}

bool Screens::ShowMessage(const std::string& message_token,
                          int offset_x,
                          int offset_y) {
  // Determine the filename of the message resource. Fall back to en-US if
  // the localized version of the message is not available.
  base::FilePath message_file_path =
      screens_path_.Append(locale_).Append(message_token + ".png");
  if (!base::PathExists(message_file_path)) {
    if (locale_ == "en-US") {
      LOG(ERROR) << "Message " << message_token
                 << " not found in en-US. No fallback available.";
      return false;
    }
    LOG(WARNING) << "Could not find " << message_token << " in " << locale_
                 << " trying default locale en-US.";
    message_file_path =
        screens_path_.Append("en-US").Append(message_token + ".png");
    if (!base::PathExists(message_file_path)) {
      LOG(ERROR) << "Message " << message_token << " not found in path "
                 << message_file_path;
      return false;
    }
  }
  return ShowImage(message_file_path, offset_x, offset_y);
}

void Screens::Instructions(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);
  constexpr int kYOffset = (-kCanvasSize / 2) + 283;
  if (!ShowMessage(message_token, kXOffset, kYOffset))
    LOG(WARNING) << "Unable to show " << message_token;
}

void Screens::InstructionsWithTitle(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);

  int title_height = GetDimension("TITLE_" + message_token + "_HEIGHT");
  int desc_height = GetDimension("DESC_" + message_token + "_HEIGHT");
  if (title_height == -1 || desc_height == -1) {
    title_height = 40;
    desc_height = 40;
    LOG(WARNING) << "Unable to get constants for  " << message_token
                 << ". Defaulting to 40.";
  }
  const int title_y = (-kCanvasSize / 2) + 220 + (title_height / 2);
  const int desc_y = title_y + (title_height / 2) + 16 + (desc_height / 2);
  if (!ShowMessage("title_" + message_token, kXOffset, title_y))
    LOG(WARNING) << "Unable to show title " << message_token;
  if (!ShowMessage("desc_" + message_token, kXOffset, desc_y))
    LOG(WARNING) << "Unable to show description " << message_token;
}

void Screens::ReadDimensionConstants() {
  image_dimensions_.clear();
  base::FilePath path = screens_path_.Append(locale_).Append("constants.sh");
  std::string dimension_consts;
  if (!ReadFileToString(path, &dimension_consts)) {
    LOG(ERROR) << "Could not read constants.sh file for language " << locale_;
    return;
  }
  if (!base::SplitStringIntoKeyValuePairs(dimension_consts, '=', '\n',
                                          &image_dimensions_))
    LOG(WARNING) << "Unable to parse all dimension information for " << locale_;
}

int Screens::GetDimension(const std::string& token) {
  if (image_dimensions_.empty()) {
    LOG(ERROR) << "No dimensions available.";
    return -1;
  }

  // Find the dimension for the token.
  for (const auto& dimension : image_dimensions_) {
    if (dimension.first == token) {
      int image_dim = -1;
      if (!base::StringToInt(dimension.second, &image_dim)) {
        LOG(ERROR) << "Could not convert " << dimension.second
                   << " to a number.";
        return -1;

      } else {
        return image_dim;
      }
    }
  }
  return -1;
}

void Screens::SetRootForTest(const std::string& test_root) {
  root_ = base::FilePath(test_root);
}

void Screens::SetLanguageForTest(const std::string& test_locale) {
  locale_ = test_locale;
  // Reload locale dependent dimension constants.
  ReadDimensionConstants();
}

void Screens::SetLocaleRtlForTest(bool is_rtl) {
  right_to_left_ = is_rtl;
}

}  // namespace screens
