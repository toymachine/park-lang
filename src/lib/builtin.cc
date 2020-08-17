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

#include <unordered_map>
#include <string>
#include <fstream>
#include <streambuf>
#include <stdlib.h>

#include <boost/algorithm/string/predicate.hpp>

#include "value.h"
#include "type.h"
#include "range.h"
#include "integer.h"
#include "string.h"
#include "channel.h"
#include "fiber.h"
#include "builtin.h"
#include "http.h"
#include "error.h"
#include "boolean.h"
#include "type.h"
#include "frame.h"

namespace park {

    MethodImpl BuiltinBinaryDispatch::dispatch(Fiber &fbr, const AST::Apply &apply) const {

        Frame frame(fbr, apply);
        if (apply.argument_count() != 2) {
            throw std::runtime_error("wrong number of arguments");
        }

        //std::cout << "a1" << std::endl;

        {
            auto found = methods_by_kind_kind_.find({frame.argument_kind(1), frame.argument_kind(2)});
            if (found != methods_by_kind_kind_.end()) {
                return found->second;
            }
        }

        //std::cout << "a2" << std::endl;

        {
            auto const &typeof_lhs = frame.argument_type(1);
            auto kind_of_rhs = frame.argument_kind(2);
            auto found = methods_by_type_kind_.find({&typeof_lhs, kind_of_rhs});
            if (found != methods_by_type_kind_.end()) {
                return found->second;
            }
        }

        {
            auto kind_of_lhs = frame.argument_kind(1);
            auto const &typeof_rhs = frame.argument_type(2);
            auto found = methods_by_kind_type_.find({kind_of_lhs, &typeof_rhs});
            if (found != methods_by_kind_type_.end()) {
                return found->second;
            }
        }

        {
            auto const &typeof_lhs = frame.argument_type(1);
            auto const &typeof_rhs = frame.argument_type(2);
            auto found = methods_by_type_type_.find({&typeof_lhs, &typeof_rhs});
            if (found != methods_by_type_type_.end()) {
                return found->second;
            }
        }


        //std::cout << "a3" << std::endl;

        throw Error::operator_not_defined_for_argument_types(fbr, *this, frame.argument_type(1),
                                                             frame.argument_type(2));
    }

    MethodImpl BuiltinSingleDispatch::dispatch(Fiber &fbr, const AST::Apply &apply) const {

        Frame frame(fbr, apply);
        if (apply.argument_count() < 1) {
            throw std::runtime_error("wrong number of arguments");
        }
        auto found = methods.find(&frame.argument_type(1));
        if (found != methods.end()) {
            return found->second;
        } else {
            throw Error::function_not_defined_for_argument_type(fbr, apply.line_, *this, frame.argument_type(1));//todo actually pass value not type
        }
    }

    gc::ref<BuiltinStaticDispatch> Builtin::TYPEOF;
    gc::ref<BuiltinStaticDispatch> Builtin::PRINT;
    gc::ref<BuiltinStaticDispatch> Builtin::RANGE;
    gc::ref<BuiltinStaticDispatch> Builtin::ORD;
    gc::ref<BuiltinStaticDispatch> Builtin::CHR;
    gc::ref<BuiltinStaticDispatch> Builtin::SLURP;
    gc::ref<BuiltinStaticDispatch> Builtin::SPIT;

    gc::ref<BuiltinSingleDispatch> Builtin::CONTAINS;
    gc::ref<BuiltinSingleDispatch> Builtin::NOT;
    gc::ref<BuiltinSingleDispatch> Builtin::LENGTH;
    gc::ref<BuiltinSingleDispatch> Builtin::CONJ;
    gc::ref<BuiltinSingleDispatch> Builtin::GET;
    gc::ref<BuiltinSingleDispatch> Builtin::ASSOC;
    gc::ref<BuiltinSingleDispatch> Builtin::FIRST;
    gc::ref<BuiltinSingleDispatch> Builtin::NEXT;
    gc::ref<BuiltinSingleDispatch> Builtin::ITERATOR;
    gc::ref<BuiltinSingleDispatch> Builtin::SEND;
    gc::ref<BuiltinSingleDispatch> Builtin::RECV;
    gc::ref<BuiltinSingleDispatch> Builtin::CLOSE;
    gc::ref<BuiltinSingleDispatch> Builtin::WRITE;
    gc::ref<BuiltinSingleDispatch> Builtin::WRITE_LINE;
    gc::ref<BuiltinSingleDispatch> Builtin::HASH;
    gc::ref<BuiltinSingleDispatch> Builtin::DEREF;
    gc::ref<BuiltinSingleDispatch> Builtin::COMPARE_AND_SET;

    gc::ref<BuiltinBinaryDispatch> Builtin::EQUALS;
    gc::ref<BuiltinBinaryDispatch> Builtin::NOT_EQUALS;
    gc::ref<BuiltinBinaryDispatch> Builtin::ADD;
    gc::ref<BuiltinBinaryDispatch> Builtin::SUBTRACT;
    gc::ref<BuiltinBinaryDispatch> Builtin::MULTIPLY;
    gc::ref<BuiltinBinaryDispatch> Builtin::LESSTHAN;
    gc::ref<BuiltinBinaryDispatch> Builtin::GREATERTHAN;
    gc::ref<BuiltinBinaryDispatch> Builtin::MODULO;


    void BuiltinImpl::init(Runtime &runtime) {
        TYPE = runtime.create_type("Builtin");
    }

    void Builtin::init(Runtime &runtime) {
        BuiltinImpl::init(runtime);

        
        Builtin::EQUALS = runtime.create_builtin<BuiltinBinaryDispatch>("equals");
        Builtin::NOT_EQUALS = runtime.create_builtin<BuiltinBinaryDispatch>("not_equals");

        Builtin::ADD = runtime.create_builtin<BuiltinBinaryDispatch>("add");
        Builtin::SUBTRACT = runtime.create_builtin<BuiltinBinaryDispatch>("subtract");
        Builtin::MULTIPLY = runtime.create_builtin<BuiltinBinaryDispatch>("multiply");
        Builtin::LESSTHAN = runtime.create_builtin<BuiltinBinaryDispatch>("lt");
        Builtin::GREATERTHAN = runtime.create_builtin<BuiltinBinaryDispatch>("gt");
        Builtin::MODULO = runtime.create_builtin<BuiltinBinaryDispatch>("mod");

        runtime.create_builtin<BuiltinSingleDispatch>("int");

        Builtin::LENGTH = runtime.create_builtin<BuiltinSingleDispatch>("length");
        Builtin::HASH = runtime.create_builtin<BuiltinSingleDispatch>("hash");

        Builtin::ASSOC = runtime.create_builtin<BuiltinSingleDispatch>("assoc");
        Builtin::GET = runtime.create_builtin<BuiltinSingleDispatch>("get");
        Builtin::ITERATOR = runtime.create_builtin<BuiltinSingleDispatch>("iterator");
        Builtin::CONTAINS = runtime.create_builtin<BuiltinSingleDispatch>("contains");
        Builtin::CONJ = runtime.create_builtin<BuiltinSingleDispatch>("conj");
        Builtin::NOT = runtime.create_builtin<BuiltinSingleDispatch>("not");

        Builtin::FIRST = runtime.create_builtin<BuiltinSingleDispatch>("first");
        Builtin::NEXT = runtime.create_builtin<BuiltinSingleDispatch>("next");

        Builtin::SEND = runtime.create_builtin<BuiltinSingleDispatch>("send");
        Builtin::RECV = runtime.create_builtin<BuiltinSingleDispatch>("recv");
        Builtin::CLOSE = runtime.create_builtin<BuiltinSingleDispatch>("close");

        Builtin::WRITE = runtime.create_builtin<BuiltinSingleDispatch>("write");
        Builtin::WRITE_LINE = runtime.create_builtin<BuiltinSingleDispatch>("write_line");

        Builtin::DEREF = runtime.create_builtin<BuiltinSingleDispatch>("deref");
        Builtin::COMPARE_AND_SET = runtime.create_builtin<BuiltinSingleDispatch>("compare_and_set");



        Builtin::PRINT = runtime.create_builtin<BuiltinStaticDispatch>("print", 
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

            Frame frame(fbr, apply);

            return frame.check().
                static_dispatch(*Builtin::PRINT).
                result<bool>([&]() {
                    auto &os = std::cout;
                    for (auto i = 1; i <= frame.argument_count(); i++) {
                        frame.argument<gc::ref<Value>>(i)->repr(fbr, os);
                        os << " ";
                    }
                    os << std::endl;
                    return true;
                });
        });

        Builtin::SLURP = runtime.create_builtin<BuiltinStaticDispatch>("slurp",
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                gc::ref<String> arg;

                return frame.check().
                    static_dispatch(*Builtin::SLURP).
                    argument_count(1).
                    argument<String>(1, arg).
                    result<Value>([&]() {
                        std::string fname = arg->to_string(fbr);

                        std::ifstream t(fname);
                        if (!t.is_open()) {
                            throw std::runtime_error("could not open file");
                        }

                        std::string str((std::istreambuf_iterator<char>(t)),
                                        std::istreambuf_iterator<char>());

                        return String::create(fbr, str);
                    });
        });

        /*




        Builtin::TYPEOF = std::make_shared<BuiltinStaticDispatch>("typeof", 
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                int64_t res = 0;

                if(!frame.check_static_dispatch(*Builtin::TYPEOF, res)) {
                    return res;
                }

                if(!frame.check_argument_count(1, res)) {
                    return res;
                }

                frame.pop_arguments_and_push_result<const Value &>(frame.argument_type(1));

                return res;
            });

        builtins.insert({{runtime.intern("typeof"), Builtin::TYPEOF}});


        //TODO move into range.cc init
        Builtin::RANGE = std::make_shared<BuiltinStaticDispatch>("range", 
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                int64_t res = 0;

                if(!frame.check_static_dispatch(*Builtin::RANGE, res)) {
                    return res;
                }

                if(!frame.check_argument_count(1, res)) {
                    return res;
                }
               
                frame.pop_arguments_and_push_result<const Value &>(
                    Range::from_range(fbr, 0, frame.argument<const Value &>(1).to_index(fbr))
                );

                return res;
            });

        builtins.insert({{runtime.intern("range"), Builtin::RANGE}});


        Builtin::ORD = std::make_shared<BuiltinStaticDispatch>("ord", 
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                int64_t res = 0;

                if(!frame.check_static_dispatch(*Builtin::ORD, res)) {
                    return res;
                }

                if(!frame.check_argument_count(1, res)) {
                    return res;
                }

                std::string s = frame.argument<const Value &>(1).to_string(fbr);
               
                frame.pop_arguments_and_push_result<int64_t>(s[0]);

                return res;
            });

        builtins.insert({{runtime.intern("ord"), Builtin::ORD}});

        Builtin::CHR = std::make_shared<BuiltinStaticDispatch>("chr", 
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                int64_t res = 0;

                if(!frame.check_static_dispatch(*Builtin::CHR, res)) {
                    return res;
                }

                if(!frame.check_argument_count(1, res)) {
                    return res;
                }

                auto ch = frame.argument<const Value &>(1).to_index(fbr, 0, 256);
               
                frame.pop_arguments_and_push_result<const Value &>(String::from_string(fbr, std::string(1, ch)));

                return res;
            });

        builtins.insert({{runtime.intern("chr"), Builtin::CHR}});




        Builtin::WRITE = std::make_shared<BuiltinStaticDispatch>("write",
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                Frame frame(fbr, apply);

                int64_t res = 0;

                if(!frame.check_static_dispatch(*Builtin::WRITE, res)) {
                    return res;
                }

                if(!frame.check_argument_count(1, res)) {
                    return res;
                }

                auto s = frame.argument<const Value &>(1).to_string(fbr);//TODO proper check only String

                std::cout << s << std::flush;

                frame.pop_arguments_and_push_result<int64_t>(s.size());

                return res;
        });

        builtins.insert({{runtime.intern("write"), Builtin::WRITE}});


        Builtin::SPIT = std::make_shared<BuiltinStaticDispatch>("spit",
            [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

            Frame frame(fbr, apply);

            int64_t res = 0;

            if(!frame.check_static_dispatch(*Builtin::SPIT, res)) {
                return res;
            }

            if(!frame.check_argument_count(2, res)) {
                return res;
            }

            auto fname = frame.argument<const Value &>(1).to_string(fbr);
            auto content = frame.argument<const Value &>(2).to_string(fbr);

            std::ofstream ofs(fname, std::ofstream::out);
            ofs << content;
            ofs.close();

            frame.stack.pop(3);
            frame.stack.push<int64_t>(content.size());

            return 0;
        });


        builtins.insert({{runtime.intern("spit"), Builtin::SPIT}});
        */
    }


    namespace builtin {
        /*
        void register_static_dispatch(Runtime &runtime, const std::string &name, MethodImpl method) {
            runtime.register_builtin(name, std::make_shared<BuiltinStaticDispatch>(name, method));
        }
        */
    }

}