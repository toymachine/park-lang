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

#ifndef __PACK_H
#define __PACK_H

#include "runtime.h"

namespace park {

    namespace pack {

        void init(Runtime &runtime);

        void pack(Fiber &fbr, const Value &value, std::ostream &outs);
        void pack_node(Fiber &fbr, const Value &value, std::ostream &outs);

        size_t read_map_header(std::istream &ins);
        size_t read_array_header(std::istream &ins);
        std::string read_string(std::istream &ins);
        int64_t read_integer(std::istream &ins);
        bool read_bool(std::istream &ins);

        gc::ref<Value> unpack(Fiber &fbr, std::istream &ins);

    }

}

#endif