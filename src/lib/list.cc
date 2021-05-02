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

#include "list.h"
#include "type.h"
#include "visitor.h"
#include "frame.h"
#include "builtin.h"

namespace park {

    class ListImpl : public ValueImpl<List, ListImpl> {

    private:
        gc::ref<Value> item_;
        gc::ref<List> tail_;

        static gc::ref<Value> FIRST;
        static gc::ref<Value> NEXT;


    public:
        static gc::ref<List> EMPTY;

        explicit ListImpl(gc::ref<Value> item, gc::ref<List> tail) : item_(item), tail_(tail) {}

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<List ";
            if(tail_) {
                item_->repr(fbr, out);
                tail_->repr(fbr, out);
            }
            out << ">";
        }

        bool to_bool(Fiber &fbr) const override {
            return tail_;
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            if(item_) {
                accept(item_);
            }
            if(tail_) {
                accept(tail_);
            }
        }

        gc::ref<List> conj(Fiber &fbr, gc::ref<Value> item) const override
        {
            return gc::make_ref<ListImpl>(fbr.allocator(), item, this);
        }

        static int64_t _first(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<ListImpl> self;

            return frame.check().
               single_dispatch(*FIRST, *TYPE).
               argument_count(1).
               argument<ListImpl>(1, self). 
               result<Value>([&]() {
                   if(self->tail_) {
                       return self->item_;
                   }
                   else {
                       throw std::runtime_error("TODO exc from first on LIST");
                   }
               });
        }

        static int64_t _next(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<ListImpl> self;

            return frame.check().
                single_dispatch(*NEXT, *TYPE).
                argument_count(1).
                argument<ListImpl>(1, self). 
                result<Value>([&]() {
                   if(self->tail_) {
                       return self->tail_;
                   }
                   else {
                       throw std::runtime_error("TODO exc from next on LIST");
                   }
                });

        }
        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("List");

            EMPTY = runtime.create_root<ListImpl>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<ListImpl>(allocator, nullptr, nullptr);
            });

            FIRST = runtime.builtin("first");
            runtime.register_method(FIRST, *TYPE, _first);

            NEXT = runtime.builtin("next");
            runtime.register_method(NEXT, *TYPE, _next);

        }

    };

    void List::init(Runtime &runtime) {
        ListImpl::init(runtime);
    }

    gc::ref<List> ListImpl::EMPTY;
    gc::ref<Value> ListImpl::FIRST;
    gc::ref<Value> ListImpl::NEXT;

    gc::ref<List> List::create(Fiber &fbr)
    {
        return ListImpl::EMPTY;
    }





}
