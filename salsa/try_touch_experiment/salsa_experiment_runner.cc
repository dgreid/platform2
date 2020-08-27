// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "salsa/try_touch_experiment/salsa_experiment_runner.h"

using base::StringPrintf;
using std::string;

bool SalsaExperimentRunner::LoadExperiment(string const& exp_string) {
  string decoded_string = Decode(exp_string);
  if (decoded_string.empty())
    return false;

  exp_ = Experiment(decoded_string);
  return exp_.valid();
}

string SalsaExperimentRunner::Decode(string const& exp_string) const {
  // Hex encoded strings always have an even length
  if (exp_string.length() % 2 != 0)
    return "";

  // Decode the string from hex, any non-hex characters invalidate it
  string decoded_string = "";
  for (string::const_iterator it = exp_string.begin(); it != exp_string.end();
       ++it) {
    char c1 = *it;
    char c2 = *++it;

    if (base::IsHexDigit(c1) && base::IsHexDigit(c2)) {
      int val1 = base::HexDigitToInt(c1);
      int val2 = base::HexDigitToInt(c2);
      char converted_char = static_cast<char>(val1 * 16 + val2);

      // After decoding, these should be the only characters in the string
      if (base::IsAsciiAlpha(converted_char) ||
          base::IsAsciiDigit(converted_char) || converted_char == '+' ||
          converted_char == ',' || converted_char == ':' ||
          converted_char == '-' || converted_char == ' ' ||
          converted_char == '.') {
        decoded_string.push_back(converted_char);
      } else {
        return "";
      }
    } else {
      return "";
    }
  }

  return decoded_string;
}

void SalsaExperimentRunner::EndCurses() {
  endwin();
}

void SalsaExperimentRunner::StartCurses() {
  initscr();
  cbreak();
  noecho();
  refresh();
  atexit(SalsaExperimentRunner::EndCurses);
  keypad(stdscr, TRUE);
}

void SalsaExperimentRunner::run() const {
  int current_treatment = -1;
  bool success = false;
  int key_press = '0';

  string treatment_list = "";
  for (int i = 0; i < exp_.Size(); i++)
    treatment_list = StringPrintf("%s  %d  ", treatment_list.c_str(), i);

  SalsaExperimentRunner::StartCurses();
  WINDOW* win = newwin(23, 59, 0, 0);
  while (key_press != 'q') {
    wclear(win);
    int selected_treatment = -1;
    if (key_press >= '0' && key_press - '0' < exp_.Size())
      selected_treatment = key_press - '0';
    else if (key_press == KEY_RIGHT || key_press == KEY_UP)
      selected_treatment = current_treatment + 1;
    else if (key_press == KEY_LEFT || key_press == KEY_DOWN)
      selected_treatment = current_treatment - 1;

    if (selected_treatment >= 0 && selected_treatment < exp_.Size()) {
      current_treatment = selected_treatment;
      success = exp_.ApplyTreatment(current_treatment);
    }

    wborder(win, '|', '|', '-', '-', ' ', ' ', ' ', ' ');
    mvwprintw(win, 1, 15, "  _____       _           ");
    mvwprintw(win, 2, 15, " / ____|     | |          ");
    mvwprintw(win, 3, 15, "| (___   __ _| |___  __ _ ");
    mvwprintw(win, 4, 15, " \\___ \\ / _` | / __|/ _` |");
    mvwprintw(win, 5, 15, " ____) | (_| | \\__ \\ (_| |");
    mvwprintw(win, 6, 15, "|_____/ \\__,_|_|___/\\__,_|");

    mvwprintw(win, 9, 2, "Selected Treatment: %s", treatment_list.c_str());
    if (success) {
      mvwprintw(win, 8, 23 + current_treatment * 5, "###");
      mvwprintw(win, 9, 22 + current_treatment * 5, "#");
      mvwprintw(win, 9, 26 + current_treatment * 5, "#");
      mvwprintw(win, 10, 23 + current_treatment * 5, "###");
    } else {
      mvwprintw(win, 10, 2,
                "There was an error applying a treatment."
                "Try again.",
                current_treatment);
    }

    mvwprintw(win, 12, 2, "Commands:");
    mvwprintw(win, 13, 6, "Arrow keys  -- Change selected treatment");
    mvwprintw(win, 14, 6, "Number keys -- Jump to a treatment");
    mvwprintw(win, 15, 6, "q           -- Quit and restore your old settings");

    mvwprintw(win, 17, 12, "Thank you for your participation!");

    mvwprintw(win, 19, 7, "Note: Treatments are ordered randomly, so there");
    mvwprintw(win, 20, 7, "is no special significance to their labels.");

    wrefresh(win);

    key_press = getch();
  }

  if (!exp_.Reset()) {
    wclear(win);
    wprintw(win, "WARNING! Some of your setting may not have been reset to ");
    wprintw(win, "their original values.  If you experience bad touchpad ");
    wprintw(win, "behavior, you can restore them manually by logging out ");
    wprintw(win, "and logging back in.  Sorry for the inconvenience.");
    wrefresh(win);
  }

  delwin(win);
}
