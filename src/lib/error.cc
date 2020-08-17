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

#include "error.h"
#include "value.h"
#include "type.h"

#include <sstream>

namespace park {

const Error Error::format(std::function<void(std::ostream &os)> func) {
    std::ostringstream os;
    func(os);
    return Error(os.str());
}

const Error Error::not_implemented(Fiber &fbr, const Value &v, std::string method_name) {
    return Error::format([&](std::ostream &os) {
        os << "Method " << method_name << " not implemented for value: ";
        v.repr(fbr, os);
    });
}

const Error Error::symbol_not_found(Fiber &fbr, std::string name) {
    return Error::format([&](std::ostream &os) {
        os << "Symbol not found: '" << name << "'";
    });
}

const Error
Error::operator_not_defined_for_argument_types(Fiber &fbr, const Value &callable, const Type &lhs, const Type &rhs) {
    return Error::format([&](std::ostream &os) {
        os << "Operator '";
        callable.repr(fbr, os);
        os << "' not defined for argument types lhs: ";
        lhs.repr(fbr, os);
        os << " and rhs: ";
        rhs.repr(fbr, os);
    });
}

const Error
Error::function_not_defined_for_argument_type(Fiber &fbr, size_t line, const Value &callable, const Value &self) {
    return Error::format([&](std::ostream &os) {
        os << "Function '";
        callable.repr(fbr, os);
        os << "' not defined for argument type: ";
        self.get_type().repr(fbr, os);
        os << " val: '";
        self.repr(fbr, os);
        os << " on line: " << line;
    });
}

const Error Error::key_not_found(Fiber &fbr, const Value &key) {
    return Error::format([&](std::ostream &os) {
        os << "key '";
        key.repr(fbr, os);
        os << "' not found";
    });
}

}
