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

#include "boolean.h"
#include "type.h"
#include "visitor.h"
#include "frame.h"

namespace park {

    class BooleanImpl : public ValueImpl<Boolean, BooleanImpl> {

    private:
        //the single 2 global instances of the actual boolean values
        static gc::ref<BooleanImpl> B_TRUE;
        static gc::ref<BooleanImpl> B_FALSE;

        static gc::ref<Value> EQUALS;
        static gc::ref<Value> NOT;

    public:
        bool v_;

        explicit BooleanImpl(bool v) : v_(v) {}

        static gc::ref<BooleanImpl> from_bool(bool v) {
            if (v) {
                return B_TRUE;
            } else {
                return B_FALSE;
            }
        }

        const Value &accept(Fiber &fbr, Visitor &visitor) const override {
            return visitor.visit(fbr, *this);
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Boolean");

            B_TRUE = runtime.create_root<BooleanImpl>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<BooleanImpl>(allocator, true);
            });
            B_FALSE = runtime.create_root<BooleanImpl>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<BooleanImpl>(allocator, false);
            });

            EQUALS = runtime.builtin("equals");
            runtime.register_method(EQUALS, *TYPE, *TYPE, _equals);

            NOT = runtime.builtin("not");
            runtime.register_method(NOT, *TYPE, _not);
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            if (v_) {
                out << "true";
            } else {
                out << "false";
            }
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  

        bool to_bool(Fiber &fbr) const override {
            return v_;
        }

        /*
        const Value &accept(Fiber &fbr, Visitor &visitor) const override {
            return visitor.visit(fbr, *this);
        }

        */

        static int64_t _equals(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<BooleanImpl> self;
            gc::ref<BooleanImpl> other;

            return frame.check().
               binary_dispatch(*EQUALS, *TYPE, *TYPE).
               argument_count(2).
               argument<BooleanImpl>(1, self).
               argument<BooleanImpl>(2, other).
               result<bool>([&]() {
                    return self->v_ == other->v_;
               });
        }

        static int64_t _not(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<BooleanImpl> self;

            return frame.check().
               single_dispatch(*NOT, *TYPE).
               argument_count(1).
               argument<BooleanImpl>(1, self).
               result<bool>([&]() {
                    return !self->v_;
               });
        }

        const size_t map_key_hash(Fiber &fbr) const override {
            return std::hash<bool>()(v_);
        }

        const bool map_key_equals(Fiber &fbr, const Value &other) const override {
            /*
            if(!Boolean::isinstance(other)) {
                return false;
            }
            else {
                return static_cast<const BooleanImpl &>(other).v == v;
            }
            */
            throw std::runtime_error("TODO boolean map equals");
        }
    };

    gc::ref<BooleanImpl> BooleanImpl::B_TRUE;
    gc::ref<BooleanImpl> BooleanImpl::B_FALSE;

    gc::ref<Value> BooleanImpl::EQUALS;
    gc::ref<Value> BooleanImpl::NOT;

    void Boolean::init(Runtime &runtime) {
        BooleanImpl::init(runtime);
    }

    const gc::ref<Boolean> Boolean::create(bool v) {
        return BooleanImpl::from_bool(v);
    }

    const Type &Boolean::type() {
        return *BooleanImpl::TYPE;
    }



}
