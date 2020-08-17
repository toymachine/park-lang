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

#include "error2.h"
#include "type.h"
#include "visitor.h"
#include "frame.h"
#include "string.h"
#include "builtin.h"

namespace park {

    static gc::ref<Value> IS_ERROR;
    static gc::ref<Value> NOT;
    static gc::ref<Value> ERROR;

    class Error2Impl : public ValueImpl<Error2, Error2Impl> {

    private:
        gc::ref<String> message_;

    public:

        explicit Error2Impl(gc::ref<String> message) : message_(message) {}

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<Error2: ";
            message_->repr(fbr, out);
            out << ">";
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            accept(message_);
        }  

        gc::ref<String> message() const {
            return message_;
        }

        static int64_t _is_error(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Value> value;

            return frame.check().
               static_dispatch(*IS_ERROR).
               argument_count(1).
               argument<Value>(1, value). 
               result<bool>([&]() {
                   return is_error(*value);
               });
        }

        static int64_t _not(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Error2Impl> self;

            return frame.check().
               single_dispatch(*NOT, *TYPE).
               argument_count(1).
               argument<Error2Impl>(1, self). 
               result<bool>([&]() {
                   return true;
               });
        }

       
        static int64_t _error(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<String> result;

            return frame.check().
               static_dispatch(*ERROR).
               argument_count(1).
               argument<String>(1, result).  //TODO should be value too
               result<Error2>([&]() {
                   return Error2::create(fbr, result->to_string(fbr));
               });
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Error2");

            IS_ERROR = runtime.create_builtin<BuiltinStaticDispatch>("is_error", _is_error);

            NOT = runtime.builtin("not");
            runtime.register_method(NOT, *TYPE, _not);

            ERROR = runtime.create_builtin<BuiltinStaticDispatch>("Error", _error);
        }

    };

   
    gc::ref<Error2> Error2::create(Fiber &fbr, const std::string &error)
    {
        return gc::make_ref<Error2Impl>(fbr.allocator(), String::create(fbr, error));
    }

    void Error2::init(Runtime &runtime) {
        Error2Impl::init(runtime);
    }

    bool Error2::is_error(const Value &value) 
    {
        return &value.get_type() == Error2Impl::TYPE.get();
    }

}
