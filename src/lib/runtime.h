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

#ifndef __RUNTIME_H
#define __RUNTIME_H

#include <mutex>
#include <optional>

#include <boost/asio.hpp>

#include "value.h"

namespace park {

    class Fiber;

    class Namespace;

    class Type;

    class Compiler;

    class Closure;

    namespace AST {
        struct Function;
        struct Module;
    }

    class Runtime {

    protected:
        virtual gc::allocator_t &allocator() = 0;

    public:
        virtual ~Runtime() {};

        boost::asio::io_service io_service;

        std::mutex lock;

        static std::unique_ptr<Runtime> create(int argc, char *argv[]);

        static Runtime &from_fbr(Fiber &fbr);

        static gc::allocator_t &current_allocator(); 

        virtual gc::collector_t &collector() = 0;

        virtual Compiler &compiler() = 0;
        
        virtual void add_root(gc::ref<gc::collectable> ref) = 0;

        virtual void run(const std::string &path) = 0;
        virtual void run(Fiber &fbr, gc::ref<Closure> closure) = 0;

        virtual void stop() = 0;
        virtual void quit() = 0;

        //hold runtime lock
        virtual size_t intern(const std::string &s) = 0;

        virtual gc::ref<Type> create_type(std::string name) = 0;

        template<typename T, typename... Args>
        gc::ref<T> create_builtin(std::string name, Args&&... args) {
            auto builtin = gc::make_shared_ref<T>(allocator(), name, std::forward<Args>(args)...);
            register_builtin(name, builtin);
            return builtin;
        }

        template<typename T>
        gc::ref<T> create_root(std::function<gc::ref<T>(gc::allocator_t &)> create) {
            auto root = create(allocator());
            add_root(root);
            return root;
        }

        virtual void register_builtin(std::string name, gc::ref<Value> builtin) = 0;

        virtual void register_method(gc::ref<Value> builtin, const Type &self, MethodImpl impl) = 0;        
        virtual void register_method(gc::ref<Value> builtin, const Type &lhs, const Type &rhs, MethodImpl impl) = 0;        
        virtual void register_method(gc::ref<Value> builtin, value_t::kind_t lhs, value_t::kind_t rhs, MethodImpl impl) = 0;
        virtual void register_method(gc::ref<Value> builtin, value_t::kind_t lhs, const Type &rhs, MethodImpl impl) = 0;
        virtual void register_method(gc::ref<Value> builtin, const Type &lhs, value_t::kind_t rhs, MethodImpl impl) = 0;        

        //need runtime lock for these:
        virtual std::optional<gc::ref<Value>> find_builtin(size_t namei) = 0;
        virtual std::optional<gc::ref<Value>> find_builtin(const std::string &name) = 0;
        virtual gc::ref<Value> builtin(const std::string &name) = 0;

        //keep track of fibers (for ownership)
        virtual void fiber_created(gc::ref<Fiber> f) = 0;

        virtual void fiber_exitted(gc::ref<Fiber> f) = 0;

        virtual void fiber_attach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) = 0;

        virtual void fiber_detach(std::unique_lock<std::mutex> &lock, gc::ref<Fiber> f) = 0;

        size_t LAMBDA_NAMEI; //TODO still used?
        size_t DEFERS_NAMEI;
        size_t APPLY_DEFERS_NAMEI;
    };



}

namespace std {
    template <class T>
    class unlock_guard {
    public:
    unlock_guard(T& mutex) : mutex_(mutex) {
        mutex_.unlock();
    }

    ~unlock_guard() {
        mutex_.lock();
    }

    unlock_guard(const unlock_guard&) = delete;
    unlock_guard& operator=(const unlock_guard&) = delete;

    private:
    T& mutex_;
    };
}

#endif
