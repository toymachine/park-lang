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

#ifndef __VALUE_H
#define __VALUE_H

#include <iostream>
#include <memory>
#include <vector>
#include <limits>
#include <cassert>
#include <csignal>

#include "error.h"
#include "gc.h"

namespace park {

    class Runtime;

    class Fiber;

    class Type;

    class Visitor;

    namespace AST {
        struct Apply;
    }

    typedef int64_t (*MethodImpl)(Fiber &fbr, const AST::Apply &apply); // TODO rewrite as 'using' stmt


    class Value : public gc::collectable
    {
    public:

        virtual const Type &get_type() const = 0;

        virtual const size_t map_key_hash(Fiber &fbr) const = 0;
        virtual const bool map_key_equals(Fiber &fbr, const Value &other) const = 0;

        virtual void repr(Fiber &fbr, std::ostream &out) const = 0;

        //callable values:
        virtual MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const = 0;

        //cast to primitive
        virtual int64_t to_index(Fiber &fbr, int64_t start = 0, int64_t end = std::numeric_limits<int64_t>::max()) const = 0;
        virtual bool to_bool(Fiber &fbr) const = 0;
        virtual std::string to_string(Fiber &fbr) const = 0;

        //accept a value for use in visitor pattern
        virtual const Value &accept(Fiber &fbr, Visitor &visitor) const = 0;

    };

    template<typename T, typename TImpl>
    class ValueImpl : public T {
    public:
        static gc::ref<Type> TYPE;

        virtual const Type &get_type() const {
            assert(TYPE);
            return *TYPE;
        }

        virtual MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const {
            throw Error::not_implemented(fbr, *this, "dispatch");
        }

        virtual int64_t
        to_index(Fiber &fbr, int64_t start = 0, int64_t end = std::numeric_limits<int64_t>::max()) const {
            throw Error::not_implemented(fbr, *this, "to_index");
        }

        virtual const size_t map_key_hash(Fiber &fbr) const {
            throw Error::not_implemented(fbr, *this, "map_key_hash");
        }

        virtual const bool map_key_equals(Fiber &fbr, const Value &other) const {
            throw Error::not_implemented(fbr, *this, "map_key_equals");
        }

        virtual bool to_bool(Fiber &fbr) const {
            throw Error::not_implemented(fbr, *this, "to_bool");
        }

        virtual std::string to_string(Fiber &fbr) const {
            throw Error::not_implemented(fbr, *this, "to_string");
        }

        virtual void repr(Fiber &fbr, std::ostream &out) const = 0;

        virtual const Value &accept(Fiber &fbr, Visitor &visitor) const {
            throw Error::not_implemented(fbr, *this, "accept");
        }

    };

    template<typename T, typename TImpl>
    gc::ref<Type> ValueImpl<T, TImpl>::TYPE;

    template<typename T, typename TImpl>
    class SharedValueImpl : public ValueImpl<gc::with_finalizer<T>, TImpl> {};

    //variant type for use without heap allocations (in stack etc)
    struct value_t {
        enum class kind_t {
            UVALUE, IVALUE, BVALUE, DVALUE, RVALUE
        } kind;
        union {
            int64_t ivalue;
            bool bvalue;
            double dvalue;
            const Value *rvalue;
        };

        bool is_ref() const {
            return kind == kind_t::RVALUE;
        }

        bool is_int64() const {
            return kind == kind_t::IVALUE;
        }

        int64_t int64() const {
            assert(is_int64());
            return ivalue;
        }

        const Value * ref() const {
            assert(is_ref());
            return rvalue;
        }

    };

    static_assert(std::is_pod<value_t>::value, "value_t must be a POD type.");

    namespace value {

        const Value &from_value_t(Fiber &fbr, const value_t &value);

        gc::ref<Value> ref_from_value_t(Fiber &fbr, const value_t &value);

        template<typename T>
        bool from_value_t(const value_t &sval, T &tval);

        template<>
        inline bool from_value_t(const value_t &sval, const Value *&tval) {
            if (sval.kind == value_t::kind_t::RVALUE) {
                tval = sval.rvalue;
                return true;
            } else {
                return false;
            }
        }

        template<>
        inline bool from_value_t(const value_t &sval, int64_t &tval) {
            if (sval.kind == value_t::kind_t::IVALUE) {
                tval = sval.ivalue;
                return true;
            } else {
                return false;
            }
        }

        const Type &value_type(const value_t &value);

        template<typename T>
        T cast(Fiber &fbr, const value_t &value);

        template<>
        inline bool cast(Fiber &fbr, const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::BVALUE: {
                    return value.bvalue;
                }
                case value_t::kind_t::RVALUE: {
                    return value.rvalue->to_bool(fbr);
                }
                case value_t::kind_t::IVALUE: {
                    return value.ivalue != 0;
                }
                case value_t::kind_t::DVALUE: {
                    return value.dvalue != 0.0;
                }
                case value_t::kind_t::UVALUE: {
                    return false;
                }
            }
        }

        template<>
        inline int64_t cast(Fiber &fbr, const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::BVALUE: {
                    return value.bvalue;
                }
                case value_t::kind_t::RVALUE: {
                    return value.rvalue->to_index(fbr);
                }                
                case value_t::kind_t::IVALUE: {
                    return value.ivalue;
                }
                case value_t::kind_t::DVALUE: {
                    return value.dvalue;
                }
                case value_t::kind_t::UVALUE: {
                    throw std::runtime_error("blah");
                }
            }
        }

        template<>
        inline const Value &cast(Fiber &fbr, const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::RVALUE: {
                    return *value.rvalue;
                }
                case value_t::kind_t::IVALUE: {
                    return from_value_t(fbr, value);
                }
                case value_t::kind_t::BVALUE: {
                    return from_value_t(fbr, value);
                }
                case value_t::kind_t::DVALUE: {
                    throw std::runtime_error("TODO");
                }
                case value_t::kind_t::UVALUE: {
                    throw std::runtime_error("undefined value_t cannot be cast to Value &");
                }
            }
        }

        template<>
        inline gc::ref<Value> cast(Fiber &fbr, const value_t &value) {
            switch (value.kind) {
                case value_t::kind_t::RVALUE: {
                    return gc::ref<Value>(value.rvalue);
                }                                
                case value_t::kind_t::IVALUE: {
                    return ref_from_value_t(fbr, value);
                }
                case value_t::kind_t::BVALUE: {
                    return ref_from_value_t(fbr, value);
                }
                case value_t::kind_t::DVALUE: {
                    return ref_from_value_t(fbr, value);
                }
                case value_t::kind_t::UVALUE: {
                    throw std::runtime_error("undefined value_t cannot be cast to gc::ref<Value>");
                }
            }
        }

        template<typename T>
        value_t to_value_t(T);

        template<>
        inline value_t to_value_t<const Value *>(const Value *rvalue) {
            return {.kind = value_t::kind_t::RVALUE, .rvalue = rvalue};
        }

        template<>
        inline value_t to_value_t<const Value &>(const Value &value) {
            return {.kind = value_t::kind_t::RVALUE, .rvalue = &value};
        }

        template<>
        inline value_t to_value_t<gc::ref<Value>>(gc::ref<Value> value) {
#ifndef NDEBUG
            assert(value.get() != nullptr);
#endif
            return {.kind = value_t::kind_t::RVALUE, .rvalue = value.mutate()};
        }

        template<>
        inline value_t to_value_t<int64_t>(int64_t value) {
            return {.kind = value_t::kind_t::IVALUE, .ivalue = value};
        }

        template<>
        inline value_t to_value_t<bool>(bool value) {
            return {.kind = value_t::kind_t::BVALUE, .bvalue = value};
        }


    }
}


#endif
