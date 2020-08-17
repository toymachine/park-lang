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

#include "value.h"

#include "boolean.h"
#include "integer.h"
#include "error.h"
#include "string.h"

namespace park {

    namespace value {

        const Value &from_value_t(Fiber &fbr, const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::IVALUE: {
                    return *Integer::create(fbr, value.ivalue);
                }
                case value_t::kind_t::BVALUE: {
                    return *Boolean::create(value.bvalue);
                }
                default: {
                    throw std::runtime_error("TODO from_value_t");
                }
            }
        }

        gc::ref<Value> ref_from_value_t(Fiber &fbr, const value_t &value) {  
            switch (value.kind) {
                case value_t::kind_t::IVALUE: {
                    return Integer::create(fbr, value.ivalue);
                }
                case value_t::kind_t::BVALUE: {
                    return Boolean::create(value.bvalue);
                }
                default: {
                    throw std::runtime_error("TODO ref_from_value_t");
                }
            }
        }

        const Type &value_type(const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::RVALUE: {
                    return value.rvalue->get_type();
                }
                case value_t::kind_t::IVALUE: {
                    return Integer::type();
                }
                case value_t::kind_t::BVALUE: {
                    return Boolean::type();
                }
                default: {
                    throw std::runtime_error("unexpected kind in value_type");
                }
            }
        }
    }
}

/*
namespace gc {
    void dump(const collectable *r)
    {
        auto s = dynamic_cast<const park::String *>(r);
        if(s != nullptr) {
            std::cerr << "str of size: " << s->size() << "'" << std::string(s->data(), std::min(s->size(), 10ul)) << "'";
        }
        else {
            std::cerr << typeid(*r).name();
        }
    }

}
*/