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

#include <iostream>
#include <fstream>

#include <memory>
#include <type_traits>
#include <algorithm>
#include <string>
#include <streambuf>
#include <unordered_map>
#include <boost/any.hpp>
#include <vector>
#include <exception>

#include "lexertl/generator.hpp"
#include <boost/container/static_vector.hpp>

#include "park/gc.h"

#include "setjmp.h"

jmp_buf jmp_env;

using fn_t = std::function<void(int)>;

std::vector<int> stack;
std::vector<fn_t> continuation_stack;


void apply2(int tcc, fn_t fn, int arg1, int arg2, fn_t next)
{
    stack.push_back(arg1);
    stack.push_back(arg2);
    continuation_stack.push_back(next);
    fn(tcc - 1);   
}

void return_(int tcc, int argcount, int result)
{
    //std::cout << "return_ " << result << " stack sz: " << stack.size() << std::endl;
    stack.pop_back();
    stack.pop_back();
    stack.push_back(result);
    auto fn = continuation_stack.back();
    continuation_stack.pop_back();
    fn(tcc - 1);
}

void branch(int tcc, fn_t fn_if_true, fn_t fn_if_false)
{
    auto b = stack.back();
    stack.pop_back();
    if(b) {
        fn_if_true(tcc - 1);
    }
    else {
        fn_if_false(tcc - 1);
    }
}

void recur2(int tcc, fn_t fn, int arg1, int arg2)
{
    *(stack.end() - 1) = arg2;
    *(stack.end() - 2) = arg1;
    fn(tcc - 1);
}

template<int ARG_COUNT>
int arg(int index)
{
    return *(stack.end() - (ARG_COUNT - index + 1));
}

void stack_guard(int tcc, fn_t fn)
{
    if(tcc < 0) {
        continuation_stack.push_back(fn);
        _longjmp(jmp_env, -1);
    }
}

void equals(int tcc) {
    stack_guard(tcc, equals);

    return_(tcc, 2, arg<2>(1) == arg<2>(2));
}

void fac(int tcc) {
    stack_guard(tcc, fac);
    //std::cout << "fac " << tcc << " " << arg<2>(1) << " " << arg<2>(2) << std::endl;

    apply2(tcc, equals, arg<2>(1), 0, [](int tcc) {
        branch(tcc, 
        [](int tcc) {
            //std::cout << "return from fac" << std::endl;
            return_(tcc, 2, arg<2>(2));
        },
        [](int tcc) {
            //std::cout << "recur from fac" << std::endl;
            recur2(tcc, fac, arg<2>(1) - 1, arg<2>(1) * arg<2>(2));
        });
    });

    std::runtime_error("should not be here");
}


void loop(int tcc) {
    stack_guard(tcc, loop);

    //std::cout << "loop " << tcc << " " << arg<2>(1) << " " << arg<2>(2) << std::endl;

    apply2(tcc, equals, arg<2>(1), 0, [](int tcc) {
        branch(tcc,
        [](int tcc) {
            return_(tcc, 2, arg<2>(2));
        },
        [](int tcc) {
            //call fac
            //std::cout << "call fac!: sck sz: " << stack.size() << std::endl;
            apply2(tcc, fac, 5, 1, [](int tcc) {
                //std::cout << "cont from aplly" << " stack sz: " << stack.size() << std::endl;
                auto result = stack.back();
                stack.pop_back();
                //std::cout << "res at loop: " << result << " stack sz: " << stack.size() << std::endl;
                recur2(tcc, loop, arg<2>(1) - 1, arg<2>(2) + result);
            });
        });
    });

    std::runtime_error("should not be here");
}

int main(int argc, char *argv[]) {
    auto done = [](int tcc) {
        std::cout << *(stack.end() - 1) << std::endl;
        exit(1);
    };

    stack.push_back(1000000);
    stack.push_back(0);

    continuation_stack.push_back(done);

    fn_t fn = loop;

    while(_setjmp(jmp_env)) {
        fn = continuation_stack.back();
        continuation_stack.pop_back();
        fn(255);
    }
    fn(255);

//    throw std::runtime_error("should not be here");
}

