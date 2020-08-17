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

#ifndef __FRAME_H
#define __FRAME_H

#include "value.h"
#include "ast.h"
#include "stack.h"
#include "fiber.h"

namespace park {

    class Frame;
 
    class FrameCheck
    {
    private:
        Frame &frame_;
        int64_t res_;
    public:
        FrameCheck(Frame &frame, int64_t res)
            : frame_(frame), res_(res) {}

    	operator bool () const { return res_ == 0; }

        int64_t result() { return res_; };

        const FrameCheck static_dispatch(const Value &callable) const;
        const FrameCheck single_dispatch(const Value &callable, const Type &type) const;
        const FrameCheck binary_dispatch(const Value &callable, const Type &lhs, const Type &rhs) const;

        const FrameCheck argument_count(int count) const;
        const FrameCheck argument_count(int from, int up_to_including) const;

        bool ok() const;

        template<typename T, typename F>
        int64_t result(F f) const;

        template<typename F>
        int64_t cc_resume(F f) const;

        template<typename Result, typename F, typename B>
        inline int64_t result_or_block(F f, B b) const;

        template<typename T>
        const FrameCheck argument(int index, gc::ref<T> &out) const;

        template<typename T>
        const FrameCheck optional_argument(int index, gc::ref<T> &out) const;

        template<typename T>
        const FrameCheck argument(int index, T &out) const;

    };

    class Frame {
    private:   
        Fiber &fbr_;
        const AST::Apply &apply_;

    public:       
        Fiber::stack_t &stack;
        size_t base;

        Frame(Fiber &fbr,
              const AST::Apply &apply)
                : fbr_(fbr), apply_(apply), 
                    stack(fbr.stack), 
                    base(stack.base(apply_.argument_count()))
        {}

        const FrameCheck check()
        {
            return FrameCheck(*this, 0);
        }

        template<typename Y>
        inline int64_t result(gc::ref<Y> v)
        {
            pop_arguments_and_push_result<gc::ref<Value>>(gc::ref_cast<Value>(v));
            return 0;
        }

        template<typename Y>
        inline int64_t throws(gc::ref<Y> v)
        {
            pop_arguments_and_push_result<gc::ref<Value>>(gc::ref_cast<Value>(v));
            return 1;
        }

        template<typename Y>
        inline int64_t result(Y v)
        {
            pop_arguments_and_push_result<Y>(v);
            return 0;
        }

        inline int argument_count() const {
            return apply_.argument_count();
        }

        const value_t &argument_(int index) {
            return stack.argument(base, index);
        }

        template<typename T>
        inline T argument(int index) const {
            return value::cast<T>(fbr_, stack.argument(base, index));
        }

        template<typename T>
        inline bool argument_if_type(int index, T &value) const {
            return value::from_value_t<T>(stack.argument(base, index), value);
        }

        inline const Type &argument_type(int index) const
        {
            return value::value_type(stack.argument(base, index));
        } 

        inline value_t::kind_t argument_kind(int index) const {
            return stack.argument(base, index).kind;
        }

        int64_t bad_dispatch();

        int64_t exception(const std::string what);

        int64_t cc_resume(std::function<bool(Fiber &fbr)> f);

        template<typename T>
        void pop_arguments_and_push_result(T result) {
            stack.pop(apply_.argument_count() + 1);
            stack.push<T>(result);
        }

        template<typename lhs_t, typename rhs_t>
        inline bool binary(lhs_t &a, rhs_t &b, const Value &callable, int64_t &res) {
            const Value *_callable;
            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable &&
                argument_if_type<lhs_t>(1, a) &&
                argument_if_type<rhs_t>(2, b)) {
                stack.pop(3);
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }

        //todo use dyn cast in debug, static on prod
        template<typename ImplTypeLHS, typename ImplTypeRHS>
        inline bool binary(const ImplTypeLHS *&a, const ImplTypeRHS *&b, const Value &callable, int64_t &res) {
            const Value *_callable;
            auto const &_a = argument<const Value &>(1);
            auto const &_b = argument<const Value &>(2);

            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable &&
                (&_a.get_type() == ImplTypeLHS::TYPE.get()) &&
                (&_b.get_type() == ImplTypeRHS::TYPE.get())) {
                a = static_cast<const ImplTypeLHS *>(&_a);
                b = static_cast<const ImplTypeRHS *>(&_b);
                stack.pop(3);
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }

        template<typename lhs_t, typename rhs_t>
        inline std::tuple<bool, int64_t, lhs_t, rhs_t> check_binary(const Value &callable) {
            const Value *_callable;
            lhs_t a;
            rhs_t b;
            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable &&
                argument_if_type<lhs_t>(1, a) &&
                argument_if_type<rhs_t>(2, b)) {
                stack.pop(3);
                return {true, 0, a, b};
            } else {
                return {false, bad_dispatch(), a, b};
            }
        }

        template<typename ImplTypeLHS, typename ImplTypeRHS>
        std::tuple<bool, int64_t, const ImplTypeLHS &, const ImplTypeRHS &> check_binary_boxed(const Value &callable) {
            const Value *_callable;

            auto const &a = static_cast<const ImplTypeLHS &>(argument<const Value &>(1));
            auto const &b = static_cast<const ImplTypeRHS &>(argument<const Value &>(2));

            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable &&
                (&a.get_type() == ImplTypeLHS::TYPE.get()) &&
                (&b.get_type() == ImplTypeRHS::TYPE.get())) {
                stack.pop(3);
                return {true, 0, a, b};
            } else {
                return {false, bad_dispatch(), a, b};
            }
        }


        /*

        template<typename ImplType>
        inline bool single(const ImplType *&a, const Value &callable, int64_t &res) {
            const Value *_callable;
            auto const &_a = argument<const Value &>(1);

            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable &&
                (&_a.get_type() == ImplType::type.get())) {
                a = static_cast<const ImplType *>(&_a);
                stack.pop(2);
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }
        */

        inline bool check_argument_count(int expected_argument_count, int64_t &res)
        {
            if(apply_.argument_count() != expected_argument_count) {
                throw std::runtime_error("wrong argument count");
            }
            return true;
        }

        inline bool check_argument_count(int from, int up_to_including, int64_t &res)
        {
            if(apply_.argument_count() >= from &&
               apply_.argument_count() <= up_to_including) {
                return true;
            }
            else {  
                throw std::runtime_error("wrong argument count");
            }
        }

        inline bool check_static_dispatch(const Value &callable, int64_t &res) {
            const Value *_callable;

            if (argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable) {
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }

        inline bool check_single_dispatch(const Value &callable, const Type &type, int64_t &res) {
            const Value *_callable;
            if (apply_.argument_count() >= 1 &&
                argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable
                &&  &argument<const Value &>(1).get_type() == &type
                ) {
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }

        inline bool check_binary_dispatch(const Value &callable, const Type &lhs, const Type &rhs, int64_t &res) {
            const Value *_callable;
            if (apply_.argument_count() == 2 &&
                argument_if_type<const Value *>(0, _callable) &&
                _callable == &callable
                &&  &argument<const Value &>(1).get_type() == &lhs
                &&  &argument<const Value &>(2).get_type() == &rhs
                ) {
                res = 0;
                return true;
            } else {
                res = bad_dispatch();
                return false;
            }
        }

    };


    inline const FrameCheck FrameCheck::static_dispatch(const Value &callable) const
    {
        if(res_ == 0) {
            int64_t res;
            if(!frame_.check_static_dispatch(callable, res)) {
                return FrameCheck(frame_, res);
            }
        }
        return *this;
    }

    inline const FrameCheck FrameCheck::single_dispatch(const Value &callable, const Type &type) const
    {
        if(res_ == 0) {
            int64_t res;
            if(!frame_.check_single_dispatch(callable, type, res)) {
                return FrameCheck(frame_, res);
            }
        }
        return *this;
    }

    inline const FrameCheck FrameCheck::binary_dispatch(const Value &callable, const Type &lhs, const Type &rhs) const
    {
        if(res_ == 0) {
            int64_t res;
            if(!frame_.check_binary_dispatch(callable, lhs, rhs, res)) {
                return FrameCheck(frame_, res);
            }
        }
        return *this;
    }

    inline const FrameCheck FrameCheck::argument_count(int expected_argument_count) const
    {
        if(res_ == 0) {
            int64_t res;
            if(!frame_.check_argument_count(expected_argument_count, res)) {
                return FrameCheck(frame_, res);
            }
        }
        return *this;
    }


    inline const FrameCheck FrameCheck::argument_count(int from, int up_to_including) const
    {
        if(res_ == 0) {
            int64_t res;
            if(!frame_.check_argument_count(from, up_to_including, res)) {
                return FrameCheck(frame_, res);
            }
        }
        return *this;
    }
        

    template<typename T>
    inline const FrameCheck FrameCheck::argument(int index, gc::ref<T> &out) const
    {
        if(res_ == 0) {
            out = gc::ref_dynamic_cast<T>(frame_.argument<gc::ref<Value>>(index));
        }
        return *this;
    }

    template<typename T>
    inline const FrameCheck FrameCheck::optional_argument(int index, gc::ref<T> &out) const
    {
        if(res_ == 0 && index >= 0 && index <= frame_.argument_count()) {
            out = gc::ref_cast<T>(frame_.argument<gc::ref<Value>>(index));
        }
        return *this;
    }

    template<typename T>
    inline const FrameCheck FrameCheck::argument(int index, T &out) const
    {
        if(res_ == 0) {
            out = frame_.argument<T>(index);
        }
        return *this;        
    }

    template<typename T, typename F>
    inline int64_t FrameCheck::result(F f) const
    {
        if(res_ == 0) {
            return frame_.result<T>(f());
        }
        return res_;
    }

    inline bool FrameCheck::ok() const
    {
        return res_ ==  0;
    }

    template<typename Result, typename F, typename B>
    inline int64_t FrameCheck::result_or_block(F f, B b) const
    {
        if(res_ == 0) {
            bool throws = false;
            auto result = f(throws);
            if(result) {
                if(throws) {
                    return frame_.throws<Result>(*result);
                }
                else {
                    return frame_.result<Result>(*result);
                }
            }
            else {
                return frame_.cc_resume([b](Fiber &fbr) {
                    b(fbr);
                    return false;
                });
            }
        }
        return res_;
    }

    template<typename F>
    inline int64_t FrameCheck::cc_resume(F f) const
    {
        if(res_ == 0) {
            return frame_.cc_resume(f);
        }
        return res_;
    }
}

#endif