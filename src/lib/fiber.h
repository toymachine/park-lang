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

#ifndef __FIBER_H
#define __FIBER_H

#include <boost/intrusive/list.hpp>

#include "value.h"
#include "runtime.h"
#include "stack.h"
#include "pool.h"

namespace park {

    using namespace boost::intrusive;

    using  FiberList = list<Fiber>;

    class Fiber : public Value, public list_base_hook<> {
        friend class Runtime;

    private:
        FiberList *color_ = nullptr;

    protected:
        gc::allocator_t *allocator_ = nullptr;

    public:
        using stack_t = Stack;

        stack_t stack;

        Fiber();
        ~Fiber();

        static void init(Runtime &runtime);

        static gc::ref<Fiber> create(gc::allocator_t &allocator, Runtime &runtime, bool is_main);

        gc::allocator_t &allocator() const {
            assert(allocator_);
            return *allocator_;
        }

        FiberList *color() const {
            assert(color_);
            return color_;
        }

        void switch_color(FiberList *color) {
            if(color_) {
                color_->erase(FiberList::s_iterator_to(*this));
                color_ = nullptr;
            }
            if(color) {
                color_ = color;
                color_->push_front(*this);
            }
        }

        virtual void attach(gc::allocator_t &allocator) = 0;
        virtual void detach(gc::allocator_t &allocator) = 0;

        virtual void enqueue(const std::function<int()> f) = 0; 

        virtual void resume_async(std::function<void(Fiber &fbr)> f, int64_t ret_code) = 0;
        virtual void resume_sync(std::function<void(Fiber &fbr)> f, int64_t ret_code) = 0;

        void roots(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept);

    };

}


#endif
