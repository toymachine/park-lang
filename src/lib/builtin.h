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

#ifndef __BUILTIN_H
#define __BUILTIN_H

#include <map>

#include "value.h"
#include "frame.h"

namespace park {

    class BuiltinStaticDispatch;
    class BuiltinSingleDispatch;
    class BuiltinBinaryDispatch;

    class Builtin : public Value {
    public:
        static void init(Runtime &runtime);

        static gc::ref<BuiltinStaticDispatch> TYPEOF;
        static gc::ref<BuiltinStaticDispatch> PRINT;
        static gc::ref<BuiltinStaticDispatch> RANGE;
        static gc::ref<BuiltinStaticDispatch> ORD;
        static gc::ref<BuiltinStaticDispatch> CHR;
        static gc::ref<BuiltinStaticDispatch> SLURP;
        static gc::ref<BuiltinStaticDispatch> SPIT;

        static gc::ref<BuiltinSingleDispatch> CONTAINS;
        static gc::ref<BuiltinSingleDispatch> NOT;
        static gc::ref<BuiltinSingleDispatch> LENGTH;
        static gc::ref<BuiltinSingleDispatch> CONJ;
        static gc::ref<BuiltinSingleDispatch> GET;
        static gc::ref<BuiltinSingleDispatch> ASSOC;
        static gc::ref<BuiltinSingleDispatch> FIRST;
        static gc::ref<BuiltinSingleDispatch> NEXT;
        static gc::ref<BuiltinSingleDispatch> ITERATOR;
        static gc::ref<BuiltinSingleDispatch> JOIN;
        static gc::ref<BuiltinSingleDispatch> SEND;
        static gc::ref<BuiltinSingleDispatch> RECV;
        static gc::ref<BuiltinSingleDispatch> WRITE;
        static gc::ref<BuiltinSingleDispatch> CLOSE;
        static gc::ref<BuiltinSingleDispatch> WRITE_LINE;
        static gc::ref<BuiltinSingleDispatch> HASH;
        static gc::ref<BuiltinSingleDispatch> DEREF;
        static gc::ref<BuiltinSingleDispatch> COMPARE_AND_SET;

        static gc::ref<BuiltinBinaryDispatch> EQUALS;
        static gc::ref<BuiltinBinaryDispatch> NOT_EQUALS;
        static gc::ref<BuiltinBinaryDispatch> ADD;
        static gc::ref<BuiltinBinaryDispatch> SUBTRACT;
        static gc::ref<BuiltinBinaryDispatch> MULTIPLY;
        static gc::ref<BuiltinBinaryDispatch> LESSTHAN;
        static gc::ref<BuiltinBinaryDispatch> GREATERTHAN;
        static gc::ref<BuiltinBinaryDispatch> MODULO;


    };

    class BuiltinImpl : public SharedValueImpl<Builtin, BuiltinImpl> {
    public:
        explicit BuiltinImpl(const std::string &name) :
                name(name) {}

        static void init(Runtime &runtime);

        MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override = 0;

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<" << name << ">";
        }

    private:
        const std::string name;
    };


    class BuiltinStaticDispatch : public BuiltinImpl {
    protected:
        MethodImpl method;
    public:
        BuiltinStaticDispatch(const std::string &name, MethodImpl method) :
                BuiltinImpl(name), method(method) {}

        MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override {
            return method;
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  

    };

    

    class BuiltinSingleDispatch : public BuiltinImpl {
    protected:
        std::map<const Type *, MethodImpl> methods;

    public:
        explicit BuiltinSingleDispatch(const std::string &name) :
                BuiltinImpl(name) {}

        MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override;

        void register_method(const Type &self, MethodImpl method) {
            methods.insert(std::pair<const Type *, MethodImpl>({&self, method}));
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  

    };


    class BuiltinBinaryDispatch : public BuiltinImpl {

    protected:
        std::map<std::pair<const Type *, const Type *>, MethodImpl> methods_by_type_type_;
        std::map<std::pair<value_t::kind_t, value_t::kind_t>, MethodImpl> methods_by_kind_kind_;
        std::map<std::pair<value_t::kind_t, const Type *>, MethodImpl> methods_by_kind_type_;
        std::map<std::pair<const Type *, value_t::kind_t>, MethodImpl> methods_by_type_kind_;

    public:
        explicit BuiltinBinaryDispatch(const std::string &name) :
                BuiltinImpl(name) {}

        MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override;

        void register_method(const Type &lhs, const Type &rhs, MethodImpl method) {
            methods_by_type_type_.insert({{&lhs, &rhs}, method});
        }

        void register_method(value_t::kind_t lhs, value_t::kind_t rhs, MethodImpl method) {
            methods_by_kind_kind_.insert({{lhs, rhs}, method});
        }

        void register_method(value_t::kind_t lhs, const Type &rhs, MethodImpl method) {
            methods_by_kind_type_.insert({{lhs, &rhs}, method});
        }

        void register_method(const Type &lhs, value_t::kind_t rhs, MethodImpl method) {
            methods_by_type_kind_.insert({{&lhs, rhs}, method});
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}  
    };


}

#endif
