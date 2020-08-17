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

#ifndef __KEYWORD_H
#define __KEYWORD_H

#include "value.h"


namespace park {

    class Keyword : public Value {

    public:
        //needs lock! or runtime init, creates shared ref
    	static gc::ref<Keyword> create(Fiber &fbr, const std::string &name);

        static void init(Runtime &runtime);

        static const Type &type();
    };

}

#endif