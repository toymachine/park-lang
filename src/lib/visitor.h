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

#ifndef __VISITOR_H
#define __VISITOR_H

#include "value.h"
#include "string.h"
#include "integer.h"
#include "vector.h"
#include "map.h"
#include "boolean.h"
#include "atom.h"

namespace park {

    class Visitor {
    public:
        virtual const Value &visit(Fiber &fbr, const Map &v) = 0;

        virtual const Value &visit(Fiber &fbr, const Vector &v) = 0;

        virtual const Value &visit(Fiber &fbr, const Integer &v) = 0;

        virtual const Value &visit(Fiber &fbr, const String &v) = 0;

        virtual const Value &visit(Fiber &fbr, const Boolean &v) = 0;

        virtual const Value &visit(Fiber &fbr, const Atom &v) = 0;

    };

}

#endif