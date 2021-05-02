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

#include <chrono>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <condition_variable>
#include <queue>
#include <fstream>
#include <random>
#include <numeric>
#include <stack>

#include "runtime.h"

#include "vector.h"

#include "fiber.h"
#include "closure.h"
#include "compiler.h"
#include "builtin.h"
#include "map.h"
#include "integer.h"
#include "string.h"
#include "boolean.h"
#include "type.h"
#include "channel.h"
#include "atom.h"
#include "lexer.h"
#include "pack.h"
#include "struct.h"
#include "namespace.h"
#include "align.h"
#include "http.h"
#include "intern.h"
#include "error2.h"
#include "list.h"
#include "mod_random.h"
#include "reader.h"
#include "symbol.h"

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace park {


    struct worker_t {
        std::thread thread_;
        std::unique_ptr<gc::allocator_t> allocator_;
    };

    thread_local gc::allocator_t *current_allocator_;

    class Loader;

    class RuntimeImpl : public Runtime {
    friend class Loader;

    private:

        Interns interns_;
        std::mutex interns_lock_;

        std::vector<worker_t> workers_;

        std::unique_ptr<Compiler> compiler_;

        std::unordered_map<size_t, gc::ref<Value>> builtins_;
        std::unordered_map<std::string, gc::ref<Namespace>> modules_;
        std::unordered_map<std::string, gc::ref<Type>> types_;

        std::vector<gc::ref<gc::collectable>> roots_;

        gc::ref<AST::Apply> bootstrap_apply_0_;
        gc::ref<AST::Apply> bootstrap_apply_1_;
        gc::ref<AST::Apply> bootstrap_apply_2_;

        gc::ref<Namespace> load_main_module(const std::string &path, const std::string &name);
        gc::ref<Namespace> load_boot_module(std::istream &ins, const std::string &name);

        gc::ref<Namespace> load_prelude();
        gc::ref<Namespace> load_compiler();
        void compile(Fiber &fbr, const fs::path &path);

        static int64_t _read(Fiber &fbr, const AST::Apply &apply);
        static int64_t _resolve(Fiber &fbr, const AST::Apply &apply);

        gc::collector_t collector_;
        std::unique_ptr<gc::allocator_t> allocator_;

        gc::ref<Fiber> main_fiber_;


        FiberList fibers_0_;
        FiberList fibers_1_;
        FiberList fibers_2_;
        FiberList fibers_3_;

        FiberList *fibers_running_;
        FiberList *fibers_sleeping_grey_;
        FiberList *fibers_sleeping_black_;
        FiberList *fibers_sleeping_scanning_;

        std::condition_variable is_fiber_sleeping_black_cond_;

        boost::asio::signal_set signals_;

    public:

        RuntimeImpl();

        RuntimeImpl(int argc, char *argv[]);

        virtual ~RuntimeImpl();

        gc::allocator_t &allocator() override
        {
            assert(allocator_);
            return *allocator_;
        }

        gc::collector_t &collector() override
        {
            return collector_;
        }

        Compiler &compiler() override
        {
            assert(compiler_);
            return *compiler_;
        }

        void add_root(gc::ref<gc::collectable> ref) override
        {
            roots_.push_back(ref);
        }

        Fiber &main_fiber() 
        {
            assert(main_fiber_);
            return *main_fiber_.mutate();
        }

        size_t intern(const std::string &s) override;
        std::string name(size_t namei) override;

        void run(const std::string &path) override;
        void run(Fiber &fbr, const AST::Apply &apply, MethodImpl code);
        void run(Fiber &fbr, gc::ref<Closure> closure) override;

        void stop() override;
        void quit() override;

        gc::ref<Type> create_type(std::string name) override;

        void register_builtin(std::string name, gc::ref<Value> builtin) override;
        void register_method(gc::ref<Value> builtin, const Type &self, MethodImpl impl) override;        
        void register_method(gc::ref<Value> builtin, const Type &lhs, const Type &rhs, MethodImpl impl) override;
        void register_method(gc::ref<Value> builtin, value_t::kind_t lhs, value_t::kind_t rhs, MethodImpl impl) override;
        void register_method(gc::ref<Value> builtin, value_t::kind_t lhs, const Type &rhs, MethodImpl impl) override;
        void register_method(gc::ref<Value> builtin, const Type &lhs, value_t::kind_t rhs, MethodImpl impl) override;     

        std::optional<gc::ref<Value>> find_builtin(size_t namei) override;
        std::optional<gc::ref<Value>> find_builtin(const std::string &name) override;
        gc::ref<Value> builtin(const std::string &name) override;

        void fiber_created(gc::ref<Fiber> f) override;

        void fiber_exitted(gc::ref<Fiber> f) override;

        void fiber_attach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) override;

        void fiber_detach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) override;

    };


    RuntimeImpl::RuntimeImpl()
            : workers_(std::thread::hardware_concurrency() * 2),
              compiler_(std::make_unique<Compiler>()),
              collector_(lock),
              allocator_(std::make_unique<gc::allocator_t>(collector_)),
              fibers_running_(&fibers_0_),
              fibers_sleeping_grey_(&fibers_1_), 
              fibers_sleeping_black_(&fibers_2_),
              fibers_sleeping_scanning_(&fibers_3_),
              signals_(io_service)
    { 
        /*
        signals_.add(SIGINT);
        signals_.add(SIGTERM);
#if defined(SIGQUIT)
        signals_.add(SIGQUIT);
#endif // defined(SIGQUIT)

        signals_.async_wait(boost::bind(&RuntimeImpl::quit, this));
        */

        LAMBDA_NAMEI = intern("__lambda__");
        DEFERS_NAMEI = intern("__defers__");
        APPLY_DEFERS_NAMEI = intern("__apply_defers__");
        
        Type::init(*this);
        Builtin::init(*this);
        Fiber::init(*this);
        Namespace::init(*this);
        Closure::init(*this);
        Integer::init(*this);
        String::init(*this);
        Map::init(*this);
        Vector::init(*this);
        List::init(*this);
        Boolean::init(*this);
        Error2::init(*this);
        Channel::init(*this);
        Atom::init(*this);
        Struct::init(*this);
//        Range::init(*this);
        Symbol::init(*this);
        Lexer::init(*this);
        Reader::init(*this);
        pack::init(*this);
        http::init(*this);
        AST::init(*this);
        random::init(*this);

        main_fiber_ = Fiber::create(allocator(), *this, true);

        fiber_created(main_fiber_);

        bootstrap_apply_0_ = AST::Apply::create_boot_0(allocator());
        add_root(bootstrap_apply_0_);
        bootstrap_apply_1_ = AST::Apply::create_boot_1(allocator());
        add_root(bootstrap_apply_1_);
        bootstrap_apply_2_ = AST::Apply::create_boot_2(allocator());
        add_root(bootstrap_apply_2_);

        current_allocator_ = &allocator();
    }

    RuntimeImpl::RuntimeImpl(int argc, char *argv[])
            : RuntimeImpl() {

        auto &fbr = main_fiber();

        fbr.attach(allocator());

        auto arglist = Vector::create(fbr);

        for (int i = 1; i < argc; i++) {
            arglist = arglist->conj(fbr, String::create(fbr, argv[i]));
        }

        gc::make_shared(fbr.allocator(), arglist);

        builtins_[intern("argv")] = arglist;

        auto ns_prelude = load_prelude();
        gc::make_shared(fbr.allocator(), ns_prelude);
        modules_["__prelude__"] = ns_prelude;

        builtins_[intern("__read__")] = gc::make_shared_ref<BuiltinStaticDispatch>(fbr.allocator(), "__read__", _read);
        builtins_[intern("__resolve__")] = gc::make_shared_ref<BuiltinStaticDispatch>(fbr.allocator(), "__resolve__", _resolve);

        auto __builtins__ = Vector::create(fbr);

        for (const auto &n : interns_.left()) {
            if (builtins_.find(n.second) != builtins_.end()) {
                __builtins__ = __builtins__->conj(fbr, String::create(fbr, n.first));
            }
        }

        __builtins__ = __builtins__->conj(fbr, String::create(fbr, "__builtins__"));
        __builtins__ = __builtins__->conj(fbr, String::create(fbr, "__compile__"));

        //TODO would we not need write_barrier here?
        gc::make_shared(fbr.allocator(), __builtins__);

        builtins_[intern("__builtins__")] = __builtins__;

        auto ns_compiler = load_compiler();
        //TODO would we not need write_barrier here?
        gc::make_shared(fbr.allocator(), ns_compiler);
        modules_["__compiler__"] = ns_compiler;

        fbr.detach(allocator());

    }

    void
    RuntimeImpl::compile(Fiber &fbr, const fs::path &path) {

        auto path_prk = path;
        path_prk.replace_extension("prk");
        auto path_pck = path;
        path_pck.replace_extension("pck");

        if(!fs::exists(path_prk)) {
            throw std::runtime_error("cannot find: " + path_prk.string());
        }

        if(!fs::exists(path_pck) || fs::last_write_time(path_prk) > fs::last_write_time(path_pck)) {
            //compile
            auto ns = modules_["__compiler__"];
            auto __compile__ = ns->find(intern("__compile__"));
            assert(__compile__);

            fbr.stack.push<gc::ref<Value>>(*__compile__);
            fbr.stack.push<gc::ref<Value>>(String::create(fbr, path_prk.string()));
            fbr.stack.push<gc::ref<Value>>(String::create(fbr, path_pck.string()));

            run(fbr, *bootstrap_apply_2_, compiler_->code(static_cast<const Closure &>(**__compile__).function()));

        }

        //all good
    }

    int64_t
    RuntimeImpl::_resolve(Fiber &fbr, const AST::Apply &apply) {
        assert(false);
        return 0;
    }

    int64_t
    RuntimeImpl::_read(Fiber &fbr, const AST::Apply &apply) {
        assert(false);
        return 0;
    }


    RuntimeImpl::~RuntimeImpl() {}

    void
    RuntimeImpl::register_builtin(std::string name, gc::ref<Value> builtin) {
        auto const namei = intern(name);
        auto const found = builtins_.find(namei);

        if (found != builtins_.end()) {
            throw std::runtime_error("cannot redefine builtin: " + name);
        } else {
            builtins_[namei] = builtin;
        }
    }

    void RuntimeImpl::register_method(gc::ref<Value> builtin, const Type &self, MethodImpl impl) 
    {
        auto _builtin = gc::ref_dynamic_cast<BuiltinSingleDispatch>(builtin);
        _builtin.mutate()->register_method(self, impl);
    }

    void RuntimeImpl::register_method(gc::ref<Value> builtin, const Type &lhs, const Type &rhs, MethodImpl impl)
    {
        auto _builtin = gc::ref_dynamic_cast<BuiltinBinaryDispatch>(builtin);
        _builtin.mutate()->register_method(lhs, rhs, impl);
    }

    void RuntimeImpl::register_method(gc::ref<Value> builtin, value_t::kind_t lhs, value_t::kind_t rhs, MethodImpl impl)
    {
        auto _builtin = gc::ref_dynamic_cast<BuiltinBinaryDispatch>(builtin);
        _builtin.mutate()->register_method(lhs, rhs, impl);
    }

    void RuntimeImpl::register_method(gc::ref<Value> builtin, value_t::kind_t lhs, const Type &rhs, MethodImpl impl)
    {
        auto _builtin = gc::ref_dynamic_cast<BuiltinBinaryDispatch>(builtin);
        _builtin.mutate()->register_method(lhs, rhs, impl);
    }
        
    void RuntimeImpl::register_method(gc::ref<Value> builtin, const Type &lhs, value_t::kind_t rhs, MethodImpl impl)
    {
        auto _builtin = gc::ref_dynamic_cast<BuiltinBinaryDispatch>(builtin);
        _builtin.mutate()->register_method(lhs, rhs, impl);
    }

    /* need not hold gil before calling this after init cycle because it will be readonly */
    std::optional<gc::ref<Value>>
    RuntimeImpl::find_builtin(const std::string &name) {
        auto found = builtins_.find(intern(name));
        if (found != builtins_.end()) {
            return found->second;
        } else {
            return std::nullopt;
        }
    }

    gc::ref<Value> RuntimeImpl::builtin(const std::string &name)
    {
        auto found = find_builtin(name);
        if(!found) {
            throw std::runtime_error("builtin not found: " + name);
        }
        return *found;
    }

    /* TODO get rid of this by removing lookup from Fiber */
    std::optional<gc::ref<Value>>
    RuntimeImpl::find_builtin(size_t namei) {
        auto found = builtins_.find(namei);
        if (found != builtins_.end()) {
            return found->second;
        } else {
            return std::nullopt;
        }
    }

    //has own lock, no longer needed to own global lock
    size_t
    RuntimeImpl::intern(const std::string &s) {
        std::unique_lock<std::mutex> guard(interns_lock_);
        return interns_.intern(s);
    }

    std::string 
    RuntimeImpl::name(size_t namei) {
        std::unique_lock<std::mutex> guard(interns_lock_);
        return interns_.right().at(namei);
    }


    void
    RuntimeImpl::run(Fiber &fbr, const AST::Apply &apply, MethodImpl code) {

        fbr.detach(allocator());

        fbr.enqueue([&]() {
            return compiler_->enter(&fbr, &apply, code);
        });

        this->io_service.run();

        fbr.attach(allocator());

        this->io_service.restart();
    }

    class Loader : public AST::Visitor
    {
        RuntimeImpl &runtime_;
        Fiber &fbr_;
        gc::ref<Namespace> ns_;
        std::stack<std::pair<std::string, std::string>> todo_;
        std::unordered_set<std::string> visited_;

        int stage_ = 0;
    public:
        Loader(RuntimeImpl &runtime, Fiber &fbr) : runtime_(runtime), fbr_(fbr) {}

        void visit_struct(const AST::Struct &struct_) override {
            if(stage_ == 0) {
                fbr_.stack.push<gc::ref<Value>>(Struct::create(fbr_, struct_));
            }
        }

        void visit_import(const AST::Import &import) override {
            //std::cerr << "  visit import from: " << import.from_ << std::endl;            
            if(visited_.count(import.from_) == 0) {
                todo_.push({import.from_, import.from_});
            }

            if(stage_ == 1) {
                for (auto &imported_symbol : *import.imports_) {
                    auto sym = gc::ref_dynamic_cast<AST::Symbol>(imported_symbol);
                    //std::cerr << "link sym: " << sym->name_ << std::endl;

                    auto ns_from = runtime_.modules_[import.from_];

                    if(auto found = ns_from->find(sym->namei_)) {
                        //std::cerr << "found sym" << std::endl;
                        ns_.mutate()->define(sym->namei_, *found);
                    }
                    else {
                        throw std::runtime_error("could not find sym: " + sym->name_ + " in " + import.from_);
                    }
                }
            }
        }

        void visit_function(const AST::Function &function) override {
            if(stage_ == 0) {
                assert(function.freevars_->size() == 0);
                auto closure = Closure::create(fbr_, gc::ref<AST::Function>(&function), 0);
                fbr_.stack.push<gc::ref<Value>>(closure);
            }
        };

        void visit_define(const AST::Define &define) override {
            //std::cerr << "  visit define: " << define.symbol_->name_ << std::endl;
            if(stage_ == 0) {
                if(define.data_) {
                    //std::cerr << "    add to ns " << ns_->name() << " sym: " << define.symbol_->namei_ << std::endl;
                    ns_.mutate()->define(define.symbol_->namei_, define.data_);
                }
                else if(define.expression_) {
                    define.expression_->accept(*this);
                    auto value = fbr_.stack.pop_cast<gc::ref<Value>>(fbr_);
                    fbr_.allocator().share(value);
                    //std::cerr << "    add to ns " << ns_->name() << " sym: " << define.symbol_->namei_ << std::endl;
                    ns_.mutate()->define(define.symbol_->namei_, value);
                }
                else {
                    throw std::runtime_error("define must have expression or data");
                }
            }
        };

        void visit_module(const AST::Module &module) override {
            for(auto &expression : *module.expressions_) {
                expression->accept(*this);
            }
        };

        gc::ref<Namespace> load_boot_module(std::istream &ins, const std::string &name)
        {
            AST::Reader reader(runtime_);
            auto ast = reader.read(fbr_, ins);

            ns_ = Namespace::create(fbr_, ast, name);

            ast->accept(*this);

            ast.mutate()->ns_ = ns_;

            return ns_;

        }

        gc::ref<Namespace> load_main_module(const std::string &path, const std::string &name)
        {

            //loading
            stage_ = 0;
            todo_.push({path, name});

            while(!todo_.empty()) {
                auto [current_path, current_name] = todo_.top();
                todo_.pop();

                visited_.insert(current_name);

                //std::cerr << "load " << current_path << " " << current_name << std::endl;

                //make sure its compiled
                runtime_.compile(fbr_, current_path);

                auto path_pck = fs::path(current_path);
                path_pck.replace_extension("pck");
                std::ifstream ins(path_pck.string());


                AST::Reader reader(runtime_);
                auto ast = reader.read(fbr_, ins);

                ns_ = Namespace::create(fbr_, ast, current_name);
                runtime_.modules_[current_name] = ns_;

                ast->accept(*this);

                ast.mutate()->ns_ = ns_;

                ns_ = nullptr;
            }


            //linking
            stage_ = 1;
            todo_.push({path, name});

            while(!todo_.empty()) {
                auto [current_path, current_name] = todo_.top();
                todo_.pop();

                visited_.insert(current_name);

                //std::cerr << "link " << current_path << " " << current_name << std::endl;

                ns_ = runtime_.modules_[current_name];
                assert(ns_);

                ns_->module()->accept(*this);

                ns_ = nullptr;

            }

            auto main_ns = runtime_.modules_[name];
            assert(main_ns);
            return main_ns;
        }
    };

    //call without lock
    gc::ref<Namespace>
    RuntimeImpl::load_boot_module(std::istream &ins, const std::string &name) {

        auto &fbr = main_fiber();

        Loader loader(*this, fbr);

        return loader.load_boot_module(ins, name);
    }

    //call without lock
    gc::ref<Namespace>
    RuntimeImpl::load_main_module(const std::string &path, const std::string &name) 
    {

        auto &fbr = main_fiber();

        Loader loader(*this, fbr);

        return loader.load_main_module(path, name);
    }

    void
    RuntimeImpl::run(Fiber &fbr, gc::ref<Closure> closure)
    {
        assert(gc::is_shared_ref(closure));

        fbr.stack.push<gc::ref<Value>>(closure);

        fbr.enqueue([&,closure]() {
            return compiler_->enter(&fbr, bootstrap_apply_0_.get(), compiler_->code(closure->function()));
        });
    }

    //call without lock
    void
    RuntimeImpl::run(const std::string &path) {
        //main run method that will run given path and startup workers etc
        
        auto &fbr = main_fiber();
        fbr.attach(allocator());

        auto ns = load_main_module(path, "__main__");
        
        auto main = ns->find(intern("main"));
        if(!main) {
            throw std::runtime_error("could not find function main in " + path + " " + ns->name());
        }

        auto main_value = *main;

        fbr.stack.push<gc::ref<Value>>(main_value);

        fbr.detach(allocator());

        fbr.enqueue([&]() {
            return compiler_->enter(&fbr, bootstrap_apply_0_.get(), compiler_->code(static_cast<const Closure &>(*main_value).function()));
        });

        assert(!workers_.empty());
       
        boost::asio::io_service::work work(io_service);

        for (auto &worker : workers_) {
            worker.allocator_ = std::make_unique<gc::allocator_t>(collector_);
            worker.thread_ = std::thread([&]() {
                current_allocator_ = worker.allocator_.get();
                while(true) {   
                    this->io_service.run();
                    if(collector_.stw_mutators_wait.load()) 
                    {
                        //std::cerr << "sleepin working checking in shared" << std::endl;
                        std::unique_lock<std::mutex> guard(lock);
                        collector_.checkin_shared(*current_allocator_, guard);
                    }
                    else {
                        break;
                    }
                }
                current_allocator_ = nullptr;
            });
        }

        collector_.start();
        collector_.collect_shared(
        //continue running?:
        [&]() {
            return !io_service.stopped();
        },
        //number of mutators to stop:
        [&]() {
            return workers_.size();
        }, 
        //stw start
        [&](int n) {
            io_service.stop();
        },
        //stw end
        [&](int n) {
            io_service.restart();
            if(n == 2) {
                assert(fibers_sleeping_grey_->empty());
                std::swap(fibers_sleeping_black_, fibers_sleeping_grey_);
            }
        },
        //iterate rootsets for collector
        [&](auto for_each_root_set) {
            //builtins
            for_each_root_set([&](auto accept) {
                for(auto &builtin : builtins_) {
                    accept(builtin.second.get());
                }
            });
            //global roots
            for_each_root_set([&](auto accept) {
                for(auto &root : roots_) {
                    accept(root.get());
                }
            });
            //modules
            for_each_root_set([&](auto accept) {
                for(auto &item : modules_) {
                    accept(item.second.get());
                }
            });
            //types
            for_each_root_set([&](auto accept) {
                for(auto &item : types_) {
                    accept(item.second.get());
                }
            });
            //all running fibers
            for(auto &current : *fibers_running_) {
                for_each_root_set([&](auto accept) {
                    accept(&current);
                    current.roots(accept);
                });
            }
        },
        //has incremental rootsets?
        [&]() {
            return !fibers_sleeping_grey_->empty();
        },
        //iterate incremental rootsets for collector
        [&](auto for_each_root_set) {
            //std::cerr << "incr rootset, grey left: " << fibers_sleeping_grey_.size() << std::endl;
            for(int i = 0; i < 100 && !fibers_sleeping_grey_->empty(); i++) {
                auto &f = fibers_sleeping_grey_->front();
                assert(f.color() == fibers_sleeping_grey_);
                f.switch_color(fibers_sleeping_scanning_);
            }
            for(auto &current : *fibers_sleeping_scanning_) {
                for_each_root_set([&](auto accept) {
                    accept(&current);
                    current.roots(accept);
                });
            };
        },
        //incremental root set done
        [&]() {
            while(!fibers_sleeping_scanning_->empty()) {
                auto &f = fibers_sleeping_scanning_->front();
                assert(f.color() == fibers_sleeping_scanning_);
                f.switch_color(fibers_sleeping_black_);
            }
            is_fiber_sleeping_black_cond_.notify_all();            
            //std::cerr << "incr root set done!" << std::endl;
        },
        //iterate all allocators for collector
        [&](auto accept) {
            //worker allocators
            for (auto &worker : workers_) {
                accept(*worker.allocator_);
            }
            //runtime allocator
            accept(*allocator_);
        });
       
        //done

        //wait for workers to finish
        for (auto &worker : workers_) {
            worker.thread_.join();
        }

        //fbr.private_heap_->clear();
        fiber_exitted(main_fiber_);
   
        collector_.collect_shared_final(//iterate all allocators for collector
        [&](auto accept) {
            //worker allocators
            for (auto &worker : workers_) {
                accept(*worker.allocator_);
            }
            //runtime allocator
            accept(*allocator_);
        });

        collector_.stop();
    }

    //call without lock
    gc::ref<Namespace> RuntimeImpl::load_prelude() {
        std::ifstream ins("./runtime/prelude.pck");
        auto ns = load_boot_module(ins, "__prelude__");
        //find all globals in prelude and add them to builtins
        std::lock_guard<std::mutex> guard(lock);
        for (const auto &n : interns_.left()) {
            if (auto found = ns->find(n.second)) {
                register_builtin(n.first, *found);
            }
        }
        return ns;
    }

    //call without lock
    gc::ref<Namespace> RuntimeImpl::load_compiler() {
        std::ifstream ins("./runtime/compiler.pck");
        auto ns = load_boot_module(ins, "__compiler__");
        std::lock_guard<std::mutex> guard(lock);
        if (auto found = ns->find(intern("__compile__"))) {
            register_builtin("__compile__", *found);
        } else {
            throw std::runtime_error("error loading compiler");
        }   
        if (auto found = ns->find(intern("__apply_defers__"))) {
            register_builtin("__apply_defers__", *found);
        } else {
            throw std::runtime_error("error loading compiler");
        }
        return ns;
    }

    void RuntimeImpl::quit() {
        std::lock_guard<std::mutex> guard(lock);
        std::cerr << "quit called!" << std::endl;
        stop();
    }

    //required gil
    void RuntimeImpl::stop() {
        //exit event loop as quick as possible (from all threads)
        io_service.stop();
        collector_.notify(); //wake up collector so that it sees the stop
    }

    void RuntimeImpl::fiber_created(gc::ref<Fiber> f) {
        //std::cerr << "fiber created: " << &f << std::endl;
        f.mutate()->switch_color(fibers_sleeping_grey_);
    }

    void RuntimeImpl::fiber_exitted(gc::ref<Fiber> f) {
        f.mutate()->switch_color(nullptr);
    }   

    //call with lock
    void RuntimeImpl::fiber_attach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) {
        assert(!(f->color() == fibers_running_));
        assert(current_allocator_ != nullptr);
        {
            f.mutate()->attach(*current_allocator_);
            if(current_allocator_->write_barrier_) {
                //in concurrent mark phase 
                if(f->color() == fibers_sleeping_scanning_ || f->color() == fibers_sleeping_grey_) {
                    if(f->color() == fibers_sleeping_grey_) {
                        //move to front of grey, so it gets picked up fast
                        f.mutate()->switch_color(fibers_sleeping_grey_);
                    }
                    //wait till it gets black eventually
                    is_fiber_sleeping_black_cond_.wait(lock, [&]() {
                        return f->color() == fibers_sleeping_black_;
                    });
                }
                assert(f->color() == fibers_sleeping_black_);
                f.mutate()->switch_color(fibers_running_);
            }
            else {
                //not in concurrent mark phase        
                assert(f->color() == fibers_sleeping_grey_);
                f.mutate()->switch_color(fibers_running_);
            }
        }
        assert(current_allocator_ != nullptr);        
    }

    //call with lock
    void RuntimeImpl::fiber_detach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) {
        assert(current_allocator_ != nullptr);
        {
            f.mutate()->detach(*current_allocator_);
            if(current_allocator_->write_barrier_) {
                f.mutate()->switch_color(fibers_sleeping_black_);
            }
            else {
                f.mutate()->switch_color(fibers_sleeping_grey_);
            }
        }
        assert(current_allocator_ != nullptr);
    }

    gc::ref<Type> RuntimeImpl::create_type(std::string name) {
        assert(types_.count(name) == 0);
        auto type = Type::create(allocator(), name);
        types_[name] = type;
        return type;
    }

    std::unique_ptr<Runtime> Runtime::create(int argc, char *argv[]) {
        return std::make_unique<RuntimeImpl>(argc, argv);
    }

    gc::allocator_t &Runtime::current_allocator()
    {
        assert(current_allocator_ != nullptr);
        return *current_allocator_;
    }

}
