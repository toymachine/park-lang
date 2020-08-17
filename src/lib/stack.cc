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

#include "stack.h"
#include "runtime.h"

namespace park {

    static_assert(sizeof(value_t) == 16);

    void Stack::ensure_capacity(size_t n)
    {
        //ensure cap for at least n more items
        auto current_size = size();
        auto next_capacity = capacity();
        if(!chunk_) {
            next_capacity = INIT_CAP;
        }
        while(next_capacity < (current_size + n) && next_capacity <= MAX_CAP) {
            next_capacity *= 2;
        }
        if(next_capacity > MAX_CAP) {
            throw std::bad_alloc();
        }

        //TODO get mem from local allocator
        auto &allocator = Runtime::current_allocator();

        //std::cerr << "next cap: " << next_capacity << std::endl;

        chunk_ptr new_chunk(reinterpret_cast<char *>(allocator.alloc_local(next_capacity * sizeof(value_t))));

        value_t *new_begin = reinterpret_cast<value_t *>(new_chunk.get());

        std::copy(begin_, end_, new_begin);

        begin_ = new_begin;
        cap_  = begin_ + next_capacity;
        end_ = begin_ + current_size;

        chunk_ = std::move(new_chunk);
    }

}