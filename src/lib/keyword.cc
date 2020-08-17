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

#include "keyword.h"
#include "fiber.h"
#include "frame.h"
#include "struct.h"

#include <unordered_map>

namespace park {

static std::unordered_map<std::string, gc::ref<Keyword>> KEYWORDS; //should be 'weak' map if we would support AST unloading

class KeywordImpl : public SharedValueImpl<Keyword, KeywordImpl>
{
private:

    std::string name_;

public:
    KeywordImpl(const std::string &name)
        : name_(name) 
    {
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}; 

    void repr(Fiber &fbr, std::ostream &out) const override {
        out << name_;
    }

    std::string to_string(Fiber &fbr) const override {
        return name_;
    }

    const size_t map_key_hash(Fiber &fbr) const override
    {
		return std::hash<const void *>()(static_cast<const void *>(this));
    }

    const bool map_key_equals(Fiber &fbr, const Value &other) const override
    {
    	return this == &other;
    }


    //needs runtime lock!!!
    static gc::ref<Keyword> create(Fiber &fbr, const std::string &name)
    {   
        auto found = KEYWORDS.find(name);
        if (found != KEYWORDS.end()) {
            return found->second;
        } else {
            auto keyword = gc::make_shared_ref<KeywordImpl>(fbr.allocator(), name);   
            KEYWORDS[name] = keyword;
            //Runtime::from_fbr(fbr).add_root(keyword); should be covered by AST not being unloaded
            return keyword;
        }
    }

    static int64_t _lookup(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<Keyword> keyword;
        gc::ref<Struct> map_or_struct; //TODO map or struct

        //TODO check args, types
        return frame.check(). 
            argument_count(1).  
            argument(0, keyword).
            argument(1, map_or_struct).
            result<Value>([&]() {   
              if (auto found = map_or_struct->get(fbr, keyword)) {
                return *found;
              }
              throw std::runtime_error("TODO _lookup failed in keyword");
            });
    }

    MethodImpl dispatch(Fiber &fbr, const AST::Apply &apply) const override {

        Frame frame(fbr, apply);

        gc::ref<Value> keyword;
        gc::ref<Value> map_or_struct;

        //TODO check args, types
        if(frame.check(). 
            argument_count(1).  
            argument(0, keyword).
            argument(1, map_or_struct).
            ok()) {
                return _lookup;
            }

        throw std::runtime_error("bad dispatch on keyword");
    }    
};

void Keyword::init(Runtime &runtime)
{
}

//needs lock! or runtime init
gc::ref<Keyword> Keyword::create(Fiber &fbr, const std::string &name)
{
   return KeywordImpl::create(fbr, name);
}

}