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

#include <sys/mman.h>

#include <vector>
#include <string>
#include <cassert>

#include <unordered_set>

#include "compiler.h"
#include "assembler.h"
#include "ast.h"
#include "exec.h"
#include "align.h"

namespace park {

    extern "C" typedef uint64_t (*entry_thunk_t)(Fiber *fbr, const AST::Apply *apply, MethodImpl code);
    extern "C" typedef uint64_t (*reentry_thunk_t)(Fiber *fbr, void *ip, int64_t ret_code);

    class X64Backend : public AST::Visitor {
    private:
        X64Assembler x64;
        int return_label; //return label of the currently compiling function
        int recur_label;  //recur label of the currently compiling function
        void *exit_thunk;
        const AST::Function *current_function_; //current function being compiled
    public:


        explicit X64Backend(void *exit_thunk) : x64(), return_label(0), recur_label(0), exit_thunk(exit_thunk), current_function_(nullptr) {
        }

        X64Backend() : X64Backend(nullptr) {
        }

        void emit_call(const AST::Node &node, const void *exec_fn) {
            x64.mov_rdi_rbx(); //1st arg Fiber
            x64.mov_rsi_imm(reinterpret_cast<int64_t>(&node)); //2nd arg AST node
            x64.mov_rax_imm(reinterpret_cast<int64_t>(exec_fn));
            x64.call_rax(); //call exec_fn
        }

        void visit_literal(const AST::Literal &literal) override {
            emit_call(literal, (void *) exec_literal);
        }

        void visit_symbol(const AST::Symbol &symbol) override {
            emit_call(symbol, (void *) exec_symbol);
        }

        void visit_builtin(const AST::Builtin &builtin) override {
            emit_call(builtin, (void *) exec_builtin);
        }

        void visit_let(const AST::Let &let) override {
            let.expression_->accept(*this);
            emit_call(let, (void *) exec_let);
        }

        void visit_local(const AST::Local &local) override {
            emit_call(local, (void *) exec_local);
        }

        void visit_global(const AST::Global &global) override {
            emit_call(global, (void *) exec_global);
        }

        void visit_do(const AST::Do &_do) override {
            auto last = _do.statements_->end() - 1;
            for (auto it = _do.statements_->begin(); it != _do.statements_->end(); it++) {
                (*it)->accept(*this);
                //ignore output of expression, but not last, which is the value of the
                //do expression
                if (it != last) {
                    emit_call(_do, (void *) exec_pop);
                }
            }
        }

        void visit_branch(const AST::Branch &branch) override {
            auto false_branch_label = x64.new_label();
            auto end_label = x64.new_label();
            branch.condition_->accept(*this);
            emit_call(branch, (void *) exec_bool);
            //rax is now 0 or 1
            x64.test_rax_rax();
            x64.jz(false_branch_label);
            branch.trueBranch_->accept(*this);
            x64.jmp_rel(end_label);
            x64.bind(false_branch_label);
            branch.falseBranch_->accept(*this);
            x64.bind(end_label);
        }

        void visit_return(const AST::Return &return_) override {
            assert(return_label != 0);
            return_.expression_->accept(*this);
            x64.jmp_rel(return_label);
            //defers are handled just after return_label
        }

        void visit_recur(const AST::Recur &recur) override {
            assert(current_function_ != nullptr);
            assert(recur_label != 0);
            auto exec_recur_label = x64.new_label();

            for (auto const &argument : *recur.arguments_) {
                argument->accept(*this);
            }

            //defer processing
            emit_call(recur, (void *) exec_check_defers);
            x64.test_rax_rax(); //test if defers processing needed
            x64.jz(exec_recur_label);
            current_function_->exec_defers().accept(*this);
            emit_call(*current_function_, (void *) exec_pop);

            x64.bind(exec_recur_label);
            emit_call(recur, (void *) exec_recur);
            x64.jmp_rel(recur_label);
        }

        void visit_function(const AST::Function &function) override {
            emit_call(function, (void *) exec_function);
        }

        //The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, and R9
        void visit_apply(const AST::Apply &apply) override {
            assert(exit_thunk != nullptr);
            assert(current_function_ != nullptr ? return_label != 0 : true);

            auto apply_label = x64.new_label();
            auto check_return_label = x64.new_label();
            auto end_label = x64.new_label();
            auto exit_label = x64.new_label();

            //function application
            //evaluate function expression, which evaluates to a callable
            apply.callable_->accept(*this);
            //and its arguments
            for (auto const &argument : *apply.arguments_) {
                argument->accept(*this);
            }

            //call the target method impl first
            x64.bind(apply_label);
            x64.mov_rdi_rbx(); //fibre 1st arg
            x64.mov_rsi_imm(reinterpret_cast<int64_t>(&apply));
            x64.mov_rax_mem(reinterpret_cast<uintptr_t >(&apply.target_)); 
            x64.call_rax();

            //actual target returned int in rax, normally 0, in which case
            //we continue. note that this jump is really predictable, because
            //it is almost always 0, (other cases indicate exit or block or exception)
            x64.test_rax_rax();
            x64.jz(end_label); //normal done, rax == 0, really done
            x64.js(apply_label); //bad dispatch, rax < 0, apply target was updated try again
            x64.jmp_rel(check_return_label); //any other issue
            x64.bind(exit_label);
            //rdi will be filled in by exit_thunk itself
            x64.mov_rsi_imm(reinterpret_cast<int64_t>(&apply));
            x64.mov_rdx_rax(); //exit code
            x64.mov_rax_imm(reinterpret_cast<int64_t>(exit_thunk));
            x64.call_rax(); //exit_thunk will save this code address so that later code can resume hereafter:
            x64.bind(check_return_label);
            //we continue here after normal call, or when we are resumed from exit
            //rax = 0, normal return
            //rax = 1, backout of current function (but not current module)
            //rax > 1, exit jit
            x64.cmp_rax_1();
            x64.js(end_label); //normal return rax = 0
            if(current_function_ != nullptr) {
                //immediate return from current func if rax == 1
                x64.jz(return_label); //jz is same as je
            }
            else {
                //for module code we cannot jump out of function (TODO remove module code)
                x64.jz(end_label);
            }
            //must be exit then
            x64.jmp_rel(exit_label);
            x64.bind(end_label);
        }

        //The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, and R9
        void compile(const AST::Function &function) {
            assert(return_label == 0);
            assert(recur_label == 0);
            assert(current_function_ == nullptr);
            current_function_ = &function;

            auto exit_label = x64.new_label();
            auto epilog_label = x64.new_label();
            return_label = x64.new_label();
            recur_label = x64.new_label();

            //at entry:
            //rdi is pointing to fiber (as is rbx)
            //rsi is pointing to apply
            //on entry, so they are already correct for calling exec_function_prolog
            x64.mov_rdx_imm(reinterpret_cast<int64_t>(&function)); //&function as 3d arg
            x64.mov_rcx_rsp_ptr(); //pass return address as 4th arg
            x64.sub_rsp_8(); //align stack before call
            x64.mov_rax_imm(reinterpret_cast<int64_t>(exec_function_prolog));
            x64.call_rax();
            x64.add_rsp_8(); //align stack after call
            x64.test_rax_rax(); //test for bad dispatch
            x64.js(exit_label); //rax < 0, bad dispatch detected, return immediately
            x64.add_rsp_8(); //pop return address so stack stays flat

            x64.bind(recur_label); //if we recur, function prolog is skipped and we return here

            emit_call(function, (void *) exec_function_checkpoint); //checks for gc etc

            function.expression_->accept(*this); //the body of the function

            x64.bind(return_label);

            emit_call(function, (void *) exec_check_defers);
            x64.test_rax_rax(); //test if defers processing needed
            x64.jz(epilog_label);

            //defers processing
            function.exec_defers().accept(*this);
            emit_call(function, (void *) exec_pop);

            x64.bind(epilog_label);            
            //call function epilog
            x64.sub_rsp_8(); //make room to return link directly in stack
            x64.mov_rdx_rsp(); //3d arg, pass stackpointer so we can return link direcly on stack
            x64.mov_rdi_rbx(); //1st arg, pass current fiber 
            x64.mov_rsi_imm(reinterpret_cast<int64_t>(&function)); //2nd arg, ptr to function
            x64.mov_rax_imm(reinterpret_cast<int64_t>(exec_function_epilog));
            x64.sub_rsp_8(); //align
            x64.call_rax();
            x64.add_rsp_8(); //align
            //link was put on stack by exec_function_epilog, retval in rax also by exec_function_epilog
            x64.bind(exit_label);
            x64.ret();

            return_label = 0;
            recur_label = 0;
            current_function_ = nullptr;
        }

        std::vector<uint8_t> make() {
            return x64.make();
        }

        void dump() {
            return x64.dump();
        }

    };


    class Compiler::Impl {
        std::mutex lock_;

        //executable memory for jit compiler
        size_t code_mem_size_;
        uint8_t *code_mem_;
        uint8_t *code_mem_ptr_;

        std::unordered_set<const AST::Function *> compiling_;
        std::condition_variable compiling_cond_;

        void *exit_thunk_;

        //need lock
        uint8_t *make_executable(const std::vector<uint8_t> &code) {
            //copy machine code into executable memory
            //TODO handle out of memory (e.g. allocate new chunks)
            assert((code_mem_size_ - ((code_mem_ptr_ - code_mem_))) >= code.size());

            std::copy(code.begin(),
                      code.end(),
                      code_mem_ptr_);

            auto const executable_code = code_mem_ptr_;

            code_mem_ptr_ += align<8>(code.size());

            return executable_code;
        }

        std::vector<uint8_t> compile(const AST::Function &function) {
            //std::cerr << "compile func" << std::endl;
            assert(exit_thunk_);

            X64Backend c(exit_thunk_);

            c.compile(function);

            auto result = c.make();

            //std::cerr << "compiled func:" << function.name_ << std::endl;
            //c.dump();

            return result;
        }

    public:
        entry_thunk_t entry_thunk_;
        reentry_thunk_t reentry_thunk_;

        explicit Impl() : code_mem_size_(1024 * 4096) {

            if (posix_memalign(reinterpret_cast<void **>(&code_mem_), 4096, code_mem_size_)) {
                throw std::runtime_error("could not allocate code memory");
            }

            if (mprotect(code_mem_, code_mem_size_, PROT_READ | PROT_WRITE | PROT_EXEC)) {
                throw std::runtime_error("could not make code memory executable");
            }

            code_mem_ptr_ = reinterpret_cast<uint8_t *>(code_mem_);

            {
                X64Assembler x64;
                x64.entry_thunk();
                entry_thunk_ = reinterpret_cast<entry_thunk_t>(make_executable(x64.make()));
            }

            {
                X64Assembler x64;
                x64.reentry_thunk();
                reentry_thunk_ = reinterpret_cast<reentry_thunk_t>(make_executable(x64.make()));
            }

            {
                X64Assembler x64;
                x64.exit_thunk(reinterpret_cast<int64_t>(&exec_exit));
                exit_thunk_ = reinterpret_cast<void *>(make_executable(x64.make()));
            }

        }

        ~Impl() {
            if (code_mem_ != nullptr) {
                free(code_mem_);
            }
        }

        //call without lock
        MethodImpl
        code(const AST::Function &function) {
            if (function.code_ == nullptr) {
                std::unique_lock<std::mutex> unique_lock(lock_);
                if (compiling_.count(&function)) {
                    //somebody already compiling it, wait for compile to finish
                    compiling_cond_.wait(unique_lock, [&]() {
                        return function.code_ != nullptr;
                    });
                } else {
                    //compile it myself
                    compiling_.insert(&function);
                    unique_lock.unlock(); //todo use unlock guard
                    auto compiled_code = compile(function);
                    unique_lock.lock();
                    const_cast<AST::Function &>(function).code_ =
                            reinterpret_cast<MethodImpl>(make_executable(compiled_code));
                    compiling_.erase(&function);
                    //notify others who might be waiting for this compile
                    compiling_cond_.notify_all();
                }
            }
            return function.code_;
        }

    };

    Compiler::Compiler()
        : impl_(std::make_unique<Compiler::Impl>()) {

        {
        }

    }

    Compiler::~Compiler() {
    }


    MethodImpl Compiler::code(const AST::Function &function)
    {
        return impl_->code(function);
    }

    uint64_t Compiler::enter(Fiber *fbr, const AST::Apply *apply, MethodImpl code)
    {
       return impl_->entry_thunk_(fbr, apply, code);
    }
        
    uint64_t Compiler::reenter(Fiber *fbr, void *ip, int64_t ret_code)
    {
       return impl_->reentry_thunk_(fbr, ip, ret_code);
    }

}