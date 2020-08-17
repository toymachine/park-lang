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

#ifndef __NAMESPACE
#define __NAMESPACE

#include "value.h"
#include "runtime.h"

#include <optional>

namespace park {

    namespace AST {
        struct Module;
    }

    class Namespace : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<Namespace> create(Fiber &fbr, gc::ref<AST::Module> module, const std::string &name);

        virtual std::string name() const = 0;

        virtual gc::ref<AST::Module> module() const = 0;

        virtual void define(size_t namei, gc::ref<Value> value) = 0;

        virtual std::optional<gc::ref<Value>> find(size_t namei) const = 0;

        virtual void set_initialized(bool initialized) = 0;

        virtual bool is_initialized() const = 0;
    };
}

#endif