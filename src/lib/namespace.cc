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

#include "namespace.h"
#include "runtime.h"
#include "ast.h"

#include "closure.h"
#include "fiber.h"

#include <unordered_map>

namespace park {

    class NamespaceImpl : public SharedValueImpl<Namespace, NamespaceImpl> {
    private:

        gc::ref<AST::Module> module_;
        std::string name_;

        bool initialized_ = false;
        std::unordered_map<size_t, gc::ref<Value>> globals_by_name_;

    public:
        NamespaceImpl(gc::ref<AST::Module> module, const std::string &name) : module_(module), name_(name), initialized_(false) {}

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            accept(module_);
            for(auto &item : globals_by_name_) {
                accept(item.second);
            }
        }

        virtual std::string name()  const override {
            return name_;
        }

        virtual gc::ref<AST::Module> module() const override {
            return module_;
        }

        virtual bool is_initialized() const override {
            return initialized_;
        }

        virtual void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<namespace>";
        }

        void define(size_t namei, gc::ref<Value> value) override {
            if(initialized_) {
                throw std::runtime_error("cannot define in namespace after initialisiation");
            }
            assert(gc::is_shared_ref(value));
            auto found = globals_by_name_.find(namei);
            if (found != globals_by_name_.end()) {
                throw std::runtime_error("cannot redefine global!"); //TODO decent error
            } else {
                globals_by_name_[namei] = value;
            }
        }

        std::optional<gc::ref<Value>> find(size_t namei) const override {
            /*
             * TODO
            if(!initialized) {
                throw std::runtime_error("namespace not initialized");
            }
             */
            auto found = globals_by_name_.find(namei);
            if (found != globals_by_name_.end()) {
                return found->second;
            } else {
                return std::nullopt;
            }
        }

        void set_initialized(bool initialized) override
        {
            assert(!initialized_);
            initialized_ = initialized;
        }

    };



    gc::ref<Namespace> Namespace::create(Fiber &fbr, gc::ref<AST::Module> module, const std::string &name)
    {
        assert(!module->ns_);
        return gc::make_shared_ref<NamespaceImpl>(fbr.allocator(), module, name);
    }

    void Namespace::init(Runtime &runtime) {
    }

}