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

#include "type.h"
#include "builtin.h"

namespace park {

    class TypeImpl : public SharedValueImpl<Type, TypeImpl> {
    private:
        const std::string name_;

    public:

        TypeImpl(std::string name) : name_(name) {}

        std::string name() const override
        {
            return name_;
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<type '" << name_ << "'>";
        }


        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Type");
        }

    };

    void Type::init(Runtime &runtime) {
        TypeImpl::init(runtime);
    }

    gc::ref<Type> Type::create(gc::allocator_t &allocator, std::string name) {
        return gc::make_shared_ref<TypeImpl>(allocator, name);
    }

}