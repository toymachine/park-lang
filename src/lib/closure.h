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

#ifndef __CLOSURE_H
#define __CLOSURE_H

#include "value.h"

#include <optional>

namespace park {

    namespace AST {
        struct Function;
    }

    class Closure : public Value {
    public:

        virtual const AST::Function &function() const = 0;

        virtual std::optional<gc::ref<Value>> lookup(size_t namei) const = 0;

        virtual void set(size_t index, gc::ref<Value> value) = 0;

        static void init(Runtime &runtime);

        static gc::ref<Closure> create(Fiber &fbr, gc::ref<AST::Function> function, size_t size);
        
        static bool isinstance(const Value &value);

    };

}


#endif