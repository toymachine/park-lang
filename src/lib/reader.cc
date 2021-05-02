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

#include "reader.h"
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
#include "list.h"
#include "symbol.h"
#include "vector.h"

namespace park {


    class ReaderImpl : public ValueImpl<Reader, ReaderImpl> {
    private:
        static std::unordered_map<std::string, std::string> translate;
        static lexertl::state_machine sm;

        static gc::ref<Value> NEXT;
        static gc::ref<Value> FIRST;
        static gc::ref<Value> READER;

        gc::ref<String> input_;
        gc::ref<Value> head_;

        lexertl::cmatch match_;
//        size_t line_ = 1;

        enum Token { LPAREN = 1, RPAREN, LBRACK, RBRACK, INTEGER, SYMBOL };


    public:
        explicit ReaderImpl(gc::ref<String> input, gc::ref<Value> head, const lexertl::cmatch &match)
                : input_(input), head_(head), match_(match) { //input_->begin(), input_->end()) 
            assert(head_);
            assert(input_);
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            accept(input_);
            accept(head_);
        }  

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Reader");

            READER = runtime.create_builtin<BuiltinStaticDispatch>("reader", _reader);

            FIRST = *runtime.find_builtin("first");
            runtime.register_method(FIRST, *TYPE, _first);

            NEXT = *runtime.find_builtin("next");
            runtime.register_method(NEXT, *TYPE, _next);

            lexertl::rules rules;

            // whitespace
            rules.push("\\n", 667); //TODO use nos to count newlines
            rules.push("\\s", 666);

            //reserved names
            //rules.push(
            //        "let|import|from|const|struct|function|if|else|return|recurs|true|false|=>|in|==|!=|&&|\\?|\\|\\||\\{|\\}|\\(|\\)|\\[|\\]|!|=|,|:|\\-|\\+|\\*|\\<|\\>|\\x25",
            //        1); //reserved names, operators, punctuation etc

            rules.push("\\(", LPAREN);
            rules.push("\\)", RPAREN);

            rules.push("\\[", LBRACK);
            rules.push("\\]", RBRACK);

            //rules.push("$[a-zA-Z_]\\w*", 6); //keyword 
            //rules.push("[a-zA-Z_]\\w*", 2); //identifier
            rules.push("[1-9][0-9]*|0", INTEGER); //integer literal
            rules.push("[a-zA-Z_\\+\\-\\*][a-zA-Z0-9_-]*", SYMBOL); //symbol

            // string literals
            //rules.push("'.*?'", 5);
            //rules.push("\\\".*?\\\"", 5);

            /* c single line comment */
            //rules.push("\"//\".*", 4);

            /* c style multiline comment */
//            rules.push_state("COMMENT");
//            rules.push("INITIAL", "\"/*\"", "COMMENT");
//            rules.push("COMMENT", "[^*]+|.", "COMMENT");
//            rules.push("COMMENT", "\"*/\"", 668, "INITIAL");


            lexertl::generator::build(rules, sm);

        }

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<reader>";
        }

        virtual bool to_bool(Fiber &fbr) const override {
            return head_;
        }

        static int64_t _reader(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<String> input;

            return frame.check().
                static_dispatch(*READER).
                argument_count(1).
                argument<String>(1, input).
                result<Value>([&]() {
                    lexertl::cmatch match(input->begin(), input->end());
                    next_token(match);
                    auto head = read_expr(fbr, match);
                    return gc::make_ref<ReaderImpl>(fbr.allocator(), input, head, match);
                });
        }

        static void next_token(lexertl::cmatch &match) {
            lexertl::lookup(sm, match);

            while (match.id != 0) {
                if (match.id == 666) {
                } else if (match.id == 667) {
//                    line_ += 1;
                } else if (match.id == 668) {
                    auto token_str = match.str();
                    //TODO can we make lexer do the line count?, can it report
                    //newlines embedded in the commment?
//                    line_ += std::count(token_str.begin(), token_str.end(), '\n');
                } else if (match.id != match.npos()) {
                    break;
                }
                if (match.id == match.npos()) {
                    std::cerr << "unrecognized: '" << match.str() << "'" << std::endl;
                    throw std::runtime_error("unrecognized");
                }

                lexertl::lookup(sm, match);
            }
        }

        static void accept_token(lexertl::cmatch &match, int token_id)
        {
            if(match.id != token_id) {
                throw std::runtime_error("TODO cannot accept");
            }
            next_token(match);
        }


        static gc::ref<List> read_list_tail(Fiber &fbr, lexertl::cmatch &match)
        {
            if(match.id != RPAREN) {
                auto item = read_expr(fbr, match);
                auto tail = read_list_tail(fbr, match);
                return tail->conj(fbr, item);
            }
            else {
                accept_token(match, RPAREN);
                return List::create(fbr);
            }
        }

        static gc::ref<Value> read_list(Fiber &fbr, lexertl::cmatch &match)
        {
            accept_token(match, LPAREN);
            return read_list_tail(fbr, match);
        }

        static gc::ref<Value> read_vector(Fiber &fbr, lexertl::cmatch &match)
        {
            accept_token(match, LBRACK);
            auto vector = Vector::create(fbr);
            accept_token(match, RBRACK);
            return vector;
        }

        static gc::ref<Value> read_integer(Fiber &fbr, lexertl::cmatch &match)
        {
            auto value = Integer::create(fbr, match.str());
            accept_token(match, INTEGER);
            return value;
        }

        static gc::ref<Value> read_symbol(Fiber &fbr, lexertl::cmatch &match)
        {
            auto value = Symbol::create(fbr, match.str());
            accept_token(match, SYMBOL);
            return value;
        }

        static gc::ref<Value> read_expr(Fiber &fbr, lexertl::cmatch &match)
        {
            switch(match.id) {
                case LPAREN: {
                    return read_list(fbr, match);
                }
                case LBRACK: {
                    return read_vector(fbr, match);
                }
                case INTEGER: {
                    return read_integer(fbr, match);
                }
                case SYMBOL: {
                    return read_symbol(fbr, match);
                }
                default: {
                    throw std::runtime_error("TODO unmatched reader case");
                }
            }
        }

        static int64_t _first(Fiber &fbr, const AST::Apply &apply) {

            //TODO return some form of struct as opposed to expensive map
            Frame frame(fbr, apply);

            gc::ref<ReaderImpl> self;

            return frame.check().
                single_dispatch(*FIRST, *TYPE).
                argument_count(1).
                argument<ReaderImpl>(1, self).
                result<Value>([&]() {

                    //return self->read_expr(fbr);
                    if(!self->to_bool(fbr)) {
                        throw std::runtime_error("no head, reader is empty");
                    }

                    return self->head_;

                    /*
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
                    */
                });
        }
                   
        static int64_t _next(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<ReaderImpl> self;

            return frame.check().
                single_dispatch(*NEXT, *TYPE).
                argument_count(1).
                argument<ReaderImpl>(1, self).
                result<Value>([&]() {
                    auto match = self->match_; //copy
                    next_token(match);
                    auto head = read_expr(fbr, match);
                    return gc::make_ref<ReaderImpl>(fbr.allocator(), self->input_, head, match);
                });
        }

    };

    lexertl::state_machine ReaderImpl::sm;
    //std::unordered_map<std::string, std::string> ReaderImpl::translate;

    gc::ref<Value> ReaderImpl::NEXT;
    gc::ref<Value> ReaderImpl::FIRST;
    gc::ref<Value> ReaderImpl::READER;


    void Reader::init(Runtime &runtime) {
        ReaderImpl::init(runtime);
    }

}