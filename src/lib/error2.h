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

#ifndef __ERROR2_H
#define __ERROR2_H

#include "value.h"

#include <string.h>

namespace park {

    class Error2 : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<Error2> create(Fiber &fbr, const std::string &message);

        static bool is_error(const Value &value);

    };
}

#endif