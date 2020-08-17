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

#ifndef __STRUCT_H
#define __STRUCT_H

#include "runtime.h"
#include "ast.h"
#include "keyword.h"

namespace park {

    class Struct : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<Type> create(Fiber &fbr, const AST::Struct &struct_);

        virtual std::optional<gc::ref<Value>> get(Fiber &fbr, gc::ref<Keyword> keyword) const  = 0;

    };

}

#endif
