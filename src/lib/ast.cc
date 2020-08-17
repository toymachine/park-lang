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

#include <vector>
#include <string>
#include <fstream>

#include "ast.h"
#include "integer.h"
#include "string.h"
#include "map.h"
#include "vector.h"
#include "stack.h"
#include "runtime.h"
#include "builtin.h"
#include "pack.h"
#include "fiber.h"
#include "namespace.h"
#include "keyword.h"

#include <boost/endian/conversion.hpp>

namespace park {

    namespace AST {

        gc::ref<Apply> APPLY_DEFERS;

        gc::ref<Builtin> builtin_for_builtin(gc::allocator_t &allocator, gc::ref<Value> builtin)
        {
            auto value = value::to_value_t<const Value *>(builtin.get());
            return gc::make_shared_ref<AST::Builtin>(allocator, value);
        }

        Function::Function(size_t line, std::string name, gc::ref<Module> module, gc::ref<NodeList> freevars, gc::ref<NodeList> locals, 
                    gc::ref<NodeList> parameters, gc::ref<Node> expression)
                : name_(name), module_(module), code_(nullptr), freevars_(freevars), locals_(locals),
                    parameters_(parameters), expression_(expression) {
            assert(gc::is_shared_ref(module));    
            assert(gc::is_shared_ref(freevars));    
            assert(gc::is_shared_ref(locals));    
            assert(gc::is_shared_ref(parameters));    
            assert(gc::is_shared_ref(expression));    
            //std::cerr << "ast function created: " << (void *)this << std::endl;
            //TODO check for duplicate params/locals before inserting in map
            local_map_.reserve(parameters->size() + locals->size());
            size_t i = 1;
            for (auto &parameter : *parameters) {
                auto sym = gc::ref_dynamic_cast<Symbol>(parameter);
                local_map_[sym->namei_] = i;
                i++;
            }
            for (auto &local : *locals) {
                auto sym = gc::ref_dynamic_cast<Symbol>(local);
                local_map_[sym->namei_] = i;
                i++;
            }
            i = 0;
            freevar_map_.reserve(freevars->size());
            for (auto &freevar : *freevars) {
                auto sym = gc::ref_dynamic_cast<Symbol>(freevar);
                freevar_map_[sym->namei_] = i;
                i++;
            }



        }

        const Node &Function::exec_defers() const
        {
            return *APPLY_DEFERS;
        }

        gc::ref<NodeList> NodeList::create(gc::allocator_t &allocator, std::initializer_list<gc::ref<Node>> nodes)
        {
            return gc::make_shared_ref_fam<NodeList, gc::ref<Node>>(allocator, nodes.size(), nodes);            
        }

        gc::ref<NodeList> NodeList::create(gc::allocator_t &allocator, size_t size)
        {
            return gc::make_shared_ref_fam<NodeList, gc::ref<Node>>(allocator, size, size);
        }

        gc::ref<Apply> Apply::create_boot_0(gc::allocator_t &allocator)
        {
            return gc::make_shared_ref<Apply>(allocator,
                666, nullptr, NodeList::create(allocator, {}));
        }

        gc::ref<Apply> Apply::create_boot_1(gc::allocator_t &allocator)
        {
            return gc::make_shared_ref<Apply>(allocator,
                666, nullptr, NodeList::create(allocator, {Literal::create(allocator, value_t())}));
        }

        gc::ref<Apply> Apply::create_boot_2(gc::allocator_t &allocator)
        {
            return gc::make_shared_ref<Apply>(allocator,
                666, nullptr, NodeList::create(allocator, {Literal::create(allocator, value_t()), Literal::create(allocator, value_t())}));
        }

        gc::ref<Literal> Literal::create(gc::allocator_t &allocator, const value_t value)
        {
#ifndef NDEBUG
            if(value.kind == value_t::kind_t::RVALUE) {
                assert(value.rvalue);
                assert(gc::is_shared_ref(value.rvalue));
            }
#endif
            return gc::make_shared_ref<Literal>(allocator, value);
        }

        void Global::initialize()
        {
            //TODO initialize globals trough module loading, not need lock?
            std::lock_guard guard(lock_);
            if(!initialized_.load()) {
                if (auto found = module_->ns_->find(namei_)) {
                    value_ = *found;
                    assert(gc::is_shared_ref(value_));
                    initialized_.store(true);
                }
                else {
                    throw std::runtime_error("name not found while initializing global: " + name_);
                }
            }
        }

        void Module::walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept)
        {  
            accept(ns_);
            accept(expressions_);
        }

        //TODO try to move read_keys into read_node (diffult for read_function wrt current_locals
        //TODO make not dependent on type being the first key in map so that we do not need special
        //pack implementation

        class Reader::Impl {
            Runtime &runtime;

            gc::ref<AST::Module> current_module;

            std::vector<std::vector<gc::ref<AST::Local>>> current_locals;

            enum class node_type_t {
                NT_MODULE, NT_CONST, NT_DEFINE, NT_LET, NT_FUNCTION, NT_STRUCT, NT_STRUCT_FIELD, NT_IMPORT, NT_IF_ELSE_STATEMENT, NT_LOCAL, NT_RETURN, NT_RECUR,
                NT_CALL, NT_BUILTIN, NT_SYMBOL, NT_GLOBAL, NT_VECTOR, NT_DICT, NT_INTEGER, NT_KEYWORD, NT_STRING, NT_BOOLEAN
            };

            enum class node_key_t {
                NAME, VALUE, FROM, LINE, EXPR, STMTS, IF_STMTS, ELSE_STMTS, ARGS, PARMS, LOCALS, FREEVARS, IMPORTS, DATA
            };

            static const std::unordered_map<std::string, node_type_t> str_to_node_type;
            static const std::unordered_map<std::string, node_key_t> str_to_node_key;

            gc::ref<Node> stmts_as_expr(Fiber &fbr, gc::ref<AST::NodeList> stmts) {
                if (!stmts || stmts->empty()) { //TODO should we do this?
                    return gc::make_shared_ref<AST::Do>(fbr.allocator(), 
                        NodeList::create(fbr.allocator(), {Literal::create(fbr.allocator(), value_t())}));
                } else if (stmts->size() == 1) {
                    return *stmts->begin();
                } else {
                    return gc::make_shared_ref<AST::Do>(fbr.allocator(), stmts);
                }
            }

            struct keys_t {
                std::optional<int64_t> line;
                std::optional<std::string> name;
                std::optional<std::string> value;
                std::optional<std::string> from;

                gc::ref<Node> expr;

                gc::ref<NodeList> stmts;
                gc::ref<NodeList> if_stmts;
                gc::ref<NodeList> else_stmts;
                gc::ref<NodeList> args;
                gc::ref<NodeList> imports;
                gc::ref<NodeList> parms;
                gc::ref<NodeList> locals;
                gc::ref<NodeList> freevars;

                gc::ref<Value> data;
            };

        public:
            explicit Impl(Runtime &runtime) : runtime(runtime) {}

            size_t intern(const std::string &s) {
                std::lock_guard guard(runtime.lock);
                return runtime.intern(s);
            }

            void read_keys(Fiber &fbr, std::istream &ins, size_t map_len, keys_t &keys) {
                while (--map_len) {
                    auto key = pack::read_string(ins);
                    switch (str_to_node_key.at(key)) {
                        case node_key_t::DATA: {
                            keys.data = pack::unpack(fbr, ins); //TODO unpack should return value_t
                            break;
                        }
                        case node_key_t::NAME: {
                            keys.name = pack::read_string(ins);
                            break;
                        }
                        case node_key_t::VALUE: {
                            keys.value = pack::read_string(ins);
                            break;
                        }
                        case node_key_t::FROM: {
                            keys.from = pack::read_string(ins);
                            break;
                        }
                        case node_key_t::LINE: {
                            keys.line = pack::read_integer(ins);
                            break;
                        }
                        case node_key_t::EXPR: {
                            keys.expr = read_node(fbr, ins);
                            break;
                        }
                        case node_key_t::STMTS: {
                            keys.stmts = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::IF_STMTS: {
                            keys.if_stmts = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::ELSE_STMTS: {
                            keys.else_stmts = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::ARGS: {
                            keys.args = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::PARMS: {
                            keys.parms = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::LOCALS: {
                            keys.locals = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::FREEVARS: {
                            keys.freevars = read_nodes(fbr, ins);
                            break;
                        }
                        case node_key_t::IMPORTS: {
                            keys.imports = read_nodes(fbr, ins);
                            break;
                        }
                    }
                }
            }

            gc::ref<AST::NodeList> read_nodes(Fiber &fbr, std::istream &ins) {
                auto n = pack::read_array_header(ins);
                auto node_list = AST::NodeList::create(fbr.allocator(), n);
                assert(gc::is_shared_ref(node_list));
                for (auto &node : *node_list) {
                    const_cast<gc::ref<AST::Node> &>(node) = read_node(fbr, ins);
                    assert(gc::is_shared_ref(node));
                }
                return node_list;
            }

            gc::ref<Node> read_module(Fiber &fbr, std::istream &ins, size_t map_len) {

                assert(!current_module);

                current_module = gc::make_shared_ref<AST::Module>(fbr.allocator());

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                current_module.mutate()->set_expressions(keys.stmts);

                return current_module;
            }

            gc::ref<Node> read_define(Fiber &fbr, std::istream &ins, size_t map_len) {
                assert(current_module);

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(!(keys.expr && keys.data));

                if (keys.name && keys.expr) {
                    return 
                        gc::make_shared_ref<AST::Define>(fbr.allocator(), 
                            gc::make_shared_ref<AST::Symbol>
                                (fbr.allocator(), *keys.name, intern(*keys.name)),
                            keys.expr, gc::ref<Value>());
                } 
                else if(keys.name && keys.data) {
                    fbr.allocator().share(keys.data);
                    return 
                        gc::make_shared_ref<AST::Define>(fbr.allocator(), 
                            gc::make_shared_ref<AST::Symbol>
                                (fbr.allocator(), *keys.name, intern(*keys.name)),
                            gc::ref<Node>(), keys.data);
                }
                else {
                    throw std::runtime_error("expected name and expr while reading define node");
                }
            }

            gc::ref<Node> read_struct_field(Fiber &fbr, std::istream &ins, size_t map_len) {
                assert(current_module);

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.name && keys.data);

                fbr.allocator().share(keys.data);
                return 
                    gc::make_shared_ref<AST::Define>(fbr.allocator(), 
                        gc::make_shared_ref<AST::Symbol>
                            (fbr.allocator(), *keys.name, intern(*keys.name)),
                        gc::ref<Node>(), keys.data);
            }

            gc::ref<Node> read_struct(Fiber &fbr, std::istream &ins, size_t map_len) {
                assert(current_module);

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.name && keys.stmts) {
                    return 
                        gc::make_shared_ref<AST::Struct>(fbr.allocator(),
                            gc::make_shared_ref<AST::Symbol>
                                    (fbr.allocator(), *keys.name, intern(*keys.name)),
                            keys.stmts);
                } 
                else {
                    throw std::runtime_error("expected name and stmts while reading struct node");
                }
            }

            gc::ref<Node> read_let(Fiber &fbr, std::istream &ins, size_t map_len) {
                assert(current_module);

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.name && keys.expr) {
                    return gc::make_shared_ref<AST::Let>(fbr.allocator(),
                            gc::make_shared_ref<AST::Symbol>(fbr.allocator(),
                                *keys.name, intern(*keys.name)), keys.expr);

                } else {
                    throw std::runtime_error("expected name and expr while reading let node");
                }
            }

            gc::ref<Node> read_symbol(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.value) {
                    return gc::make_shared_ref<AST::Symbol>(fbr.allocator(), std::move(*keys.value), intern(*keys.value));
                } else {
                    throw std::runtime_error("todo symbol");
                }
            }

            gc::ref<Node> read_global(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.value) {
                    return gc::make_shared_ref<AST::Global>(fbr.allocator(),
                        current_module, *keys.value, intern(*keys.value));
                } else {
                    throw std::runtime_error("todo global");
                }
            }

            gc::ref<Node> read_local(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.value && *keys.value != "");

                auto local = gc::make_shared_ref<AST::Local>(fbr.allocator(),
                    std::move(*keys.value), intern(*keys.value));

                current_locals.back().push_back(local);

                return local;

            }

            gc::ref<Node> read_if_else_statement(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                return gc::make_shared_ref<AST::Branch>(
                    fbr.allocator(),
                    std::move(keys.expr), 
                    stmts_as_expr(fbr, keys.if_stmts),
                    stmts_as_expr(fbr, keys.else_stmts));
            }

            gc::ref<Node> read_return(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.expr) {
                    return gc::make_shared_ref<AST::Return>(fbr.allocator(), keys.expr);
                } else {
                    throw std::runtime_error("expected expr while reading return");
                }

            }

            gc::ref<Node> read_call(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.line && keys.expr) {
                    auto apply = gc::make_shared_ref<AST::Apply>(fbr.allocator(), 
                        *keys.line, std::move(keys.expr), std::move(keys.args));

                    if(keys.data) {
                        /*
                        keys.props->repr(fbr, std::cerr);
                        std::cerr << std::endl;
                        */
                        apply.mutate()->throws_ = false;
                    }

                    return apply;
                } else {
                    throw std::runtime_error("error in read_call");
                }

            }

            gc::ref<Node> read_builtin(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (keys.value) {
                    //no need to lock find_builtins, because builtins do not get written
                    //concurrently TODO really?
                    if (auto found = runtime.find_builtin(*keys.value)) {
                        return gc::make_shared_ref<AST::Builtin>(fbr.allocator(),
                            value::to_value_t<gc::ref<Value>>((*found).get()));
                    } else {
                        throw std::runtime_error("builtin not found in read_builtin: '" + *keys.value + "'");
                    }
                } else {
                    throw std::runtime_error("error in read_builtin");
                }
            }

            gc::ref<Node> read_integer(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.value);

                return Literal::create(fbr.allocator(), value::to_value_t<int64_t>(std::stoi(*keys.value)));
            }

            gc::ref<Node> read_keyword(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.value);

                return Literal::create(fbr.allocator(), value::to_value_t<gc::ref<Value>>(Keyword::create(fbr, (*keys.value))));
            }


            gc::ref<Node> read_string(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.value);

                auto const _value = String::create_shared(fbr, *keys.value);

                return Literal::create(fbr.allocator(), value::to_value_t<const Value *>(_value.get()));
            }


            gc::ref<Node> read_boolean(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                assert(keys.value);

                if (*keys.value == "true") {
                    return Literal::create(fbr.allocator(), value::to_value_t<bool>(true));
                } else if (*keys.value == "false") {
                    return Literal::create(fbr.allocator(), value::to_value_t<bool>(false));
                } else {
                    throw std::runtime_error("bad value for bool: " + *keys.value);
                }
            }

            gc::ref<Node> read_vector(Fiber &fbr, std::istream &ins, size_t map_len) {

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                auto value = Vector::create(fbr);

                return Literal::create(fbr.allocator(), value::to_value_t<gc::ref<Value>>(value));
            }

            gc::ref<Node> read_dict(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                auto value = Map::create(fbr);

                return Literal::create(fbr.allocator(), value::to_value_t<gc::ref<Value>>(value));
            }

            gc::ref<Node> read_recur(Fiber &fbr, std::istream &ins, size_t map_len) {
                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                return gc::make_shared_ref<AST::Recur>(fbr.allocator(), keys.args);
            }

            gc::ref<Node> read_import(Fiber &fbr, std::istream &ins, size_t map_len) {
                 assert(current_module);

                 keys_t keys;
                 read_keys(fbr, ins, map_len, keys);
 
                 return 
                    gc::make_shared_ref<AST::Import>(fbr.allocator(), current_module,
                            *keys.from, keys.imports);
            }

            gc::ref<Node> read_function(Fiber &fbr, std::istream &ins, size_t map_len) {
                assert(current_module);

                current_locals.push_back({});

                keys_t keys;
                read_keys(fbr, ins, map_len, keys);

                if (!keys.name) {
                    keys.name = "<unknown>";
                }

                if (!keys.line) {
                    keys.line = 666;
                }
                
                auto function = gc::make_shared_ref<AST::Function>(fbr.allocator(), *keys.line, *keys.name, current_module, keys.freevars, keys.locals,
                                                                   keys.parms, stmts_as_expr(fbr, keys.stmts));

                //now bind locals to their index
                for (auto &local : current_locals.back()) {
                    if (auto local_index = function->local_index(local->namei_)) {
                        local.mutate()->index_ = *local_index;   
                    } else {
                        throw std::runtime_error("could not resolve local index for local: " + local->name_);
                    }
                }

                current_locals.pop_back();

                return function;

            }

            gc::ref<Node> read_node(Fiber &fbr, std::istream &ins) {

                auto map_len = pack::read_map_header(ins);
                if (map_len < 1) {
                    throw std::runtime_error("expected at least 1 entry in node");
                }

                auto type_key = pack::read_string(ins);
                assert(type_key == "type");

                auto type_value = pack::read_string(ins);

                if(str_to_node_type.find(type_value) == str_to_node_type.end()) {
                    throw std::runtime_error("unknown node_type in prc: " + type_value);
                }

                switch (str_to_node_type.at(type_value)) {
                    case node_type_t::NT_MODULE:
                        return read_module(fbr, ins, map_len);
                    case node_type_t::NT_DEFINE:
                        return read_define(fbr, ins, map_len);
                    case node_type_t::NT_STRUCT:
                        return read_struct(fbr, ins, map_len);
                    case node_type_t::NT_STRUCT_FIELD:
                        return read_struct_field(fbr, ins, map_len);
                    case node_type_t::NT_LET:
                        return read_let(fbr, ins, map_len);
                    case node_type_t::NT_FUNCTION:
                        return read_function(fbr, ins, map_len);
                    case node_type_t::NT_IMPORT:
                        return read_import(fbr, ins, map_len);
                    case node_type_t::NT_IF_ELSE_STATEMENT:
                        return read_if_else_statement(fbr, ins, map_len);
                    case node_type_t::NT_LOCAL:
                        return read_local(fbr, ins, map_len);
                    case node_type_t::NT_RETURN:
                        return read_return(fbr, ins, map_len);
                    case node_type_t::NT_RECUR:
                        return read_recur(fbr, ins, map_len);
                    case node_type_t::NT_CALL:
                        return read_call(fbr, ins, map_len);
                    case node_type_t::NT_BUILTIN:
                        return read_builtin(fbr, ins, map_len);
                    case node_type_t::NT_SYMBOL:
                        return read_symbol(fbr, ins, map_len);
                    case node_type_t::NT_GLOBAL:
                        return read_global(fbr, ins, map_len);
                    case node_type_t::NT_VECTOR:
                        return read_vector(fbr, ins, map_len);
                    case node_type_t::NT_DICT:
                        return read_dict(fbr, ins, map_len);
                    case node_type_t::NT_INTEGER:
                        return read_integer(fbr, ins, map_len);
                    case node_type_t::NT_KEYWORD:
                        return read_keyword(fbr, ins, map_len);
                    case node_type_t::NT_STRING:
                        return read_string(fbr, ins, map_len);
                    case node_type_t::NT_BOOLEAN:
                        return read_boolean(fbr, ins, map_len);
                    default: {
                        throw std::runtime_error("unknown node_type");
                    }
                }
            }
        };

        const std::unordered_map<std::string, Reader::Impl::node_type_t> Reader::Impl::str_to_node_type =
                {{"module",            node_type_t::NT_MODULE},
                 {"define",            node_type_t::NT_DEFINE},
                 {"let",               node_type_t::NT_LET},
                 {"function",          node_type_t::NT_FUNCTION},
                 {"struct",            node_type_t::NT_STRUCT},
                 {"struct_field",      node_type_t::NT_STRUCT_FIELD},
                 {"import",            node_type_t::NT_IMPORT},
                 {"if_else_statement", node_type_t::NT_IF_ELSE_STATEMENT},
                 {"local",             node_type_t::NT_LOCAL},
                 {"return",            node_type_t::NT_RETURN},
                 {"recur",             node_type_t::NT_RECUR},
                 {"call",              node_type_t::NT_CALL},
                 {"builtin",           node_type_t::NT_BUILTIN},
                 {"symbol",            node_type_t::NT_SYMBOL},
                 {"global",            node_type_t::NT_GLOBAL},
                 {"vector",            node_type_t::NT_VECTOR},
                 {"dict",              node_type_t::NT_DICT},
                 {"integer",           node_type_t::NT_INTEGER},
                 {"keyword",           node_type_t::NT_KEYWORD},
                 {"string",            node_type_t::NT_STRING},
                 {"boolean",           node_type_t::NT_BOOLEAN}
                };

        const std::unordered_map<std::string, Reader::Impl::node_key_t> Reader::Impl::str_to_node_key =
                {{"name",       node_key_t::NAME},
                 {"value",      node_key_t::VALUE},
                 {"from",       node_key_t::FROM},
                 {"line",       node_key_t::LINE},
                 {"expr",       node_key_t::EXPR},
                 {"stmts",      node_key_t::STMTS},
                 {"if_stmts",   node_key_t::IF_STMTS},
                 {"else_stmts", node_key_t::ELSE_STMTS},
                 {"args",       node_key_t::ARGS},
                 {"parms",      node_key_t::PARMS},
                 {"locals",     node_key_t::LOCALS},
                 {"freevars",   node_key_t::FREEVARS},
                 {"imports",    node_key_t::IMPORTS},
                 {"data",       node_key_t::DATA},
                };

        Reader::Reader(Runtime &runtime)
                : _impl(std::make_unique<Reader::Impl>(runtime)) {}

        Reader::~Reader() {}

        gc::ref<Module> Reader::read(Fiber &fbr, std::istream &ins) {
            return _impl->read_node(fbr, ins);
        }

        static gc::ref<BuiltinStaticDispatch> PACK_NODE;

        void init(Runtime &runtime) {

            PACK_NODE = runtime.create_builtin<BuiltinStaticDispatch>("pack_node", 
                [](Fiber &fbr, const AST::Apply &apply) -> int64_t {

                    Frame frame(fbr, apply);

                    int64_t res = 0;

                    if(!frame.check_static_dispatch(*PACK_NODE, res)) {
                        return res;
                    }

                    if(!frame.check_argument_count(2, res)) {
                        return res;
                    }

                    auto &node = frame.argument<const Value &>(1);
                    auto fname = frame.argument<const Value &>(2).to_string(fbr);
     
                    std::ofstream ofs(fname, std::ios_base::binary);

                    pack::pack_node(fbr, node, ofs);

                    frame.pop_arguments_and_push_result<bool>(true);

                    return res; 
                }); 



            APPLY_DEFERS = runtime.create_root<Apply>([&](gc::allocator_t &allocator) {
                return gc::make_shared_ref<Apply>(allocator, 
                    666, gc::make_shared_ref<Symbol>(allocator, "__apply_defers__", runtime.APPLY_DEFERS_NAMEI),
                    NodeList::create(allocator, 
                        {gc::make_shared_ref<Symbol>(allocator, "__defers__", runtime.DEFERS_NAMEI)}));
            });

        }
    }
}