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

#ifndef _EXEC_H
#define _EXEC_H

#include "ast.h"

namespace park {

    extern "C" {

    extern void exec_literal(Fiber &fbr, const AST::Literal &literal);

    extern void exec_symbol(Fiber &fbr, const AST::Symbol &symbol);

    extern void exec_builtin(Fiber &fbr, const AST::Builtin &builtin);

    extern void exec_let(Fiber &fbr, const AST::Let &let);

    extern void exec_local(Fiber &fbr, const AST::Local &local);

    extern void exec_global(Fiber &fbr, const AST::Global &global);

    extern void exec_pop(Fiber &fbr, const AST::Node &node);

    extern int64_t exec_bool(Fiber &fbr, const AST::Branch &branch);

    extern void exec_recur(Fiber &fbr, const AST::Recur &recur);

    int64_t exec_check_defers(Fiber &fbr, const AST::Node &node);

    extern void exec_function(Fiber &fbr, const AST::Function &function);

    extern int64_t
    exec_function_prolog(Fiber &fbr, const AST::Apply &apply, const AST::Function &function, void *link);

    extern void exec_function_checkpoint(Fiber &fbr, const AST::Function &function);

    extern int64_t
    exec_function_epilog(Fiber &fbr, const AST::Function &function, void **link);

    extern void *exec_exit(Fiber &fbr, const AST::Apply &apply, void *link);

    } // extern "C"

}
#endif