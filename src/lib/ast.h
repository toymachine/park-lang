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

#ifndef __AST_H
#define __AST_H

#include <memory>
#include <unordered_map>
#include <atomic>
#include <iostream>
#include <optional>

#include "value.h"

#include <boost/optional.hpp>

namespace park {

    class Fiber;

    class Value;

    class Namespace;

    class Map;

    class Runtime;

    namespace AST {

        struct Node;
        struct Literal;
        struct Symbol;
        struct Branch;
        struct Return;
        struct Apply;
        struct Function;
        struct Import;
        struct Do;
        struct Recur;
        struct Let;
        struct Local;
        struct Global;
        struct Builtin;
        struct Define;
        struct Module;
        struct Struct;

        class Visitor;

        struct Node : public gc::collectable {

            virtual void accept(Visitor &visitor) const = 0;

        };

        struct SharedNode : public gc::with_finalizer<Node> {};

        struct NodeList : public gc::collectable
        {
            size_t size_;

            alignas(16)
            gc::ref<Node> nodes_[];

            NodeList(size_t size) : size_(size) {
                for(auto i = 0; i < size; i++) {
                    nodes_[i] = gc::ref<Node>{};
                }
            }

            NodeList(std::initializer_list<gc::ref<Node>> nodes)
                : size_(nodes.size()) {
                std::copy(nodes.begin(), nodes.end(), nodes_);
#ifndef NDEBUG
                for(auto i = 0; i < size_; i++) {
                    assert(gc::is_shared_ref(nodes_[i]));
                }
#endif
            }
            
            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                for(auto i = 0; i < size_; i++) {
                    accept(nodes_[i]);
                }
            }

            bool empty() const {
                return size_ == 0;
            }

            size_t size() const {
                return size_;
            }

            const gc::ref<Node> *begin() const {
                return &nodes_[0];
            }

            const gc::ref<Node> *end() const {
                return &nodes_[size_];
            }

            static gc::ref<NodeList> create(gc::allocator_t &allocator, size_t size);

            static gc::ref<NodeList> create(gc::allocator_t &allocator, std::initializer_list<gc::ref<Node>> nodes);
        };

        class Visitor {
        public:
            virtual void visit_define(const Define &node) {};

            virtual void visit_apply(const Apply &node)  {};

            virtual void visit_symbol(const Symbol &node) {};

            virtual void visit_builtin(const Builtin &node) {};

            virtual void visit_let(const Let &node) {};

            virtual void visit_local(const Local &node) {};

            virtual void visit_global(const Global &node) {};

            virtual void visit_branch(const Branch &node) {};

            virtual void visit_return(const Return &node) {};

            virtual void visit_recur(const Recur &node) {};

            virtual void visit_function(const Function &node) {};

            virtual void visit_do(const Do &node) {};

            virtual void visit_literal(const Literal &node) {};

            virtual void visit_module(const Module &node) {};

            virtual void visit_import(const Import &node) {};

            virtual void visit_struct(const Struct &node) {};
        };

        struct Module : public Node {
            gc::ref<Namespace> ns_;
            gc::ref<NodeList> expressions_;

            void accept(Visitor &visitor) const override {
                visitor.visit_module(*this);
            }

            void set_expressions(gc::ref<NodeList> expressions) {
                assert(gc::is_shared_ref(expressions));
                expressions_ = expressions;
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override;
        };

        struct Define : public Node {
            gc::ref<Symbol> symbol_;
            gc::ref<Node> expression_;
            gc::ref<Value> data_;

            Define(gc::ref<Symbol> symbol, gc::ref<Node> expression, gc::ref<Value> data)
                : symbol_(symbol), expression_(expression), data_(data) 
            {
#ifndef NDEBUG
                assert(gc::is_shared_ref(symbol));
                if(expression) {
                    assert(gc::is_shared_ref(expression));
                }
                if(data) {
                    assert(gc::is_shared_ref(data));
                }
#endif                
            }

            void accept(Visitor &visitor) const override {
                visitor.visit_define(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                accept(symbol_);
                if(expression_) {
                    accept(expression_);
                }
                if(data_) {
                    accept(data_);
                }
            }
        };

        struct Struct : public Node {
            gc::ref<Symbol> name_;
            gc::ref<NodeList> fields_;

            Struct(gc::ref<Symbol> name, gc::ref<NodeList> fields) 
                : name_(name), fields_(fields) {}

            void accept(Visitor &visitor) const override {
                visitor.visit_struct(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                accept(name_);
                accept(fields_);
            }            
        };

        struct Import : public SharedNode {
            gc::ref<Module> module_;
            std::string from_;
            gc::ref<NodeList> imports_;

            Import(gc::ref<Module> module, const std::string &from, gc::ref<NodeList> imports) 
                : module_(module), from_(from), imports_(imports) {
                assert(gc::is_shared_ref(module));
                assert(gc::is_shared_ref(imports));
            }

            void accept(Visitor &visitor) const override {
                visitor.visit_import(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                accept(module_);
                accept(imports_);
            }
        };

        struct Literal : public Node {
            const value_t value_;

            Literal(const value_t value) : value_(value) {
#ifndef NDEBUG
                if(value_.kind == value_t::kind_t::RVALUE) {
                    assert(gc::is_shared_ref(value_.rvalue));
                }                
#endif
            }

            void accept(Visitor &visitor) const override {
                visitor.visit_literal(*this);
            }

            static gc::ref<Literal>
            create(gc::allocator_t &allocator, const value_t value);

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                if(value_.kind == value_t::kind_t::RVALUE) {
                    assert(value_.rvalue);
                    accept(value_.rvalue);
                }
            }
        };


        struct Branch : public Node {
            gc::ref<Node> condition_;
            gc::ref<Node> trueBranch_;
            gc::ref<Node> falseBranch_;

            Branch(gc::ref<Node> condition,
                   gc::ref<Node> trueBranch,
                   gc::ref<Node> falseBranch)
                    : condition_(condition),
                      trueBranch_(trueBranch),
                      falseBranch_(falseBranch) {
                assert(gc::is_shared_ref(condition));
                assert(gc::is_shared_ref(trueBranch));
                assert(gc::is_shared_ref(falseBranch));                        
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_branch(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                accept(condition_);
                accept(trueBranch_);
                accept(falseBranch_);
            }
        };

        struct Symbol : public SharedNode {
            const std::string name_;
            size_t namei_;

            Symbol(const std::string &name, size_t namei)
                    : name_(name), namei_(namei) {}

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_symbol(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {};  
        };

        struct Builtin : public Node {
            const value_t value_;
            
            Builtin(const value_t value) : value_(value) {
                assert(value_.kind != value_t::kind_t::UVALUE);
#ifndef NDEBUG
                if(value_.kind == value_t::kind_t::RVALUE) {
                    assert(gc::is_shared_ref(value_.rvalue));
                }
#endif
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_builtin(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                if(value_.kind == value_t::kind_t::RVALUE) {
                    assert(value_.rvalue);
                    accept(value_.rvalue);
                }
            }

        };

        struct Global : public SharedNode {

            std::atomic<bool> initialized_;
            gc::ref<Value> value_;
            gc::ref<Module> module_;
            const std::string name_;
            const size_t namei_;

            std::mutex lock_; //TODO global object locks

            Global(gc::ref<Module> module, const std::string name, const size_t namei)
                    : initialized_(false), value_(nullptr), module_(module), name_(name), namei_(namei), lock_() {
                assert(gc::is_shared_ref(module));
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_global(*this);
            }

            void initialize();

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                if(initialized_.load()) {
                    assert(value_);
                    accept(value_);
                }
                assert(module_);
                accept(module_);
            }

        };

        struct Let : public Node {
            gc::ref<Symbol> symbol_;
            gc::ref<Node> expression_;

            Let(gc::ref<Symbol> symbol, gc::ref<Node> expression)
                : symbol_(symbol), expression_(expression) {
                assert(gc::is_shared_ref(symbol));    
                assert(gc::is_shared_ref(expression));    
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_let(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
                accept(symbol_);
                accept(expression_);
            }
        };

        struct Local : public SharedNode {
            std::string name_;
            size_t namei_;
            size_t index_;

            Local(size_t index)
                : name_(), namei_(), index_(index) {}

            Local(std::string name, size_t namei)
                : name_(name), namei_(namei), index_(0) {}

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_local(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}
        };


        struct Function : public SharedNode {
            size_t line_;

            std::string name_;
            gc::ref<Module> module_;

            std::atomic<MethodImpl> code_;

            gc::ref<NodeList> freevars_;
            gc::ref<NodeList> locals_;
            gc::ref<NodeList> parameters_;

            gc::ref<Node> expression_;

            std::unordered_map<size_t, size_t> local_map_;
            std::unordered_map<size_t, size_t> freevar_map_; //maps freevars to their position in Closure table

            Function(size_t line, std::string name, gc::ref<Module> module, 
                     gc::ref<NodeList> freevars, gc::ref<NodeList> locals, 
                     gc::ref<NodeList> parameters, gc::ref<Node> expression);

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                accept(module_);
                accept(freevars_);
                accept(locals_);
                accept(parameters_);
                accept(expression_);
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_function(*this);
            }

            const size_t local_count() const {
                return locals_->size_;
            }

            bool has_defers() const;

            const Node &exec_defers() const;

            std::optional<size_t> local_index(size_t namei) const {
                auto found = local_map_.find(namei);
                if (found != local_map_.end()) {
                    return found->second;
                } else {
                    return std::nullopt;
                }
            }


            std::optional<size_t> freevar_index(size_t namei) const {
                auto found = freevar_map_.find(namei);
                if (found != freevar_map_.end()) {
                    return found->second;
                } else {
                    return std::nullopt;
                }
            }

            /*
            void dump() {
                size_t i = 1;
                std::cout << "parameters:" << std::endl;
                for (auto const &parameter : parameters) {
                    auto const &sym = std::static_pointer_cast<Symbol>(parameter);
                    std::cout << "  param: " << sym->name << std::endl;
                    i++;
                }
                std::cout << "locals:" << std::endl;
                for (auto const &local : locals) {
                    auto const &sym = std::static_pointer_cast<Symbol>(local);
                    std::cout << "  local: " << sym->name << std::endl;
                    i++;
                }
                std::cout << "freevars:" << std::endl;
                for (auto const &freevar : freevars) {
                    auto const &sym = std::static_pointer_cast<Symbol>(freevar);
                    std::cout << "  freevar: " << sym->name << std::endl;
                    i++;
                }
            }
            */

        };

        struct Do : public Node {
            gc::ref<NodeList> statements_;

            Do(gc::ref<NodeList> statements)
                : statements_(statements) {
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_do(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                accept(statements_);
            }
        };

        struct Return : public Node {
            gc::ref<Node> expression_;

            explicit Return(gc::ref<Node> expression) 
                : expression_(expression) {
                assert(gc::is_shared_ref(expression));
            }

            void accept(Visitor &visitor) const override {
                visitor.visit_return(*this);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                accept(expression_);
            }
        };

        struct Recur : public Node {
            gc::ref<NodeList> arguments_;

            Recur(gc::ref<NodeList> arguments) : arguments_(arguments) {
                assert(gc::is_shared_ref(arguments));
            }

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_recur(*this);
            }

            const size_t argument_count() const {
                return arguments_->size_;
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                accept(arguments_);
            }
        };


        struct Apply : public Node {
            std::atomic<MethodImpl> target_;
            size_t line_;
            gc::ref<Node> callable_;
            gc::ref<NodeList> arguments_;
            bool bootstrap_apply = false;
            bool throws_ = true;


            Apply(size_t line, gc::ref<Node> callable, gc::ref<NodeList> arguments);

            static gc::ref<Apply> create_boot_0(gc::allocator_t &allocator);
            static gc::ref<Apply> create_boot_1(gc::allocator_t &allocator);
            static gc::ref<Apply> create_boot_2(gc::allocator_t &allocator);

            /*
            static gc::ref<Apply> boot_0(gc::allocator_t &allocator)
            {
                return gc::make_shared_ref<Apply>(allocator,
                    666, nullptr, NodeList::create(allocator, {}));
            }
            */

            virtual void accept(Visitor &visitor) const override {
                visitor.visit_apply(*this);
            }

            size_t argument_count() const {
                return arguments_->size_;
            }

            void set_target(MethodImpl target) {
                target_.store(target, std::memory_order_relaxed);
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                if(callable_) {
                    accept(callable_);
                }
                if(arguments_) {
                    accept(arguments_);
                }
            }

        };

        class Reader {
        public:
            Reader(Runtime &runtime);

            ~Reader();

            gc::ref<Module> read(Fiber &fbr, gc::ref<Map> module);
            gc::ref<Module> read(Fiber &fbr, std::istream &ins);

        private:
            class Impl;

            std::unique_ptr<Impl> _impl;

        };

        void init(Runtime &runtime);

    }
}
#endif
