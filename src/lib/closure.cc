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


#include "closure.h"
#include "runtime.h"
#include "type.h"
#include "ast.h"
#include "builtin.h"
#include "compiler.h"

namespace park {

    class ClosureImpl : public ValueImpl<Closure, ClosureImpl> {

    private:
        gc::ref<AST::Function> function_;
        size_t size_; //num freevars

    alignas(16)
        gc::ref<Value> freevars_[];

    public:
        ClosureImpl(gc::ref<AST::Function> _function,
                    size_t size)
                : function_(_function), size_(size) {
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Closure");
        }

        const AST::Function &function() const override {
            return *function_;
        }

        MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override {
            return Runtime::from_fbr(fbr).compiler().code(*function_);
        }

        std::optional<gc::ref<Value>> lookup(size_t namei) const override {
            if(auto index = function_->freevar_index(namei)) {
                return freevars_[*index];
            }
            else {
                return std::nullopt;
            }
        }

        void set(size_t index, gc::ref<Value> value) override {
            freevars_[index] = value;
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<closure " << (void *) this << " of function: " << function_->name_ << ">";
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
            accept(function_);   
            for(auto i = 0; i < size_; i++) {
                accept(freevars_[i]);
            }
        }

        /*
        const bool to_bool(Fiber &fbr) const override {
            return true;
        }
        */

    };

    gc::ref<Closure> Closure::create(Fiber &fbr, gc::ref<AST::Function> function, size_t size) {
        return gc::make_ref_fam<ClosureImpl, gc::ref<Value>>(fbr.allocator(), size, function, size);
    }

    void Closure::init(Runtime &runtime) {
        ClosureImpl::init(runtime);
    }

    bool Closure::isinstance(const Value &value) {
        return &value.get_type() == ClosureImpl::TYPE.get();
    }



}

