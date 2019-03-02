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

/******************************************************************************

 Key-Value parser
(http://www.boost.org/doc/libs/1_47_0/libs/spirit/example/qi/key_value_sequence.cpp)

 Usage:

 int main()
 {
    namespace qi = boost::spirit::qi;

    std::string input("key1=value1,key2,key3=value3");
    std::string::iterator begin = input.begin();
    std::string::iterator end = input.end();

    parsekv::key_value_sequence<std::string::iterator> p;
    parsekv::pairs_type m;

    if (!qi::parse(begin, end, p, m)) {
        std::cout << "Parsing failed\n";
    }
    else
    {
        std::cout << "Parsing succeeded, found entries:\n";
        parsekv::pairs_type::iterator end = m.end();
        for (parsekv::pairs_type::iterator it = m.begin(); it != end; ++it) {
            std::cout << (*it).first;
            if (!(*it).second.empty()) {
                std::cout << "=" << (*it).second;
            }
            std::cout << std::endl;
        }
    }
    return 0;
 }

******************************************************************************/

#ifndef INCLUDE_PARSEKV_H_
#define INCLUDE_PARSEKV_H_

#include <boost/fusion/include/std_pair.hpp>
#include <boost/spirit/include/qi.hpp>
#include <iostream>
#include <map>

namespace parsekv {
namespace qi = boost::spirit::qi;

typedef std::map<std::string, std::string> pairs_type;

template <typename Iterator>
struct key_value_sequence : qi::grammar<Iterator, pairs_type()> {
  key_value_sequence() : key_value_sequence::base_type(query) {
    query = pair >> *((qi::lit(',') | '&') >> pair);
    pair = key >> -('=' >> value);
    key = qi::char_("a-zA-Z_") >> *qi::char_("a-zA-Z_0-9");
    value = +qi::char_("a-zA-Z_0-9.");
  }

  qi::rule<Iterator, pairs_type()> query;
  qi::rule<Iterator, std::pair<std::string, std::string>()> pair;
  qi::rule<Iterator, std::string()> key, value;
};
} // namespace parsekv

#endif /* INCLUDE_PARSEKV_H_ */
