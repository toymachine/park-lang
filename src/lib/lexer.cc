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

#include "lexer.h"
#include "type.h"

#include <algorithm>
#include <unordered_map>

#include "lexertl/generator.hpp"
#include "lexertl/lookup.hpp"

#include "builtin.h"
#include "runtime.h"
#include "boolean.h"
#include "string.h"
#include "map.h"
#include "integer.h"

namespace park {

    class LexerImpl : public ValueImpl<Lexer, LexerImpl> {
    private:
        static std::unordered_map<std::string, std::string> translate;
        static lexertl::state_machine sm;

        static gc::ref<Value> NEXT;
        static gc::ref<Value> FIRST;
        static gc::ref<Value> LEXER;

        gc::ref<String> input_;

        lexertl::cmatch results_;
        size_t line_ = 1;


    public:
        explicit LexerImpl(gc::ref<String> input)
                : input_(input), results_(input_->begin(), input_->end()) {
            lookup();
        }

        LexerImpl(const LexerImpl &obj, bool next) //next argument to distinguish from copy constructor
                : LexerImpl(obj) {
            lookup();
        }

        void lookup() {
            lexertl::lookup(sm, results_);

            while (results_.id != 0) {
                if (results_.id == 666) {
                } else if (results_.id == 667) {
                    line_ += 1;
                } else if (results_.id == 668) {
                    auto token_str = results_.str();
                    //TODO can we make lexer do the line count?, can it report
                    //newlines embedded in the commment?
                    line_ += std::count(token_str.begin(), token_str.end(), '\n');
                } else if (results_.id != results_.npos()) {
                    break;
                }
                if (results_.id == results_.npos()) {
                    std::cerr << "unrecognized: '" << results_.str() << "'" << std::endl;
                    throw std::runtime_error("unrecognized");
                }

                lexertl::lookup(sm, results_);
            }
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            accept(input_);
        }  

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Lexer");

            LEXER = runtime.create_builtin<BuiltinStaticDispatch>("lexer", _lexer);

            FIRST = *runtime.find_builtin("first");
            runtime.register_method(FIRST, *TYPE, _first);

            NEXT = *runtime.find_builtin("next");
            runtime.register_method(NEXT, *TYPE, _next);

            lexertl::rules rules;

            // whitespace
            rules.push("\\n", 667); //TODO use nos to count newlines
            rules.push("\\s", 666);

            //reserved names
            rules.push(
                    "let|import|from|const|struct|function|if|else|return|recurs|true|false|=>|in|==|!=|&&|\\?|\\|\\||\\{|\\}|\\(|\\)|\\[|\\]|!|=|,|:|\\-|\\+|\\*|\\<|\\>|\\x25",
                    1); //reserved names, operators, punctuation etc

            rules.push("$[a-zA-Z_]\\w*", 6); //keyword 
            rules.push("[a-zA-Z_]\\w*", 2); //identifier
            rules.push("[1-9][0-9]*|0", 3); //integer literal

            // string literals
            rules.push("'.*?'", 5);
            rules.push("\\\".*?\\\"", 5);

            /* c single line comment */
            rules.push("\"//\".*", 4);

            /* c style multiline comment */
            rules.push_state("COMMENT");
            rules.push("INITIAL", "\"/*\"", "COMMENT");
            rules.push("COMMENT", "[^*]+|.", "COMMENT");
            rules.push("COMMENT", "\"*/\"", 668, "INITIAL");


            lexertl::generator::build(rules, sm);

            translate["\x25"] = "percent";
            translate["("] = "lparen";
            translate[")"] = "rparen";
            translate["?"] = "qmark";
            translate["["] = "lbrack";
            translate["]"] = "rbrack";
            translate["{"] = "lbrace";
            translate["}"] = "rbrace";
            translate["=="] = "double_equals";
            translate["!="] = "not_equals";
            translate["&&"] = "double_amp";
            translate["||"] = "double_pipe";
            translate[","] = "comma";
            translate["'"] = "single_quote";
            translate["-"] = "minus";
            translate["+"] = "plus";
            translate["*"] = "star";
            translate["="] = "equals";
            translate[":"] = "colon";
            translate["<"] = "lessthan";
            translate[">"] = "greaterthan";
            translate["=>"] = "equals_greaterthan";
            translate["!"] = "exclamation_mark";

        }

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<lexer>";
        }

        virtual bool to_bool(Fiber &fbr) const override {
            return results_.id != 0;
        }

        static int64_t _lexer(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Value> arg;

            return frame.check().
                static_dispatch(*LEXER).
                argument_count(1).
                argument<Value>(1, arg).
                result<Value>([&]() {
                    return gc::make_ref<LexerImpl>(fbr.allocator(), arg);
                });
        }

        static int64_t _first(Fiber &fbr, const AST::Apply &apply) {

            //TODO return some form of struct as opposed to expensive map
            Frame frame(fbr, apply);

            gc::ref<LexerImpl> self;

            return frame.check().
                single_dispatch(*FIRST, *TYPE).
                argument_count(1).
                argument<LexerImpl>(1, self).
                result<Value>([&]() {
                    auto m = Map::create(fbr)->
                            assoc(fbr, String::create(fbr, "line"), Integer::create(fbr, self->line_));

                    if (self->results_.id == 0) { //eoi
                        return m->
                                assoc(fbr, String::create(fbr, "token"), String::create(fbr, "eoi"));
                    } else if (self->results_.id == 2) { //identifier
                        return m->
                                assoc(fbr, String::create(fbr, "token"),
                                    String::create(fbr, "identifier"))->
                                assoc(fbr, String::create(fbr, "value"),
                                    String::create(fbr, self->results_.str()));
                    } else if(self->results_.id == 6) { //keyword literal
                        return m->
                                assoc(fbr, String::create(fbr, "token"),
                                    String::create(fbr, "keyword"))->
                                assoc(fbr, String::create(fbr, "value"),
                                    String::create(fbr, self->results_.str()));
                    } else if (self->results_.id == 3) { //integer literal
                        return m->
                                assoc(fbr, String::create(fbr, "token"),
                                    String::create(fbr, "integer_literal"))->
                                assoc(fbr, String::create(fbr, "value"),
                                    String::create(fbr, self->results_.str()));
                    } else if (self->results_.id == 5) { //string literal
                        std::vector<char> buff;
                        bool escape = false;
                        for(auto cur = self->results_.first + 1; cur < self->results_.second - 1; cur++) {
                            if(escape) {
                                if(*cur == '\\') {
                                   buff.push_back('\\'); 
                                }
                                else if(*cur == 'n') {
                                   buff.push_back('\n'); 
                                }
                                else if(*cur == 'r') {
                                   buff.push_back('\r'); 
                                }
                                escape = false;
                            }
                            else {
                                if(*cur == '\\') {
                                    escape = true;
                                }
                                else {
                                    buff.push_back(*cur);
                                }
                            }
                        }
                        assert(!escape);
                        return m->
                                assoc(fbr, String::create(fbr, "token"),
                                    String::create(fbr, "string_literal"))->
                                assoc(fbr, String::create(fbr, "value"), String::create(fbr, std::string(buff.data(), buff.size())));
                    } else {
                        auto found = translate.find(self->results_.str());
                        if (found != translate.end()) {
                            return m->assoc(fbr, String::create(fbr, "token"),
                                        String::create(fbr, found->second));
                        } else {
                            return m->assoc(fbr, String::create(fbr, "token"),
                                        String::create(fbr, self->results_.str()));
                        }
                    }

                    return m;
                });
        }
                   
        static int64_t _next(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<LexerImpl> self;

            return frame.check().
                single_dispatch(*NEXT, *TYPE).
                argument_count(1).
                argument<LexerImpl>(1, self).
                result<Value>([&]() {
                    return gc::make_ref<LexerImpl>(fbr.allocator(), *self, true);
                });
        }

    };

    lexertl::state_machine LexerImpl::sm;
    std::unordered_map<std::string, std::string> LexerImpl::translate;

    gc::ref<Value> LexerImpl::NEXT;
    gc::ref<Value> LexerImpl::FIRST;
    gc::ref<Value> LexerImpl::LEXER;


    void Lexer::init(Runtime &runtime) {
        LexerImpl::init(runtime);
    }

}