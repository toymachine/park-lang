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

#ifndef __STACK_H
#define __STACK_H

#include <type_traits>
#include <vector>
#include <csignal>

#include "value.h"
#include "integer.h"
#include "boolean.h"

#include <boost/container/static_vector.hpp>

namespace park {

    class Stack {
    public:

    private:
        using chunk_ptr = std::unique_ptr<char, gc::chunk_deleter>;
        chunk_ptr chunk_;

        value_t * end_ = nullptr; //stack ptr
        value_t * begin_ = nullptr; //start of stack
        value_t * cap_ = nullptr; //end of stack array e.g. not the stack ptr

        static const size_t INIT_CAP = 32;
        static const size_t MAX_CAP = 4096;
    public:

        Stack()
        {
        }

        size_t capacity() const noexcept
        {
            return cap_ - begin_;
        }

        void ensure_capacity(size_t n);

        template<typename F>
        void each(F f) {
            for(auto cur = begin_ ; cur < end_; cur++) {
                f(*cur);
            }
        }

        bool empty() const noexcept
        {
            return end_ == begin_;
        }

        size_t size() const noexcept
        {
            return end_ - begin_;
        }

        void clear() noexcept
        {
            end_ = nullptr;
            begin_ = nullptr;
            cap_ = nullptr;
            chunk_ = nullptr;
        }

        const value_t &back() const
        {
            assert(!empty());
            return *(end_ - 1);
        }

        void pop_back() {
            pop(1);
        }

        inline void pop(size_t n) {
            assert((end_ - n) >= begin_);
            end_ -= n;
            assert(end_ >= begin_ && end_ < cap_);
        }

        size_t base(size_t argument_count) const
        {
            assert((end_ - argument_count - 1) >= begin_);
            return (end_ - argument_count - 1) - begin_;
        }

        const value_t &callable(size_t base) const {
            return *(begin_ + base);
        }

        const value_t &local(size_t base, size_t local_index) const {
            return *(begin_ + base + local_index);
        }

        const value_t &argument(size_t base, size_t argument_index) const {
            return *(begin_ + base + argument_index);
        }

        void push_local(size_t base, size_t local_index) {
            push_back(local(base, local_index));
        }

        void set_local(size_t base, size_t local_index) {
            *(begin_ + base + local_index) = back();
        }

        void init_locals(size_t local_count)
        {
            assert(end_>= begin_ && end_ <= cap_);
            if((end_ + local_count) > cap_) {
                ensure_capacity(local_count);
            }           
            std::fill_n(end_, local_count, value_t());
            end_ += local_count;
            assert(end_ >= begin_ && end_ <= cap_);
        }

        void recur(size_t argument_count, size_t local_count) 
        {
            std::copy(end_ - argument_count, end_,
                      end_ - (2 * argument_count) - local_count);

            end_ -= argument_count;
        }

        template<typename T>
        void push(T value) {
            push_back(value::to_value_t<T>(value));
        }

        void push_back (const value_t& val)
        {
            assert(end_>= begin_ && end_ <= cap_);
            if(end_ == cap_) {
                ensure_capacity(1);
            }
            *end_ = val;
            end_++;
            assert(end_ >= begin_ && end_ <= cap_);
        }

        void pop_frame(size_t base)
        {
            pop(size() - base);
        }

        template<typename T>
        T pop_cast(Fiber &fbr) {
            assert(!empty());
            T result = value::cast<T>(fbr, back());
            pop_back();
            return result;
        }


       
    };
}

#endif