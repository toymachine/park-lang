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

#include "fiber.h"
#include "closure.h"
#include "builtin.h"
#include "exec.h"
#include "namespace.h"
#include "type.h"
#include "boolean.h"
#include "list.h"
#include "error2.h"
#include "compiler.h"

#include <unordered_set>

namespace park {

    gc::ref<Value> SLEEP;
    gc::ref<Value> EXIT;
    gc::ref<Value> SPAWN;
    gc::ref<Value> DEFER;

    class FiberImpl : public SharedValueImpl<Fiber, FiberImpl> {

    private:

 
    public:
        Runtime &runtime;

        const bool is_main; //can live in runtime?

        struct frame_stack_t {
            const AST::Apply *apply; //callsite
            size_t base; //position of current callable in value stack
            size_t argument_count; //argument count to current callable
            size_t local_count;
            gc::ref<List> defers; //list of defers we need to apply on function exit
        };

        std::vector<frame_stack_t> frame_stack;
        std::vector<void *> link_stack; //return address from current function, TODO put back in framestack
        std::unique_ptr<gc::private_heap_t> private_heap_;

        int checkpoint_ = 0;

        FiberImpl(Runtime &runtime, bool is_main) :
                runtime(runtime),
                is_main(is_main),
                private_heap_(std::make_unique<gc::private_heap_t>()) {
        }

        ~FiberImpl()
        {
            assert(is_main ? true : private_heap_->chunks_.empty());
        }

        static void init(Runtime &runtime);

        void roots(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept);

        using post_exit_callback_cc_resume_t = std::function<bool(Fiber &fbr)>;
  
        post_exit_callback_cc_resume_t post_exit_callback_cc_resume;

        std::function<int()> post_exit(int exit_code, std::function<int()> f);

        void stack_trace();

        void dump_stack() {
            stack.each([&](auto &item) {
                std::cerr << "kind: " << (int) item.kind << " val: "; 
                switch(item.kind) {
                    case value_t::kind_t::RVALUE: {
                        assert(item.rvalue);
                        item.rvalue->repr(*this, std::cerr);
                    }
                    default: {
                        
                    }                    
                }
                std::cerr << std::endl;
            });
        }

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
        }

        static int64_t _sleep(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            int64_t milliseconds;

            return frame.check().
               static_dispatch(*SLEEP).
               argument_count(1).
               argument<int64_t>(1, milliseconds).
               cc_resume([milliseconds](Fiber &fbr) -> bool {
                   from_fbr(fbr).sleep(milliseconds);
                   return false; //block
               });
        }

        void __spawn(gc::ref<Closure> closure)
        {
            gc::make_shared(allocator(), closure);
    
            auto fbr = Fiber::create(allocator(), runtime, false);
            {
                std::lock_guard<std::mutex> lock_guard(runtime.lock);
                runtime.fiber_created(fbr);
            }
    
            runtime.run(*fbr.mutate(), closure);
        }

        static int64_t _spawn(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Closure> closure;

            return frame.check().
                static_dispatch(*SPAWN).
                argument_count(1).
                argument<Closure>(1, closure).
                result<Value>([&]() {
                    from_fbr(fbr).__spawn(closure);
                    return closure;
               });
        }

        void __defer(gc::ref<Closure> closure)
        {
            frame_stack.back().defers = defers()->conj(*this, closure);
        }

        static int64_t _defer(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Closure> closure;

            return frame.check().
                static_dispatch(*DEFER).
                argument_count(1).
                argument<Closure>(1, closure).
                result<Value>([&]() {
                    from_fbr(fbr).__defer(closure);
                    return closure;
               });
        }

         static int64_t _exit(Fiber &fbr, const AST::Apply &apply) {
            return 2;
        }

        const Closure &current_closure();

        void attach_and_exec(const std::function<int()> f);

        int resume(void *ip, int64_t ret_code);
        void resume_async(std::function<void(Fiber &fbr)> f, int64_t ret_code) override;
        void resume_sync(std::function<void(Fiber &fbr)> f, int64_t ret_code) override;

        void sleep(int milliseconds);

        void enqueue(const std::function<int()> f) override;

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "(fiber)";
        }

        bool to_bool(Fiber &fbr) const override {
            return true;
        }

        static FiberImpl &from_fbr(Fiber &fbr) {
            return static_cast<FiberImpl &>(fbr); //this must be static because of speed
        }

        Runtime &get_runtime() {
            return runtime;
        }

        void attach(gc::allocator_t &allocator) override
        {
            allocator_ = &allocator;
            std::swap(private_heap_, allocator_->private_heap_);
        }

        void detach(gc::allocator_t &allocator) override
        {
            std::swap(allocator_->private_heap_, private_heap_);
            allocator_ = nullptr;
        } 

        void exec_literal(const AST::Literal &literal) {
            stack.push_back(literal.value_);
        }

        //TODO get rid of this
        gc::ref<Value> local_by_index(size_t local_index) {
            assert(!frame_stack.empty());
            /*
            size_t idx = frame_stack.back().base + local_index;
            assert(idx >= 0 && idx < stack.size());
            return value::cast<gc::ref<Value>>(*this, (*stack)[idx]);
            */
            return value::cast<gc::ref<Value>>(*this, stack.local(frame_stack.back().base, local_index));

        }

        //TODO get rid of this
        std::optional<gc::ref<Value>> lookup(const AST::Symbol &symbol) {
            if (!frame_stack.empty()) {

                auto const &closure = current_closure();

                //check current lambda ref
                if (symbol.namei_ == runtime.LAMBDA_NAMEI) {
                    return gc::ref<Value>(&closure);
                }

                if (symbol.namei_ == runtime.DEFERS_NAMEI) {
                    return gc::ref_cast<Value>(defers());
                }

                //check locals
                if (auto local_index = closure.function().local_index(symbol.namei_)) {
                    //std::cout << "loc by index: " << *local_index << std::endl;
                    return local_by_index(*local_index);
                }

                //check freevars
                if (auto freevar = closure.lookup(symbol.namei_)) {
                    //std::cout << "loc by freevar: " << std::endl;
                    return *freevar;
                }

            }

            //check builtins
            //TODO get rid of lookup and the find_builtin with int
            if (auto builtin = runtime.find_builtin(symbol.namei_)) {
                return *builtin;
            }

            //not found
            return std::nullopt;
        }

        //TODO get rid of this
        void exec_symbol(const AST::Symbol &symbol) {

            if (auto value = lookup(symbol)) {
                stack.push<gc::ref<Value>>(*value);
            } else {
                throw Error::symbol_not_found(*this, symbol.name_);
            }
        }

        void exec_local(const AST::Local &local) {
            assert(!frame_stack.empty());
            stack.push_local(frame_stack.back().base, local.index_);
        }

        void exec_builtin(const AST::Builtin &builtin) {
            stack.push_back(builtin.value_);
        }

        void exec_global(const AST::Global &global) {
            if (!global.initialized_.load()) {
                const_cast<AST::Global &>(global).initialize();
            }   
            assert(global.value_);
            stack.push<gc::ref<Value>>(global.value_);

        }

        void exec_let(const AST::Let &let) {
            assert(!frame_stack.empty());
            if (auto local_index = current_closure().function().local_index(let.symbol_->namei_)) {
                stack.set_local(frame_stack.back().base, *local_index);
            } else {
                throw Error::symbol_not_found(*this, let.symbol_->name_);
            }
        }

        void exec_pop(const AST::Node &node) {
            assert(!stack.empty());
            stack.pop_back();
        }

        int64_t exec_bool(const AST::Branch &branch) {
            return stack.pop_cast<bool>(*this);
        }

        int64_t exec_dispatch(const AST::Apply &apply) {
            auto const callable = value::cast<gc::ref<Value>>(
                *this, stack.callable(stack.base(apply.argument_count())));
            const_cast<AST::Apply &>(apply).set_target(callable->dispatch(*this, apply));
            return -1;
        }

        void exec_recur(const AST::Recur &recur);

        void exec_function(const AST::Function &function) {

            auto closure = Closure::create(*this, gc::ref<AST::Function>(&function), function.freevars_->size());

            for (auto freevar : *function.freevars_) {
                auto symbol = gc::ref_dynamic_cast<AST::Symbol>(freevar);
                if (auto value = lookup(*symbol)) {   
                    if(auto index = function.freevar_index(symbol->namei_)) {
                        closure.mutate()->set(*index, *value);
                    }
                    else {
                        throw std::runtime_error("freevar should have index");
                    }
                }
                else {
                    throw std::runtime_error("could not lookup freevar!");
                }
            }


            stack.push<gc::ref<Value>>(closure);
        }

        int64_t exec_check_defers(const AST::Node &node) {
            return bool(frame_stack.back().defers);
        }

        int64_t exec_function_prolog(const AST::Apply &apply, const AST::Function &function, void *link) {
            auto argument_count = apply.arguments_->size();
            auto local_count = function.local_count();

            //assert(stack.size() >= (argument_count + 1));
            auto base = stack.base(argument_count);

            auto &callable = stack.callable(base);

            assert(callable.kind == value_t::kind_t::RVALUE);

            if (!Closure::isinstance(*callable.rvalue)) {
                return exec_dispatch(apply); //bad dispatch, might be some other type of callable
            }

            auto const &closure = static_cast<const Closure &>(*callable.rvalue);
            if (&closure.function() != &function) {
                return exec_dispatch(apply); //its a closure, but for a different function, bad dispatch
            }

            frame_stack.push_back({       &apply,
                                          base,
                                          argument_count,
                                          local_count,
                                          nullptr,
                                  });
            link_stack.push_back(link);

            //push uninitialized locals
            stack.init_locals(local_count);

            return 0;
        }

        void exec_function_checkpoint(const AST::Function &function);

        int64_t exec_function_epilog(const AST::Function &function, void **link);

        void *exec_exit(const AST::Apply &apply, void *link);

        template<typename F>
        void *pop_frame(F &&f) {
            auto const frame = frame_stack.back();
            stack.pop_frame(frame.base);
            f(); //f pushes result
            auto link = link_stack.back();
            frame_stack.pop_back();
            link_stack.pop_back();
            return link;
        }


        void *alloc(std::size_t sz);

        gc::ref<List> defers() {
            assert(!frame_stack.empty());
            auto defers = frame_stack.back().defers;
            if(!defers) {
                defers = List::create(*this);
            }
            return defers;
        }
    };

    
    const Closure &
    FiberImpl::current_closure() {
        assert(!frame_stack.empty());
        auto const &current_frame = frame_stack.back();
        auto &callable = stack.callable(current_frame.base);
        assert(callable.kind == value_t::kind_t::RVALUE);
        assert(Closure::isinstance(*callable.rvalue));
        return static_cast<const Closure &>(*callable.rvalue);
    }


    //call with lock
    std::function<int()>
    FiberImpl::post_exit(int exit_code, std::function<int()> f) {
        assert(exit_code >= 0 && exit_code <= 4);

        if (exit_code == 0 || exit_code == 1 || exit_code == 2) {

            //0 = normal exit, 1 = unhandled error, 2 = early exit

            auto self = gc::ref<Fiber>(this);

            if(exit_code == 1) {
                std::cerr << "exit with unhandled error!: ";
                auto &error = stack.back();
                assert(error.kind == value_t::kind_t::RVALUE);
                error.rvalue->repr(*this, std::cerr);
                std::cerr << std::endl;
            }

            stack.clear();

            if (is_main) {
                //std::cerr << "stk sz: " << stack.size() << std::endl;
                runtime.stop();
            }
            else {
                allocator().private_heap_->clear();
                runtime.fiber_exitted(self);
            }

        } else if (exit_code == 4) {
            //exit for cc and resume
            assert(post_exit_callback_cc_resume);
            auto resume_with_result = post_exit_callback_cc_resume(*this);
            post_exit_callback_cc_resume = nullptr;
            if(resume_with_result) {
                //TODO pop and push result as value_t 
                auto result = stack.pop_cast<gc::ref<Value>>(*this);
                //resume with result
                auto link_address = pop_frame([&]() {
                    stack.push<gc::ref<Value>>(result);
                });
                return [this, link_address]() -> int {
                    return resume(link_address, 0);
                };
            }
            else {
                return nullptr;
            }
        }
        else {
            throw std::runtime_error("unexpected exit code");
        }

        return nullptr;
    }


    void FiberImpl::stack_trace() {
        /*
        for (auto i = frame_stack.rbegin(); i != frame_stack.rend(); ++i) {
            Frame frame(stack, stack.begin() + (*i).base);
            std::cerr << "# " << std::distance(i, frame_stack.rend()) - 1;
            std::cerr << "\t";
            frame.argument<const Value &>(*this, 0).repr(*this, std::cerr);
            std::cerr << "(";
            for (auto j = 1; j <= (*i).argument_count; j++) {
                frame.argument<const Value &>(*this, j).repr(*this, std::cerr);
                if (j < (*i).argument_count) {
                    std::cerr << ", ";
                }
            }
            std::cerr << ")" << std::endl;
        }
        */
    }

    int FiberImpl::resume(void *ip, int64_t ret_code)
    {
        return runtime.compiler().reenter(this, ip, ret_code);
    }

    //move to runtime TODO 
    void FiberImpl::enqueue(std::function<int()> f) {
        runtime.io_service.post([f, this]() mutable {
            attach_and_exec(f);
        });
    }

    //call without lock
    void FiberImpl::resume_async(std::function<void(Fiber &fbr)> f, int64_t ret_code) {
        auto link_address = pop_frame([&]() {
            f(*this);
        });
        enqueue([this, link_address, ret_code]() {
            return resume(link_address, ret_code);
        });
    }

    //call without lock
    void FiberImpl::resume_sync(std::function<void(Fiber &fbr)> f, int64_t ret_code) {
        attach_and_exec([&]() {
            return resume(pop_frame([&]() {
               f(*this);
            }), ret_code);
        });
    }

    //call without lock
    void FiberImpl::attach_and_exec(std::function<int()> f) 
    {
        std::unique_lock<std::mutex> lock(runtime.lock);
          
        assert(this->allocator_ == nullptr);
        runtime.fiber_attach(lock, gc::ref<Fiber>(this));  
        assert(this->allocator_ != nullptr);
     
        //trampoline
        auto exit_code = 0;
        do {
            std::unlock_guard<std::unique_lock<std::mutex>> unlock_guard(lock);
            exit_code = f();
        }
        while((f = post_exit(exit_code, f))); 

        assert(this->allocator_ != nullptr);
        runtime.fiber_detach(lock, gc::ref<Fiber>(this));
        assert(this->allocator_ == nullptr);
    }

    void FiberImpl::roots(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept)
    {
        stack.each([&](auto &item) {
            if(item.kind == value_t::kind_t::RVALUE) {
                gc::ref<Value> ref(item.rvalue);
                accept(ref);
                item.rvalue = ref.mutate();
            }
        });
        for(auto &item : frame_stack) {
            if(item.defers) {
                accept(item.defers);
            }
        }
    }

    void FiberImpl::exec_function_checkpoint(const AST::Function &function) {
        assert(allocator_ != nullptr);
        if(++checkpoint_ % 256 == 0) {
            auto &collector = runtime.collector();
            if(collector.stw_mutators_wait.load()) 
            {
                std::unique_lock<std::mutex> lock(runtime.lock);
                collector.checkin_shared(*allocator_, lock);
            }
            collector.checkin_local(*allocator_, [&](auto accept) {
                roots(accept);
            });
        }
    }

    int64_t FiberImpl::exec_function_epilog(const AST::Function &function, void **link) {
        assert(frame_stack.back().apply != nullptr);
        auto &apply = *frame_stack.back().apply;
        auto result = stack.back();
        stack.pop_back();
        stack.pop(function.local_count());
        *link = pop_frame([&]() {
            stack.push_back(result);
        }); 
        //might be Error, TODO have actual throw stmt and flag in other way, removeing the call to is_error
        if(result.kind == value_t::kind_t::RVALUE && Error2::is_error(*result.rvalue) && apply.throws_) {
            return 1;
        }
        else {
            return 0;
        }
    }

    void FiberImpl::exec_recur(const AST::Recur &recur) {

        assert(!frame_stack.empty());
        auto &current_frame = frame_stack.back();
        assert(recur.arguments_->size() == current_frame.argument_count);
        auto const local_count = current_frame.local_count;
        auto const argument_count = current_frame.argument_count;

        stack.recur(argument_count, local_count);

        current_frame.defers = nullptr;

    }

    void *FiberImpl::exec_exit(const AST::Apply &apply, void *link) {

        // called when jumping out of jitted code
        // pushes the frame that is normally not used for builtins

        auto argument_count = apply.arguments_->size();

        frame_stack.push_back({&apply, stack.base(argument_count), argument_count, 0, nullptr});
        link_stack.push_back(link);

        return link_stack[0]; //return the address to jump to to perform the exit, which is the link of the top-most frame
    }

    //called under lock
    void FiberImpl::sleep(int milliseconds) {
        assert(milliseconds >= 0);
        //TODO check that sleep is not already set, or check that sleep is only called from own running fiber
        //TODO can we do this without heap alloc?
        auto sleep_timer = std::make_unique<boost::asio::deadline_timer>(runtime.io_service);
        sleep_timer->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        sleep_timer->async_wait([this, _ = std::move(sleep_timer)](const boost::system::error_code &ec) {
            //TODO check error code
            resume_sync([&](Fiber &fbr) {
                stack.push<bool>(true);
            }, 0);
        });
    }

    //not forget to call fiber_created on runtime
    Fiber::Fiber() 
    {
    }

    Fiber::~Fiber()
    {
        assert(stack.size() == 0);
        assert(color_ == nullptr);
    }

    gc::ref<Fiber> Fiber::create(gc::allocator_t &allocator, Runtime &runtime, bool is_main) {
        return gc::make_shared_ref<FiberImpl>(allocator, runtime, is_main);
    }

    void Fiber::init(Runtime &runtime) {
        FiberImpl::init(runtime);
    }

    int64_t Frame::cc_resume(std::function<bool(Fiber &fbr)> f) {
        auto &fiber = FiberImpl::from_fbr(fbr_);
        fiber.post_exit_callback_cc_resume = std::move(f);
        return 4;
    }


    int64_t Frame::bad_dispatch() {
        return FiberImpl::from_fbr(fbr_).exec_dispatch(apply_);
    }

    int64_t Frame::exception(const std::string what) {
        auto &fiber = FiberImpl::from_fbr(fbr_);
        std::cerr << "exception: " << what << " in function:" << std::endl;
        fiber.stack_trace();
        return 3;
    }

    Runtime &Runtime::from_fbr(Fiber &fbr) {
        return FiberImpl::from_fbr(fbr).runtime;
    }

    void Fiber::roots(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept)
    {
        FiberImpl::from_fbr(*this).roots(accept);
    }

// interface with compiler (eg. impl of exec.h)

    void exec_literal(Fiber &fbr, const AST::Literal &literal) {
        FiberImpl::from_fbr(fbr).exec_literal(literal);
    }

    void exec_symbol(Fiber &fbr, const AST::Symbol &symbol) {
        FiberImpl::from_fbr(fbr).exec_symbol(symbol);
    }

    void exec_let(Fiber &fbr, const AST::Let &let) {
        FiberImpl::from_fbr(fbr).exec_let(let);
    }

    void exec_local(Fiber &fbr, const AST::Local &local) {
        FiberImpl::from_fbr(fbr).exec_local(local);
    }

    void exec_global(Fiber &fbr, const AST::Global &global) {
        FiberImpl::from_fbr(fbr).exec_global(global);
    }

    void exec_builtin(Fiber &fbr, const AST::Builtin &builtin) {
        FiberImpl::from_fbr(fbr).exec_builtin(builtin);
    }

    void exec_pop(Fiber &fbr, const AST::Node &node) {
        FiberImpl::from_fbr(fbr).exec_pop(node);
    }

    int64_t exec_bool(Fiber &fbr, const AST::Branch &branch) {
        return FiberImpl::from_fbr(fbr).exec_bool(branch);
    }

    void exec_recur(Fiber &fbr, const AST::Recur &recur) {
        return FiberImpl::from_fbr(fbr).exec_recur(recur);
    }

    void exec_function(Fiber &fbr, const AST::Function &function) {
        FiberImpl::from_fbr(fbr).exec_function(function);
    }

    int64_t exec_function_prolog(Fiber &fbr, const AST::Apply &apply, const AST::Function &function, void *link) {
        return FiberImpl::from_fbr(fbr).exec_function_prolog(apply, function, link);
    }

    void exec_function_checkpoint(Fiber &fbr, const AST::Function &function) {
        FiberImpl::from_fbr(fbr).exec_function_checkpoint(function);
    }

    int64_t exec_function_epilog(Fiber &fbr, const AST::Function &function, void **link) {
        return FiberImpl::from_fbr(fbr).exec_function_epilog(function, link);
    }

    void *exec_exit(Fiber &fbr, const AST::Apply &apply, void *link) {
        return FiberImpl::from_fbr(fbr).exec_exit(apply, link);
    }

    int64_t exec_check_defers(Fiber &fbr, const AST::Node &node) {
        return FiberImpl::from_fbr(fbr).exec_check_defers(node);
    }

    int64_t exec_dispatch(Fiber &fbr, const AST::Apply &apply) {
        return FiberImpl::from_fbr(fbr).exec_dispatch(apply);
    }

    AST::Apply::Apply(size_t line, gc::ref<Node> callable, gc::ref<NodeList> arguments)
        : target_(exec_dispatch), line_(line), callable_(callable), arguments_(arguments) {}


    void FiberImpl::init(Runtime &runtime) {
        TYPE = runtime.create_type("Fiber");

        /*
        std::cerr << "sz Fiber: " << sizeof(FiberImpl) << std::endl;
        std::cerr << "sz Stack: " << sizeof(Fiber::stack_t) << std::endl;
        std::cerr << "sz private_heap_t: " << sizeof(gc::private_heap_t) << std::endl;
        std::cerr << "sz value_t: " << sizeof(value_t) << std::endl;
        std::cerr << "sz frame_stack_t: " << sizeof(frame_stack_t) << std::endl;
        std::cerr << "sz block_t: " << sizeof(gc::block_t) << std::endl;
        std::cerr << "sz post_exit_callback_cc_resume_t: " << sizeof(post_exit_callback_cc_resume_t) << std::endl;
        */
        
        SLEEP = runtime.create_builtin<BuiltinStaticDispatch>("sleep", _sleep);
        EXIT = runtime.create_builtin<BuiltinStaticDispatch>("exit", _exit);
        SPAWN = runtime.create_builtin<BuiltinStaticDispatch>("spawn", _spawn);
        DEFER = runtime.create_builtin<BuiltinStaticDispatch>("defer", _defer);

    }

}