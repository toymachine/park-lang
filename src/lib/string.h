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

#ifndef __STRING2_H
#define __STRING2_H

#include <memory>

#include "value.h"

#include <boost/asio.hpp>

namespace park {

    class String : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<String> create(Fiber &fbr, const std::string &from_str);
        static gc::ref<String> create_shared(Fiber &fbr, const std::string &from_str);

        virtual const char *begin() const = 0;
        virtual const char *end() const = 0;

        virtual const char *data() const = 0;
        virtual size_t size() const = 0;

        virtual boost::asio::const_buffer buffer() const = 0;

        static gc::ref<String> concat(Fiber &fbr, const String &lhs, const String &rhs);
    };

}

#endif