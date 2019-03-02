// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_UTIL_H_
#define INCLUDE_UTIL_H_

inline bool parse_dbl(const char *s, double &v) {
  char *endp;

  v = strtod(s, &endp);

  if (endp == s) {
    return false;
  }

  if (*endp == 'k') {
    v *= 1.0e3;
    endp++;
  } else if (*endp == 'M') {
    v *= 1.0e6;
    endp++;
  } else if (*endp == 'G') {
    v *= 1.0e9;
    endp++;
  }

  return (*endp == '\0');
}

#endif /* INCLUDE_UTIL_H_ */
