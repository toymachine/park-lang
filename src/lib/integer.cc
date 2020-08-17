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


#include "integer.h"
#include "frame.h"
#include "type.h"
#include "visitor.h"

namespace park {

class IntegerImpl :  public ValueImpl<Integer, IntegerImpl>
{
	int64_t v;

    static gc::ref<Value> EQUALS;
    static gc::ref<Value> ADD;
    static gc::ref<Value> SUBTRACT;
    static gc::ref<Value> MULTIPLY;
    static gc::ref<Value> LESSTHAN;
    static gc::ref<Value> GREATERTHAN;
    static gc::ref<Value> MODULO;

public:
	IntegerImpl(uint64_t v) : v(v) {}

    const Value &accept(Fiber &fbr, Visitor &visitor) const override {
        return visitor.visit(fbr, *this);
    }

    const size_t map_key_hash(Fiber &fbr) const override
    {
		return std::hash<int64_t>()(v);
    }

    const bool map_key_equals(Fiber &fbr, const Value &other) const override
    {
    	return v == other.to_index(fbr);
    }

    void repr(Fiber &fbr, std::ostream &out) const override {
        out << v;
    }

    int64_t
    to_index(Fiber &fbr, int64_t start = 0, int64_t end = std::numeric_limits<int64_t>::max()) const override {
        assert(v >= 0);
        return v;
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  

    static int64_t _equals(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        const IntegerImpl *a;
        const IntegerImpl *b;
        int64_t res;
        if (frame.binary<IntegerImpl, IntegerImpl>(a, b, *EQUALS, res)) {
            frame.stack.push<bool>(a->v == b->v);
        }
        return res;
    }

    static int64_t _equals_int64_t(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        int64_t a, b, res;
        if (frame.binary<int64_t, int64_t>(a, b, *EQUALS, res)) {
            frame.stack.push<bool>(a == b);
        }
        return res;
    }

    static int64_t _add(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        const IntegerImpl *a;
        const IntegerImpl *b;
        int64_t res;
        if (frame.binary<IntegerImpl, IntegerImpl>
                (a, b, *ADD, res)) {
            frame.stack.push<int64_t>(a->v + b->v);
        }
        return res;
    }

    static int64_t _add_int64_t(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        int64_t a, b, res;
        if (frame.binary<int64_t, int64_t>
                (a, b, *ADD, res)) {
            frame.stack.push<int64_t>(a + b);
        }
        return res;
    }

    static int64_t _subtract_boxed_boxed(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        if(apply.argument_count() != 2) {
            return frame.bad_dispatch();
        }

        auto &c = frame.argument_(0);

        if(!(c.is_ref() && c.ref() == SUBTRACT.get())) {
            return frame.bad_dispatch();
        }

        auto &a = frame.argument_(1);
        auto &b = frame.argument_(2);

        if(!(a.is_ref() && &a.ref()->get_type() == IntegerImpl::TYPE.get())) {
            return frame.bad_dispatch();
        }

        if(!(b.is_ref() && &b.ref()->get_type() == IntegerImpl::TYPE.get())) {
            return frame.bad_dispatch();
        }

        auto a_ = static_cast<const IntegerImpl *>(a.ref());
        auto b_ = static_cast<const IntegerImpl *>(b.ref());

        frame.stack.pop(3);
        frame.stack.push<int64_t>(a_->v - b_->v);

        return 0;
    }

    static int64_t _subtract_boxed_int64(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        if(apply.argument_count() != 2) {
            return frame.bad_dispatch();
        }

        auto &c = frame.argument_(0);

        if(!(c.is_ref() && c.ref() == SUBTRACT.get())) {
            return frame.bad_dispatch();
        }

        auto &a = frame.argument_(1);
        auto &b = frame.argument_(2);

        if(!(a.is_ref() && &a.ref()->get_type() == IntegerImpl::TYPE.get())) {
            return frame.bad_dispatch();
        }

        if(!b.is_int64()) {
            return frame.bad_dispatch();
        }

        auto a_ = static_cast<const IntegerImpl *>(a.ref());

        frame.stack.pop(3);
        frame.stack.push<int64_t>(a_->v - b.int64());

        return 0;
    }

    static int64_t _subtract_int64_boxed(Fiber &fbr, const AST::Apply &apply) {
        throw std::runtime_error("TODO _subtract_int64_boxed");
    }

    static int64_t _subtract_int64_t(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        auto [ok, res, a, b] = 
            frame.check_binary<int64_t, int64_t>(*SUBTRACT);
        if(ok) {
            frame.stack.push<int64_t>(a - b);
        }

        return res;

    }

    static int64_t _multiply(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        auto [ok, res, a, b] = 
            frame.check_binary_boxed<IntegerImpl, IntegerImpl>(*MULTIPLY);
        if(ok) {
            frame.stack.push<int64_t>(a.v * b.v);
        }
        return res;
    }

    static int64_t _multiply_int64_t(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        auto [ok, res, a, b] = 
            frame.check_binary<int64_t, int64_t>(*MULTIPLY);
        if(ok) {
            frame.stack.push<int64_t>(a * b);
        }

        return res;
    }

    static int64_t _lessthan(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        const IntegerImpl *a;
        const IntegerImpl *b;
        int64_t res;
        if (frame.binary<IntegerImpl, IntegerImpl>
                (a, b, *LESSTHAN, res)) {
            frame.stack.push<bool>(a->v < b->v);
        }
        return res;
    }

    static int64_t _greaterthan(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        const IntegerImpl *a;
        const IntegerImpl *b;
        int64_t res;
        if (frame.binary<IntegerImpl, IntegerImpl>
                (a, b, *GREATERTHAN, res)) {
            frame.stack.push<bool>(a->v > b->v);
        }
        return res;
    }

    static int64_t _modulo(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        const IntegerImpl *a;
        const IntegerImpl *b;
        int64_t res;
        if (frame.binary<IntegerImpl, IntegerImpl>
                (a, b, *MODULO, res)) {
            frame.stack.push<int64_t>(a->v % b->v);
        }
        return res;
    }

	static void init(Runtime &runtime) {
        TYPE = runtime.create_type("Integer");

        EQUALS = runtime.builtin("equals");
        runtime.register_method(EQUALS, *TYPE, *TYPE, _equals);
        runtime.register_method(EQUALS, value_t::kind_t::IVALUE, value_t::kind_t::IVALUE, _equals_int64_t);

        ADD = runtime.builtin("add");
        runtime.register_method(ADD, *TYPE, *TYPE, _add);
        runtime.register_method(ADD, value_t::kind_t::IVALUE, value_t::kind_t::IVALUE, _add_int64_t);

        SUBTRACT = runtime.builtin("subtract");
        runtime.register_method(SUBTRACT, *TYPE, *TYPE, _subtract_boxed_boxed);
        runtime.register_method(SUBTRACT, *TYPE, value_t::kind_t::IVALUE, _subtract_boxed_int64);
        runtime.register_method(SUBTRACT, value_t::kind_t::IVALUE, *TYPE, _subtract_int64_boxed);
        runtime.register_method(SUBTRACT, value_t::kind_t::IVALUE, value_t::kind_t::IVALUE, _subtract_int64_t);

        MULTIPLY = runtime.builtin("multiply");
        runtime.register_method(MULTIPLY, *TYPE, *TYPE, _multiply);
        runtime.register_method(MULTIPLY, value_t::kind_t::IVALUE, value_t::kind_t::IVALUE, _multiply_int64_t);

        LESSTHAN = runtime.builtin("lt");
        runtime.register_method(LESSTHAN, *TYPE, *TYPE, _lessthan);

        GREATERTHAN = runtime.builtin("gt");
        runtime.register_method(GREATERTHAN, *TYPE, *TYPE, _greaterthan);

        MODULO = runtime.builtin("mod");
        runtime.register_method(MODULO, *TYPE, *TYPE, _modulo);

	}

};

gc::ref<Value> IntegerImpl::EQUALS;
gc::ref<Value> IntegerImpl::ADD;
gc::ref<Value> IntegerImpl::SUBTRACT;
gc::ref<Value> IntegerImpl::MULTIPLY;
gc::ref<Value> IntegerImpl::LESSTHAN;
gc::ref<Value> IntegerImpl::GREATERTHAN;
gc::ref<Value> IntegerImpl::MODULO;

void Integer::init(Runtime &runtime)
{
	IntegerImpl::init(runtime);
}

const Type &Integer::type() {
    return *IntegerImpl::TYPE;
}

gc::ref<Integer> Integer::create(Fiber &fbr, int64_t i) {
	return gc::make_ref<IntegerImpl>(fbr.allocator(), i);
}

}