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

#include "vector.h"
#include "type.h"
#include "visitor.h"
#include "frame.h"

#include <algorithm>

namespace park {

    static gc::ref<Value> CONJ;
    static gc::ref<Value> LENGTH;
    static gc::ref<Value> GET;
    static gc::ref<Value> NOT;
    static gc::ref<Value> FIRST;
    static gc::ref<Value> NEXT;
    static gc::ref<Value> ADD;

    class Array : public Value
    {
    };

    class ArrayImpl : public ValueImpl<Array, ArrayImpl> 
    {
    private:
        const size_t size_;

        alignas(16)
        gc::ref<Value> arr_[];

    public:
        static gc::ref<ArrayImpl> EMPTY;

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Array");
            EMPTY = runtime.create_root<ArrayImpl>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<ArrayImpl>(allocator);
            });
        }

        explicit ArrayImpl()
                : size_(0) {}

        /* copy constructor */
        explicit ArrayImpl(const gc::ref<Value> arr[], size_t size)
            : size_(size)
        {
            assert(size_ > 0 && size_ <= 32);
            std::copy_n(arr, size, arr_);
#ifndef NDEBUG		
            for(auto i = 0; i < size_; i++) {
                assert(arr_[i]);
            }
#endif
        }

        /* copy and append constructor */
        explicit ArrayImpl(const gc::ref<Value> arr[], size_t size, gc::ref<Value> val)
            : size_(size + 1)
        {
            assert(size_ > 0 && size_ <= 32);
            assert(val);
            std::copy_n(arr, size, arr_);
            arr_[size] = val;
#ifndef NDEBUG		
            for(auto i = 0; i < size_; i++) {
                assert(arr_[i]);
            }
#endif
        }

        /* single value constructor */
        explicit ArrayImpl(gc::ref<Value> val)
            : size_(1)
        {
            assert(size_ > 0 && size_ <= 32);            
            assert(val);
            arr_[0] = val;
        }

        /* 2 value constructor */
        explicit ArrayImpl(gc::ref<Value> val1, gc::ref<Value> val2)
            : size_(2)
        {
            assert(size_ > 0 && size_ <= 32);
            assert(val1);
            assert(val2);
            arr_[0] = val1;
            arr_[1] = val2;
        }

        static gc::ref<ArrayImpl> create(Fiber &fbr, const ArrayImpl &other)
        {
            return gc::make_ref_fam<ArrayImpl, gc::ref<Value>>(fbr.allocator(), other.size_, other.arr_, other.size_);
        }

        static gc::ref<ArrayImpl> create(Fiber &fbr, gc::ref<Value> val)
        {
            return gc::make_ref_fam<ArrayImpl, gc::ref<Value>>(fbr.allocator(), 1, val);
        }

        static gc::ref<ArrayImpl> create(Fiber &fbr, gc::ref<Value> val1, gc::ref<Value> val2)
        {
            return gc::make_ref_fam<ArrayImpl, gc::ref<Value>>(fbr.allocator(), 2, val1, val2);
        }

        gc::ref<ArrayImpl> append(Fiber &fbr, gc::ref<Value> val) const {
           return gc::make_ref_fam<ArrayImpl, gc::ref<Value>>(fbr.allocator(), size_ + 1, arr_, size_, val);
        }

        void set(size_t idx, gc::ref<Value> v) {
            assert(idx >= 0 && idx < size_);
            arr_[idx] = v;
        }

        gc::ref<Value> get(size_t idx) const {
            assert(idx >= 0 && idx < size_);
            return arr_[idx];
        }
        
        size_t size() const {
            return size_;
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
            assert(size_ >= 0 && size_ <= 32);
            for(auto i = 0; i < size_; i++) {
                assert(arr_[i]);
                accept(arr_[i]);
            }
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "[";
            out << " TODO array repr";
            out << "]";
        }

    };

    gc::ref<ArrayImpl> ArrayImpl::EMPTY;

    class VectorImpl : public ValueImpl<Vector, VectorImpl>  {

        const int cnt_;
        const int shift_;

        const gc::ref<ArrayImpl> root_;
        const gc::ref<ArrayImpl> tail_;

        static gc::ref<VectorImpl> EMPTY;

        static int64_t _conj(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;
            gc::ref<Value> val;

            return frame.check().
               single_dispatch(*CONJ, *TYPE).
               argument_count(2).
               argument<VectorImpl>(1, self).
               argument<Value>(2, val).
               result<Value>([&]() {
                   return self->conj(fbr, val);
               });
        }

        static int64_t _get(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;
            int64_t idx;

            return frame.check().
                single_dispatch(*GET, *TYPE).
                argument_count(2).
                argument<VectorImpl>(1, self).
                argument<int64_t>(2, idx).
                result<Value>([&]() {
                    return self->nth(idx);
                });
        }

        static int64_t _length(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;

            return frame.check().
               single_dispatch(*LENGTH, *TYPE).
               argument_count(1).
               argument<VectorImpl>(1, self).
               result<int64_t>([&]() {
                   return self->cnt_;
               });
        }

        static int64_t _not(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;

            return frame.check().
               single_dispatch(*NOT, *TYPE).
               argument_count(1).
               argument<VectorImpl>(1, self).
               result<bool>([&]() {
                   return self->cnt_ == 0;
               });
        }

        static int64_t _first(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;

            return frame.check().
               single_dispatch(*FIRST, *TYPE).
               argument_count(1).
               argument<VectorImpl>(1, self). 
               result<Value>([&]() {
                   return self->nth(0);
               });
        }

        static int64_t _add(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorImpl> self;
            gc::ref<VectorImpl> other;

            //TODO this impl is silly and slow
            return frame.check().
                binary_dispatch(*ADD, *TYPE, *TYPE).
                argument_count(2).
                argument<VectorImpl>(1, self). 
                argument<VectorImpl>(2, other). 
                result<Value>([&]() {
                    auto new_vector = self;
                    for (size_t i = 0; i < other->size(); i++) {
                        new_vector = gc::ref_dynamic_cast<VectorImpl>(new_vector->conj(fbr, other->nth(i)));
                    }
                    return new_vector;
                });
        }

public:
        static int64_t _next(Fiber &fbr, const AST::Apply &apply);
        static int64_t _add_vector_iterator(Fiber &fbr, const AST::Apply &apply);

        virtual size_t size() const override {
            return cnt_;
        }

         gc::ref<Value> nth(size_t i) const override {
            if (i < cnt_) {
                if (i >= tailoff()) {
                    return tail_->get(i & 0x01f);
                }
                auto arr = root_;
                for (auto level = shift_; level > 0; level -= 5) {
                    arr = gc::ref_dynamic_cast<ArrayImpl>(arr->get((i >> level) & 0x01f));
                }
                return arr->get(i & 0x01f);
            }
            throw std::runtime_error("index out of bound");
        }

private:
        bool to_bool(Fiber &fbr) const override {
            return cnt_ != 0;
        }

        const size_t tailoff() const {
            return cnt_ - tail_->size();
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "[";
            for(auto i = 0; i < std::min(130UL, size()); i++) {
                nth(i)->repr(fbr, out);
                out << ", ";
            }
            if(size() > 130UL) {
                out << "...";
            }
            out << "]";
        }

        gc::ref<ArrayImpl> pushTail(Fiber &fbr, int level,
            gc::ref<ArrayImpl> arr,
            gc::ref<ArrayImpl> tailNode,
            gc::ref<ArrayImpl> &expansion) const {
            assert(arr);
            assert(tailNode);
            gc::ref<ArrayImpl> newchild;
            if (level == 0) {
                newchild = tailNode;
            } else {
                assert(arr->size() > 0);
                assert((&arr->get(arr->size() - 1)->get_type()) == ArrayImpl::TYPE.get());
                newchild = pushTail(
                        fbr,
                        level - 5,
                        ArrayImpl::create(fbr, *gc::ref_dynamic_cast<ArrayImpl>(arr->get(arr->size() - 1))),
                        tailNode, expansion
                );
                if (!expansion) {
                    auto ret = ArrayImpl::create(fbr, *arr);
                    ret.mutate()->set(arr->size() - 1, gc::ref_cast<Value>(newchild));
                    return ret;
                } else {
                    newchild = expansion;
                }
            }
            assert(newchild);
            //expansion
            if (arr->size() == 32) {
                expansion = ArrayImpl::create(fbr, gc::ref_cast<Value>(newchild));
                return arr;
            } else {
                expansion = nullptr;
                return arr->append(fbr, gc::ref_cast<Value>(newchild));
            }

        }

        gc::ref<Vector> conj(Fiber &fbr, gc::ref<Value> val) const override {
            if (tail_->size() < 32) {
                return gc::make_ref<VectorImpl>(
                    fbr.allocator(), cnt_ + 1, shift_, root_, tail_->append(fbr, val));
            } 
            else {
                gc::ref<ArrayImpl> expansion;
                auto newroot = pushTail(fbr, shift_ - 5, root_, tail_, expansion);
                int newshift = shift_;
                if (expansion) {
                    newroot = ArrayImpl::create(fbr, gc::ref_cast<Value>(newroot), gc::ref_cast<Value>(expansion));
                    newshift += 5;
                }
                return gc::make_ref<VectorImpl>(
                    fbr.allocator(), cnt_ + 1, newshift, newroot, ArrayImpl::create(fbr, val));
            }
        }

    public:
        VectorImpl(int cnt, int shift,
                   gc::ref<ArrayImpl> root,
                   gc::ref<ArrayImpl> tail)
                : cnt_(cnt), shift_(shift), root_(root), tail_(tail) {}

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
            accept(root_);
            accept(tail_);
        }

        const Value &accept(Fiber &fbr, Visitor &visitor) const override {
            return visitor.visit(fbr, *this);
        }

        static gc::ref<Vector> create() {
            return EMPTY;
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Vector");

            EMPTY = runtime.create_root<VectorImpl>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<VectorImpl>(allocator, 0, 5, ArrayImpl::EMPTY, ArrayImpl::EMPTY);
            });

            runtime.register_method(CONJ, *TYPE, _conj);
            runtime.register_method(GET, *TYPE, _get);
            runtime.register_method(LENGTH, *TYPE, _length);
            runtime.register_method(NOT, *TYPE, _not);
            runtime.register_method(FIRST, *TYPE, _first);
            runtime.register_method(NEXT, *TYPE, _next);
            runtime.register_method(ADD, *TYPE, *TYPE, _add);

        }
    };

    class VectorIterator : public Value {
    };

    class VectorIteratorImpl : public ValueImpl<VectorIterator, VectorIteratorImpl> {

    private:
        gc::ref<VectorImpl> v_;
        const size_t start_;

    public:
        VectorIteratorImpl(gc::ref<VectorImpl> v, size_t start)
            : v_(v), start_(start) {}

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
            accept(v_);
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "(vector_iterator)";
        }

        bool to_bool(Fiber &fbr) const override {
            return (v_->size() - start_) > 0;
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("VectorIterator");

            runtime.register_method(FIRST, *TYPE, _first);
            runtime.register_method(NEXT, *TYPE, _next);

            /*
            BuiltinSingleDispatch::LENGTH->register_method(*type, _length);
            */
        }

        size_t size() const {
            return v_->size() - start_;
        }

        gc::ref<Value> nth(int i) const {
            return v_->nth(start_ + i);
        }

        static int64_t _first(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorIteratorImpl> self;

            return frame.check().
               single_dispatch(*FIRST, *TYPE).
               argument_count(1).
               argument<VectorIteratorImpl>(1, self). 
               result<Value>([&]() {
                   return self->nth(0);
               });
        }

        static int64_t _next(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<VectorIteratorImpl> self;

            return frame.check().
                single_dispatch(*NEXT, *TYPE).
                argument_count(1).
                argument<VectorIteratorImpl>(1, self). 
                result<Value>([&]() {
                    return gc::make_ref<VectorIteratorImpl>(fbr.allocator(), self->v_, self->start_ + 1);
                });
        }

    };

    gc::ref<VectorImpl> VectorImpl::EMPTY;


    void Vector::init(Runtime &runtime) {
        CONJ = runtime.builtin("conj");
        GET = runtime.builtin("get");
        LENGTH = runtime.builtin("length");
        NOT = runtime.builtin("not");
        FIRST = runtime.builtin("first");
        NEXT = runtime.builtin("next");
        ADD = runtime.builtin("add");

        ArrayImpl::init(runtime);
        VectorImpl::init(runtime);
        VectorIteratorImpl::init(runtime);

        runtime.register_method(ADD, *VectorImpl::TYPE, *VectorIteratorImpl::TYPE, VectorImpl::_add_vector_iterator);

    }

    gc::ref<Vector> Vector::create(Fiber &fbr) {
        return VectorImpl::create();
    }

    int64_t VectorImpl::_next(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<VectorImpl> self;

        return frame.check().
            single_dispatch(*NEXT, *TYPE).
            argument_count(1).
            argument<VectorImpl>(1, self). 
            result<Value>([&]() {
                return gc::make_ref<VectorIteratorImpl>(fbr.allocator(), self, 1);
            });
    }

    int64_t VectorImpl::_add_vector_iterator(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<VectorImpl> self;
        gc::ref<VectorIteratorImpl> other;

        //TODO this impl is silly and slow
        return frame.check().
            binary_dispatch(*ADD, *TYPE, *VectorIteratorImpl::TYPE).
            argument_count(2).
            argument<VectorImpl>(1, self). 
            argument<VectorIteratorImpl>(2, other). 
            result<Value>([&]() {
                auto new_vector = self;
                for (size_t i = 0; i < other->size(); i++) {
                    new_vector = gc::ref_dynamic_cast<VectorImpl>(new_vector->conj(fbr, other->nth(i)));
                }
                return new_vector;
            });
    }

}



