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

#include "atom.h"
#include "builtin.h"
#include "visitor.h"

namespace park {

std::array<std::mutex, 1024> object_locks;

std::mutex &object_lock(void *ptr) {
    //TODO how good is the hash wrt high bits always being 00?
    auto idx = std::hash<void *>{}(ptr) % 1024;
    return object_locks[idx];
}

//TODO lock not always needed, e.g. when write barrier is off, we could use atomic
class AtomImpl : public SharedValueImpl<Atom, AtomImpl>
{
private:
    gc::ref<Value> v_;

    static gc::ref<Value> DEREF;
    static gc::ref<Value> COMPARE_AND_SET;
    static gc::ref<Value> ATOM;

public:

    AtomImpl(gc::ref<Value> v)
        : v_(v)
    {
        assert(is_shared_ref(v));
    }

    void repr(Fiber &fbr, std::ostream &out) const override {
        out << "<atom>";
    }

    bool compare_and_set(Fiber &fbr, gc::ref<Value> old_val, gc::ref<Value> new_val) {
        assert(gc::is_shared_ref(old_val));
        std::lock_guard guard(object_lock(this));
        if(v_ == old_val) {
            gc::ref_write<Value>(fbr.allocator(), v_, new_val);
            return true;
        }
        else {
            return false;
        }
    }

    gc::ref<Value> deref() const {
        auto &lock = object_lock(static_cast<void *>(const_cast<AtomImpl *>(this)));
        lock.lock();
        auto v = v_;
        lock.unlock();
        return v;
    }

    gc::ref<Value> value() const override {
        return deref();
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
        accept(deref());
    }

    const Value &accept(Fiber &fbr, Visitor &visitor) const override {
        return visitor.visit(fbr, *this);
    }

    static int64_t _deref(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<AtomImpl> self;

        return frame.check().
            single_dispatch(*DEREF, *TYPE).
            argument_count(1).
            argument<AtomImpl>(1, self).
            result<Value>([&]() {
                //TODO race between deref and gc? e.g. deref copies the ref, but gc might mis it before it gets written to the stack here:
                return self->deref();
            });
 
    }

    static int64_t _atom(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<Value> initial;

        return frame.check().
            static_dispatch(*ATOM).
            argument_count(1).
            argument<Value>(1, initial).
            result<Value>([&]() {
                return Atom::create(fbr.allocator(), initial);
            });

    }

    static int64_t _compare_and_set(Fiber &fbr, const AST::Apply &apply) {

        Frame frame(fbr, apply);

        gc::ref<AtomImpl> self;
        gc::ref<Value> old_val;
        gc::ref<Value> new_val;

        return frame.check().
            single_dispatch(*COMPARE_AND_SET, *TYPE).
            argument_count(3).
            argument<AtomImpl>(1, self).
            argument<Value>(2, old_val).
            argument<Value>(3, new_val).
            result<bool>([&]() {
                return self.mutate()->compare_and_set(fbr, old_val, new_val);
        });
    }

    static void init(Runtime &runtime) {
        TYPE = runtime.create_type("Atom");

        DEREF = runtime.builtin("deref");
        runtime.register_method(DEREF, *TYPE, _deref);

        ATOM = runtime.create_builtin<BuiltinStaticDispatch>("atom", AtomImpl::_atom);

        COMPARE_AND_SET = runtime.builtin("compare_and_set");
        runtime.register_method(COMPARE_AND_SET, *TYPE, _compare_and_set);

    }

};

gc::ref<Value> AtomImpl::DEREF;
gc::ref<Value> AtomImpl::COMPARE_AND_SET;
gc::ref<Value> AtomImpl::ATOM;

gc::ref<Atom> Atom::create(gc::allocator_t &allocator, gc::ref<Value> initial)
{
    gc::make_shared(allocator, initial);
    return gc::make_shared_ref<AtomImpl>(allocator, initial);
}

void Atom::init(Runtime &runtime) {
    AtomImpl::init(runtime);
}

}
