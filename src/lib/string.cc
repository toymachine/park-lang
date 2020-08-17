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


#include "string.h"
#include "string.h"
#include "integer.h"
#include "type.h"
#include "builtin.h"
#include "visitor.h"
#include "frame.h"

namespace park {

    static gc::ref<Value> INT;
    static gc::ref<Value> LENGTH;
    static gc::ref<Value> HASH;
    static gc::ref<Value> ADD;
    static gc::ref<Value> EQUALS;
    static gc::ref<Value> NOT_EQUALS;
    static gc::ref<Value> STRING2;

    static const int CUTOFF = 256;

    template<typename S, typename Impl>
    class BaseStringImpl : public ValueImpl<S, Impl>
    {

    public:
        const char *data() const override = 0;
        size_t size() const override = 0;

        const Value &accept(Fiber &fbr, Visitor &visitor) const override {
            return visitor.visit(fbr, *this);
        }

        const char *begin() const override
        {
            return data();
        }
        
        const char *end() const override
        {
            return data() + size();
        }

        virtual boost::asio::const_buffer buffer() const override 
        {
            return boost::asio::const_buffer(data(), size());
        }

        bool equals(const BaseStringImpl &other) const
        {
            if(this == &other) {
                return true;
            }

            if(size() != other.size()) {
                return false;
            }

            return 0 == memcmp(data(), other.data(), size());         
        }

        const size_t map_key_hash(Fiber &fbr) const override
        {
            std::hash<char> hasher;
            size_t result = 0;
            auto _size = size();
            auto _data = data();
            for(size_t i = 0; i < _size; ++i) {
                result = result * 31 + hasher(_data[i]);
            }
            return result;        
        }
        
        std::string to_string(Fiber &fbr) const override {
            return std::string(data(), size());
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out.write(data(), size());
        }

        


    };

    
    class BigStringImpl : public BaseStringImpl<gc::with_finalizer<String>, BigStringImpl>
    {
    private:
        std::string data_;
    public:
        BigStringImpl(const std::string &from_str)
            : data_(from_str) 
        {
            assert(from_str.size() >= CUTOFF); 
        }

        BigStringImpl(const char *data, size_t size, const char *data2, size_t size2)
            : data_()
        {
            assert(size + size2 >= CUTOFF);
            data_.reserve(size + size2);
            data_.append(data, size);
            data_.append(data2, size2);
        }   

        /*
        virtual ~BigStringImpl() {};

        void finalize() override
        {
            this->~BigStringImpl();
        }
        */

        const char *data() const override 
        {
            return data_.data();
        }

        size_t size() const override 
        {
            return data_.size();
        }

        const bool map_key_equals(Fiber &fbr, const Value &other) const override;

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}; 

        static gc::ref<String> create_impl(Fiber &fbr, const std::string &from_str)
        {
            return gc::make_shared_ref<BigStringImpl>(fbr.allocator(), from_str);
        }

        static gc::ref<String> create_shared_impl(Fiber &fbr, const std::string &from_str)
        {
            return gc::make_shared_ref<BigStringImpl>(fbr.allocator(), from_str);
        }           

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("BigString");
        }
    };

    class StringImpl : public BaseStringImpl<String, StringImpl>  {
    private:

        size_t size_;

    alignas(16)
        char data_[];

        
    public:
        StringImpl(const std::string &from_str)
            : size_(from_str.size())
        {
            assert(size_ < CUTOFF);
            std::copy(from_str.begin(), from_str.end(), data_);
        }   

        StringImpl(const char *data, size_t size, const char *data2, size_t size2)
            : size_(size + size2)
        {
            assert(size_ < CUTOFF);
            std::copy_n(data, size, data_);
            std::copy_n(data2, size2, data_+ size);
        }   

        StringImpl(size_t size)
            : size_(size)
        {
            assert(size_ < CUTOFF);
            std::memset(data_, 0, size);
        }   

        const char *data() const override 
        {
            return data_;
        }

        size_t size() const override 
        {
            return size_;
        }

        const bool map_key_equals(Fiber &fbr, const Value &other) const override;

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}; 

        static gc::ref<String> create_impl(Fiber &fbr, const std::string &from_str)
        {
            assert(from_str.size() < CUTOFF);
            return gc::make_ref_fam<StringImpl, char>(fbr.allocator(), from_str.size(), from_str);
        }

        static gc::ref<String> create_shared_impl(Fiber &fbr, const std::string &from_str)
        {
            return gc::make_shared_ref_fam<StringImpl, char>(fbr.allocator(), from_str.size(), from_str);
        }



        static int64_t _length(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<StringImpl> self;

            return frame.check().
               single_dispatch(*LENGTH, *TYPE).
               argument_count(1).
               argument<StringImpl>(1, self).
               result<int64_t>([&]() {
                   return self->size_;
               });
        }

        static int64_t _hash(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<StringImpl> self;

            return frame.check().
               single_dispatch(*HASH, *TYPE).
               argument_count(1).
               argument<StringImpl>(1, self).
               result<int64_t>([&]() {
                   return self->map_key_hash(fbr);
               });
        }

        static int64_t _add(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<StringImpl> self;
            gc::ref<StringImpl> other;

            return frame.check().
               binary_dispatch(*ADD, *TYPE, *TYPE).
               argument_count(2).
               argument<StringImpl>(1, self).
               argument<StringImpl>(2, other).
               result<Value>([&]() {
                   return String::concat(fbr, *self, *other);
               });
        }

        static int64_t _equals(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<StringImpl> self;
            gc::ref<StringImpl> other;

            return frame.check().
               binary_dispatch(*EQUALS, *TYPE, *TYPE).
               argument_count(2).
               argument<StringImpl>(1, self).
               argument<StringImpl>(2, other).
               result<bool>([&]() {
                   return self->equals(*other);
               });
        }

        static int64_t _not_equals(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<StringImpl> self;
            gc::ref<StringImpl> other;

            return frame.check().
               binary_dispatch(*NOT_EQUALS, *TYPE, *TYPE).
               argument_count(2).
               argument<StringImpl>(1, self).
               argument<StringImpl>(2, other).
               result<bool>([&]() {
                   return !self->equals(*other);
               });
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("String");

            runtime.register_method(LENGTH, *TYPE, _length);
            runtime.register_method(ADD, *TYPE, *TYPE, _add);
            runtime.register_method(EQUALS, *TYPE, *TYPE, _equals);
            runtime.register_method(NOT_EQUALS, *TYPE, *TYPE, _not_equals);
            runtime.register_method(HASH, *TYPE, _hash);
        }



    };

    gc::ref<String> String::concat(Fiber &fbr, const String &lhs, const String &rhs)
    {
        if(lhs.size() + rhs.size() < CUTOFF) {
            return gc::make_ref_fam<StringImpl, char>(fbr.allocator(), lhs.size() + rhs.size(), lhs.data(), lhs.size(), rhs.data(), rhs.size());
        }
        else {
            return gc::make_shared_ref<BigStringImpl>(fbr.allocator(), lhs.data(), lhs.size(), rhs.data(), rhs.size());
        }
    }

    static int64_t _add_big_small(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<BigStringImpl> self;
        gc::ref<StringImpl> other;

        return frame.check().
            binary_dispatch(*ADD, *BigStringImpl::TYPE, *StringImpl::TYPE).
            argument_count(2).
            argument<BigStringImpl>(1, self).
            argument<StringImpl>(2, other).
            result<Value>([&]() {
                return String::concat(fbr, *self, *other);
            });
    }

    static int64_t _add_small_big(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<StringImpl> self;
        gc::ref<BigStringImpl> other;

        return frame.check().
            binary_dispatch(*ADD, *StringImpl::TYPE, *BigStringImpl::TYPE).
            argument_count(2).
            argument<StringImpl>(1, self).
            argument<BigStringImpl>(2, other).
            result<Value>([&]() {
                return String::concat(fbr, *self, *other);
            });
    }

    static int64_t _int_small(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<StringImpl> self;

        return frame.check().
            single_dispatch(*INT, *StringImpl::TYPE).
            argument_count(1).
            argument<StringImpl>(1, self).
            result<int64_t>([&]() {
                return std::stoi(self->to_string(fbr));
            });
    }

    void String::init(Runtime &runtime)
    {
        LENGTH = runtime.builtin("length");
        ADD = runtime.builtin("add");
        EQUALS = runtime.builtin("equals");
        NOT_EQUALS = runtime.builtin("not_equals");
        HASH = runtime.builtin("hash");
        INT = runtime.builtin("int");

        StringImpl::init(runtime);
        BigStringImpl::init(runtime);

        runtime.register_method(ADD, *BigStringImpl::TYPE, *StringImpl::TYPE, _add_big_small);
        runtime.register_method(ADD, *StringImpl::TYPE, *BigStringImpl::TYPE, _add_small_big);
        runtime.register_method(INT, *StringImpl::TYPE, _int_small);

    }

    gc::ref<String> String::create(Fiber &fbr, const std::string &from_str)
    {
        if(from_str.size() < CUTOFF) {
            return StringImpl::create_impl(fbr, from_str);
        }
        else {
            return BigStringImpl::create_impl(fbr, from_str);
        }
    }

    gc::ref<String> String::create_shared(Fiber &fbr, const std::string &from_str)
    {
        if(from_str.size() < CUTOFF) {
            return StringImpl::create_shared_impl(fbr, from_str);
        }        
        else {
            return BigStringImpl::create_shared_impl(fbr, from_str);
        }
    }

    const bool StringImpl::map_key_equals(Fiber &fbr, const Value &other) const 
    {
        auto &type = other.get_type();
        if(&type == StringImpl::TYPE.get() || &type == BigStringImpl::TYPE.get()) {
            return equals(static_cast<const BaseStringImpl &>(other));
        }
        else {
            throw std::runtime_error("not a string");
        }
    }

    const bool BigStringImpl::map_key_equals(Fiber &fbr, const Value &other) const 
    {
        auto &type = other.get_type();
        if(&type == StringImpl::TYPE.get() || &type == BigStringImpl::TYPE.get()) {
            return equals(static_cast<const BaseStringImpl &>(other));
        }
        else {
            throw std::runtime_error("not a string");
        }
    }


    /*
    template<typename S, typename Impl>
    const bool BaseStringImpl<S, Impl>::map_key_equals(Fiber &fbr, const Value &other) const override
    {
        //TODO check when cast fails!, e.g. map keys might be of different type
        //return equals(dynamic_cast<const BaseStringImpl &>(other));
        auto &other_type = other.get_type();
        if(&other_type == BigStringImpl::TYPE.get() || &other_type == StringImpl.TYPE.get()) {
            return equals(static_cast<const BaseStringImpl &>(other));
        }
        else {
            throw std::runtime_error("can only compare strings");
        }
    }
    */

    /*
    gc::ref<String> String::create_local(Fiber &fbr, size_t size)
    {
        if(size <= MAX_SZ) {
            return StringImpl::create_impl(fbr, size);
        }
        else {
            throw std::runtime_error("too big");
        }
    }
    */


    /*
    const char *String::begin() const
    {
        //todo dyn/static cast based on DEBUG
        return static_cast<const StringImpl &>(*this).begin();
    }

    const char *String::end() const
    {
        //todo dyn/static cast based on DEBUG
        return static_cast<const StringImpl &>(*this).end();
    }
    */

}
