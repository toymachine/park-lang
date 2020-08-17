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

#include "struct.h"
#include "builtin.h"
#include "string.h"
#include "keyword.h"
#include "type.h"
#include "map.h"
#include "closure.h"
#include "runtime.h"
#include "compiler.h"

#include <unordered_map>

namespace park {

    static gc::ref<Value> GET;
    static gc::ref<Value> METHOD;
    static gc::ref<Value> IMPLEMENT;

    int64_t _constructor(Fiber &fbr, const AST::Apply &apply);
    int64_t _implement(Fiber &fbr, const AST::Apply &apply);

    using slot_map_t = std::unordered_map<gc::ref<Keyword>, size_t>; //keyword -> slot

    class StructTypeImpl : public SharedValueImpl<Type, StructTypeImpl> {
    private:
    
        const std::string name_;
        const slot_map_t slot_map_;

    public:
        StructTypeImpl(const std::string &name, slot_map_t &slot_map) 
            : name_(name), slot_map_(std::move(slot_map))
        {
        }

        const Type &get_type() const override {
            return *this;
        }

        std::string name() const override
        {
            return name_;
        }

        size_t size() const
        {
            return slot_map_.size();
        }

        std::optional<size_t> slot(gc::ref<Keyword> keyword) const {
            auto found = slot_map_.find(keyword);
            if (found != slot_map_.end()) {
                return found->second;
            }

            return std::nullopt;
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            throw std::runtime_error("TODO walk struct type");
        }  

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<" << name_ << " #slots: " << slot_map_.size() << ">";
        }

        virtual MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override {
            //TODO check dispatch
            return _constructor;
        }
    };

// TODO kwargs for struct constructor e.g. Aap(piet="klaas", bla=10)
// TODO every value gets a pointer to meta including type?
    class StructImpl : public ValueImpl<Struct, StructImpl> {
    private:
        gc::ref<StructTypeImpl> type_;


        const size_t size_;

    alignas(16)
        gc::ref<Value> slots_[];

    public:
        StructImpl(gc::ref<StructTypeImpl> type, size_t size) : type_(type), size_(size) {}

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            accept(type_);
            for(auto i = 0; i < size_; i++) {
                accept(slots_[i]);
            }
        }

        virtual const Type &get_type() const override {
            return *type_;
        }

        virtual std::optional<gc::ref<Value>> get(Fiber &fbr, gc::ref<Keyword> keyword) const override {
            if(auto slot = type_->slot(keyword)) {
                return slots_[*slot];
            }
            else {
                return std::nullopt;
            }
        }

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<struct " << type_->name() << ">";
        }

        const gc::ref<Value> *begin() const {
            return &slots_[0];
        }

        const gc::ref<Value> *end() const {
            return &slots_[size_];
        }


        static int64_t _method(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);
    
            gc::ref<Keyword> name;

            return frame.check().
                static_dispatch(*METHOD).
                argument_count(1).
                argument<Keyword>(1, name).
                result<Value>([&]() {
                    return  gc::make_shared_ref<BuiltinSingleDispatch>(fbr.allocator(), name->to_string(fbr));
                });                   
        }

        static void init(Runtime &runtime) {

            METHOD = runtime.create_builtin<BuiltinStaticDispatch>("method", _method);
            IMPLEMENT = runtime.create_builtin<BuiltinStaticDispatch>("implement", _implement);

            GET = runtime.builtin("get");
        }

    };

    int64_t _get(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<StructImpl> self;
        gc::ref<Keyword> key;
        gc::ref<Value> default_value;

        return frame.check().
            argument_count(2, 3).
            argument<StructImpl>(1, self).
            single_dispatch(*GET, self->get_type()).
            argument<Keyword>(2, key).
            optional_argument<Value>(3, default_value). 
            result<Value>([&]() {
                if (auto found = self->get(fbr, key)) {
                    return *found;
                } 
                else if(default_value) {
                    return default_value;
                }
                throw Error::key_not_found(fbr, *key);
            });
    }


    int64_t _constructor(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<StructTypeImpl> type;

        return frame.check().
//                static_dispatch(*STRUCT). TODO how to check dispatch here?
            argument<StructTypeImpl>(0, type).
            result<Value>([&]() {
                assert(frame.argument_count() == type->size()); //TODO proper error
                auto instance = gc::make_ref_fam<StructImpl, gc::ref<Value>>(fbr.allocator(), type->size(), type, type->size());
                size_t i = 1;
                for(auto &slot : *instance) {
                    const_cast<gc::ref<Value> &>(slot) = frame.argument<gc::ref<Value>>(i);
                    i++;
                }
                return instance;
            });                               
    }

    int64_t _implement(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<BuiltinSingleDispatch> method;
        gc::ref<Type> type;
        gc::ref<Closure> closure;

        return frame.check().
            static_dispatch(*IMPLEMENT).
            argument_count(3).
            argument<BuiltinSingleDispatch>(1, method).
            argument<Type>(2, type).
            argument<Closure>(3, closure).
            result<Value>([&]() {
                auto &runtime = Runtime::from_fbr(fbr);
                auto impl = runtime.compiler().code(closure->function());
                std::lock_guard<std::mutex> guard(runtime.lock);//TODO remove when implement is toplevel constuct
                runtime.register_method(method, *type, impl);
                return closure;
            });                   
    }

    void Struct::init(Runtime &runtime) {
        StructImpl::init(runtime);
    }

    gc::ref<Type> Struct::create(Fiber &fbr, const AST::Struct &struct_)
    {
        //TODO assert that runtime is in init phase
        slot_map_t fields;
        int slot_i = 0;
        for (auto &field : *struct_.fields_) {
            auto define = gc::ref_dynamic_cast<AST::Define>(field);
            assert(define->data_);
            fields.insert({Keyword::create(fbr, define->symbol_->name_), slot_i});
            slot_i += 1;
        }

        auto struct_type = gc::make_shared_ref<StructTypeImpl>(fbr.allocator(), struct_.name_->name_, fields);
        auto &runtime = Runtime::from_fbr(fbr);
        runtime.register_method(GET, *struct_type, _get);
        return struct_type;
    }
}
