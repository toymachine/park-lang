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

#ifndef __LIST_H
#define __LIST_H

#include "value.h"

namespace park {

    class List : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<List> create(Fiber &fbr);
        
        virtual gc::ref<List> conj(Fiber &fbr, gc::ref<Value> item) const = 0;

    };
}

#endif