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

#include <random>
#include "runtime.h"
#include "builtin.h"
#include "mod_random.h"

namespace park {
namespace random {

    std::default_random_engine generator;

    gc::ref<BuiltinStaticDispatch> RANDINT;

    void init(Runtime &runtime) {
        RANDINT = runtime.create_builtin<BuiltinStaticDispatch>("randint",
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

            Frame frame(fbr, apply);

            int64_t start;
            int64_t end;

            return frame.check().
               static_dispatch(*RANDINT).
               argument_count(2).
               argument<int64_t>(1, start).
               argument<int64_t>(2, end).
               result<int64_t>([&]() {
                   //std::cerr << "start " << start << " end " << end << std::endl;
                   assert(start >= 0);
                   assert(end >= 0); 
                   assert(end >= start);
                   //TODO check params also in release
                   std::uniform_int_distribution<int> distribution(start,end);
                   return distribution(generator);
               });
        });
    }

}
}

