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

#ifndef __ERROR_H
#define __ERROR_H

#include <exception>
#include <string>
#include <functional>

namespace park {

    class Value;
    class Type;
    class Fiber;

    class Error : public std::runtime_error {
    private:
        static const Error format(std::function<void(std::ostream &os)>);

    public:
        explicit Error(const std::string &msg) : std::runtime_error(msg) {};

        static const Error not_implemented(Fiber &fbr, const Value &v, std::string method_name);

        static const Error symbol_not_found(Fiber &fbr, std::string name);

        static const Error
        operator_not_defined_for_argument_types(Fiber &fbr, const Value &callable, const Type &lhs, const Type &rhs);

        static const Error
        function_not_defined_for_argument_type(Fiber &fbr, size_t line, const Value &callable, const Value &self);

        static const Error key_not_found(Fiber &fbr, const Value &key);
    };

}


#endif
