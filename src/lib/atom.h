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

#ifndef __ATOM_H
#define __ATOM_H

#include "value.h"
#include "fiber.h"

namespace park {

    class Atom : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<Atom> create(gc::allocator_t &allocator, gc::ref<Value> initial);

        virtual gc::ref<Value> value() const = 0;

    };

}
#endif