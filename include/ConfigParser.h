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

#include <map>
#include <string>
#include <vector>

// Class ConfigParser

class ConfigParser {
public:
  // public types
  using map_type = std::map<std::string, std::string>;
  using pair_type = std::pair<std::string, std::string>;
  // Parse "foo=x,bar,baz=10" style configuration parameter
  // into a map (map_type).
  void parse_config_string(std::string text, map_type &output);

  // Default constructors
  ConfigParser() = default;
  ConfigParser(const ConfigParser &) = delete;
  ConfigParser operator=(const ConfigParser &) = delete;

private:
  // Split input string into a vector of multiple strings.
  std::vector<std::string> split_delimiter(const std::string str);
  // Parse input string as a pair of "key=value".
  // Delimiter is "=".
  // Only the leftmost "=" is parsed.
  // If no "=" is contained, null value is set for the key.
  pair_type split_equal_sign(const std::string str);
};

/*

// Test code example

#include "ConfigParser.h"
#include <iostream>

int main() {
    std::string text{"alpha=100,beta,gamma=xyz"};
    // std::string text{"="};

    ConfigParser cp;
    ConfigParser::map_type parsed_map;

    cp.parse_config_string(text, parsed_map);
    for (ConfigParser::pair_type pair: parsed_map) {
        std::cout << pair.first << " => " << pair.second << "\n";
    }
}

*/
