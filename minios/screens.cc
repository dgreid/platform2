// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace screens {

const char kScreens[] = "etc/screens";

// Colors.
const char kMenuBlack[] = "0x202124";
const char kMenuBlue[] = "0x8AB4F8";
const char kMenuGrey[] = "0x3F4042";
const char kMenuButtonFrameGrey[] = "0x9AA0A6";

namespace {
constexpr char kConsole0[] = "dev/pts/0";

// Dimensions.
// TODO(vyshu): Get this from frecon.
constexpr int kFreconScalingFactor = 1;
// TODO(vyshu): Get this from frecon print-resolution.
constexpr int kCanvasSize = 1080;
constexpr int kMonospaceGlyphHeight = 20;
constexpr int kMonospaceGlyphWidth = 10;
constexpr int kDefaultMessageWidth = 720;
constexpr int kButtonHeight = 32;

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

void Screens::ClearMainArea() {
  constexpr int kFooterHeight = 142;
  if (!ShowBox(0, -kFooterHeight / 2, kCanvasSize + 100,
               (kCanvasSize - kFooterHeight), kMenuBlack))
    LOG(WARNING) << "Could not clear main area.";
}

void Screens::ClearScreen() {
  if (!ShowBox(0, 0, kCanvasSize + 100, kCanvasSize, kMenuBlack))
    LOG(WARNING) << "Could not clear screen.";
}

void Screens::ShowButton(const std::string& message_token,
                         int offset_y,
                         bool is_selected,
                         int inner_width) {
  const int btn_padding = 32;  // Left and right padding.
  int left_padding_x = (-kCanvasSize / 2) + (btn_padding / 2);
  const int offset_x = left_padding_x + (btn_padding / 2) + (inner_width / 2);
  int right_padding_x = offset_x + (btn_padding / 2) + (inner_width / 2);
  // Clear previous state.
  if (!ShowBox(offset_x, offset_y, (btn_padding * 2 + inner_width),
               kButtonHeight, kMenuBlack)) {
    LOG(WARNING) << "Could not clear button area.";
  }

  if (right_to_left_) {
    std::swap(left_padding_x, right_padding_x);
  }

  if (is_selected) {
    ShowImage(screens_path_.Append("btn_bg_left_focused.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right_focused.png"), right_padding_x,
              offset_y);

    ShowBox(offset_x, offset_y, inner_width, kButtonHeight, kMenuBlue);

    ShowMessage(message_token + "_focused", offset_x, offset_y);
  } else {
    ShowImage(screens_path_.Append("btn_bg_left.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right.png"), right_padding_x,
              offset_y);
    ShowMessage(message_token, offset_x, offset_y);
    ShowBox(offset_x, offset_y - (kButtonHeight / 2) + 1, inner_width, 1,
            kMenuButtonFrameGrey);
    ShowBox(offset_x, offset_y + (kButtonHeight / 2), inner_width, 1,
            kMenuButtonFrameGrey);
  }
}

void Screens::ShowStepper(const std::vector<std::string>& steps) {
  // The icon real size is 24x24, but it occupies a 36x36 block. Use 36 here for
  // simplicity.
  constexpr int kIconSize = 36;
  constexpr int kSeparatorLength = 46;
  constexpr int kPadding = 6;

  int stepper_x = (-kCanvasSize / 2) + (kIconSize / 2);
  constexpr int kStepperXStep = kIconSize + kSeparatorLength + (kPadding * 2);
  constexpr int kStepperY = 144 - (kCanvasSize / 2);
  int separator_x =
      (-kCanvasSize / 2) + kIconSize + kPadding + (kSeparatorLength / 2);

  for (const auto& step : steps) {
    base::FilePath stepper_image = screens_path_.Append("ic_" + step + ".png");
    if (!base::PathExists(stepper_image)) {
      stepper_image = screens_path_.Append("ic_done.png");
      // TODO(vyshu): Create a new generic icon to be used instead of done.
      LOG(WARNING) << "Stepper icon " << stepper_image
                   << " not found. Defaulting to the done icon.";
      if (!base::PathExists(stepper_image)) {
        LOG(ERROR) << "Could not find stepper icon done. Cannot show stepper.";
        return;
      }
    }
    ShowImage(stepper_image, stepper_x, kStepperY);
    stepper_x += kStepperXStep;
  }

  for (int i = 0; i < steps.size() - 1; ++i) {
    ShowBox(separator_x, kStepperY, kSeparatorLength, 1, kMenuGrey);
    separator_x += kStepperXStep;
  }
}

void Screens::ShowLanguageMenu(bool is_selected) {
  constexpr int kOffsetY = -kCanvasSize / 2 + 40;
  constexpr int kBgX = -kCanvasSize / 2 + 145;
  constexpr int kGlobeX = -kCanvasSize / 2 + 20;
  constexpr int kArrowX = -kCanvasSize / 2 + 268;
  // TODO(vyshu): Find declaration of language_width.
  constexpr int kLanguageWidth = 57;
  constexpr int kTextX = -kCanvasSize / 2 + 40 + kLanguageWidth / 2;

  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);

  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("language_folded", kTextX, kOffsetY);
}

void Screens::ShowFooter() {
  constexpr int kQrCodeSize = 86;
  constexpr int kQrCodeX = (-kCanvasSize / 2) + (kQrCodeSize / 2);
  constexpr int kQrCodeY = (kCanvasSize / 2) - (kQrCodeSize / 2) - 56;

  constexpr int kSeparatorX = 410 - (kCanvasSize / 2);
  constexpr int kSeparatorY = kQrCodeY;
  constexpr int kFooterLineHeight = 18;

  constexpr int kFooterY = (kCanvasSize / 2) - kQrCodeSize + 9 - 56;
  constexpr int kFooterLeftX =
      kQrCodeX + (kQrCodeSize / 2) + 16 + (kDefaultMessageWidth / 2);
  constexpr int kFooterRightX = kSeparatorX + 32 + (kDefaultMessageWidth / 2);

  ShowMessage("footer_left_1", kFooterLeftX, kFooterY);
  ShowMessage("footer_left_2", kFooterLeftX,
              kFooterY + kFooterLineHeight * 2 + 14);
  ShowMessage("footer_left_3", kFooterLeftX,
              kFooterY + kFooterLineHeight * 3 + 14);

  constexpr int kNavButtonHeight = 24;
  constexpr int kNavButtonY = (kCanvasSize / 2) - (kNavButtonHeight / 2) - 56;
  int nav_btn_x = kSeparatorX + 32;
  // Navigation key icons.
  const std::string footer_type = is_detachable_ ? "tablet" : "clamshell";
  const std::string nav_key_enter =
      is_detachable_ ? "button_power" : "key_enter";
  const std::string nav_key_up = is_detachable_ ? "button_volume_up" : "key_up";
  const std::string nav_key_down =
      is_detachable_ ? "button_volume_down" : "key_down";

  constexpr int kUpDownIconWidth = 24;
  constexpr int kIconPadding = 8;
  const int enter_icon_width = is_detachable_ ? 40 : 66;

  ShowMessage("footer_right_1_" + footer_type, kFooterRightX, kFooterY);
  ShowMessage("footer_right_2_" + footer_type, kFooterRightX,
              kFooterY + kFooterLineHeight + 8);

  nav_btn_x += enter_icon_width / 2;
  ShowImage(screens_path_.Append("nav-" + nav_key_enter + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += enter_icon_width / 2 + kIconPadding + kUpDownIconWidth / 2;
  ShowImage(screens_path_.Append("nav-" + nav_key_up + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += kIconPadding + kUpDownIconWidth;
  ShowImage(screens_path_.Append("nav-" + nav_key_down + ".png"), nav_btn_x,
            kNavButtonY);

  ShowImage(screens_path_.Append("qr_code.png"), kQrCodeX, kQrCodeY);
  // TODO(vyshu): Get hardware from "crossystem hwid".
  std::string hwid = "CHROMEBOOK";
  int hwid_len = hwid.size();
  int hwid_x = kQrCodeX + (kQrCodeSize / 2) + 16 + 5;
  const int hwid_y = kFooterY + kFooterLineHeight;

  if (right_to_left_) {
    hwid_x = -hwid_x - kMonospaceGlyphWidth * (hwid_len - 2);
  }

  ShowText(hwid, hwid_x, hwid_y, "grey");
  ShowBox(kSeparatorX, kSeparatorY, 1, kQrCodeSize, kMenuGrey);
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
