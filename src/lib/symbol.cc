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

#include "symbol.h"
#include "fiber.h"

namespace park {

class SymbolImpl : public ValueImpl<Symbol, SymbolImpl>
{
private:
    size_t namei_;
    

public:
    SymbolImpl(size_t namei)
        : namei_(namei)
    {
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}; 

    void repr(Fiber &fbr, std::ostream &out) const override {
        out << to_string(fbr);
    }

    std::string to_string(Fiber &fbr) const override {
        return Runtime::from_fbr(fbr).name(namei_);
    }

    const size_t map_key_hash(Fiber &fbr) const override
    {
		return std::hash<size_t>()(namei_);
    }

    const bool map_key_equals(Fiber &fbr, const Value &other) const override
    {
        throw std::runtime_error("TODO map key equals for symbol");
    }

    static gc::ref<Symbol> create(Fiber &fbr, const std::string &name)
    {   
        return gc::make_ref<SymbolImpl>(fbr.allocator(), Runtime::from_fbr(fbr).intern(name));   
    }
};

void Symbol::init(Runtime &runtime)
{
}

gc::ref<Symbol> Symbol::create(Fiber &fbr, const std::string &name)
{
   return SymbolImpl::create(fbr, name);
}

}