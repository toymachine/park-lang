/*
 * Copyright 2020 Henk Punt
 *
 * This file is part of Park.
 *
 * Park is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * Park is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Park. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __INTERN 
#define __INTERN

#include <numeric>
#include <random>

#include <boost/bimap.hpp>

class Interns
{
public:
    using bimap_t = boost::bimap<std::string,size_t>;


private:
    bimap_t interned_;

    size_t next_id_ {0};

public:
    bimap_t::left_map &left() {
        return interned_.left;
    }

    size_t intern(const std::string &s) {

        auto found = interned_.left.find(s);
        if (found != interned_.left.end()) {
            return found->second;
        } else {
            auto new_id = ++next_id_;
            interned_.left.insert({s, new_id});
            return new_id;
        }
    }
};


#endif
