// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019-2021 Kenji Rikitake, JJ1BDX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// ConfigParser

#include "ConfigParser.h"

// Split input string into a vector of multiple strings.

std::vector<std::string> ConfigParser::split_delimiter(const std::string str) {
  std::vector<std::string> elements(0);
  std::string token;
  token.clear();
  for (char c : str) {
    // Delimiters: '&' or ','
    if ((c == '&') || (c == ',')) {
      if (!token.empty()) {
        elements.push_back(token);
      }
      token.clear();
    } else {
      token += c;
    }
  }
  if (!token.empty()) {
    elements.push_back(token);
  }
  return elements;
}

// Parse input string as a pair of "key=value".
// Delimiter is "=".
// Only the leftmost "=" is parsed.
// If no "=" is contained, null value is set for the key.

ConfigParser::pair_type ConfigParser::split_equal_sign(const std::string str) {
  std::string token;
  std::string key;
  std::string value;
  token.clear();
  key.clear();
  value.clear();
  bool found_equal = false;
  for (char c : str) {
    if ((c == '=') && (!found_equal)) {
      key = token;
      found_equal = true;
      token.clear();
    } else {
      token += c;
    }
  }
  if (!token.empty()) {
    if (!found_equal) {
      key = token;
    } else {
      value = token;
    }
  }
  return std::make_pair(key, value);
}

// Parse "foo=x,bar,baz=10" style configuration parameter
// into a map (map_type).

void ConfigParser::parse_config_string(std::string text,
                                       ConfigParser::map_type &output) {
  std::vector<std::string> tokens = split_delimiter(text);
  for (std::string str : tokens) {
    pair_type element = split_equal_sign(str);
    if (element.first.size() > 0) {
      output.insert(element);
    }
  }
}
