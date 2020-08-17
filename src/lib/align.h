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

#ifndef __ALIGN_H
#define __ALIGN_H


namespace park {

    template<int N>
    inline std::size_t align(std::size_t sz) {
        if (sz % N != 0) {
            return sz + (N - sz % N);
        } else {
            return sz;
        }
    }

    template<int N>
    inline intptr_t align(intptr_t sz) {
        if (sz % N != 0) {
            return sz + (N - sz % N);
        } else {
            return sz;
        }
    }

}

#endif