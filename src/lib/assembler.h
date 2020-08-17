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

#ifndef __ASSEMBLER_H
#define __ASSEMBLER_H

#include <vector>
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace park {

    class X64Assembler {
    private:
        std::vector<uint8_t> code;
        std::vector<std::pair<int, int>> fixups;
        std::unordered_map<int, int> binds;
        std::vector<std::pair<int, int>> instructions;
        int labels = 0;

        void emit(int64_t i) {
            auto const *pi = reinterpret_cast<const uint8_t *>(&i);
            code.insert(code.end(), pi, pi + 8);
        }

        void emit_at(int offset, int64_t i) {
            auto const *pi = reinterpret_cast<const uint8_t *>(&i);
            std::copy(pi, pi + 8, code.begin() + offset);
        }

        void emit_at(int offset, int32_t i) {
            auto const *pi = reinterpret_cast<const uint8_t *>(&i);
            std::copy(pi, pi + 4, code.begin() + offset);
        }

        int offset() {
            return std::distance(code.begin(), code.end());
        }

        void emit_code(const std::vector<uint8_t> &bytes) {
            int start = offset();
            code.insert(code.end(), bytes.begin(), bytes.end());
            instructions.push_back({start, offset()});
        }

        void emit_code(std::function<void()> insert_code) {
            int start = offset();
            insert_code();
            instructions.push_back({start, offset()});
        }

    public:
        X64Assembler() {}

        int new_label() {
            return ++labels;
        }

        int bind(int label) {
            if (binds.count(label) > 0) {
                throw std::runtime_error("cannot bind twice");
            } else {
                auto o = offset();
                binds[label] = o;
                return o;
            }
        }

        std::vector<uint8_t> make() {
            for (auto fixup : fixups) {
                auto found = binds.find(fixup.first);
                if (found == binds.end()) {
                    throw std::runtime_error("bind not found");
                } else {
                    emit_at(fixup.second,
                            static_cast<int32_t>(found->second - fixup.second - 4));
                }
            }

            return code;
        }

        void dump() {
            using namespace std;
            for (auto instruction : instructions) {
                auto start = instruction.first;
                auto end = instruction.second;
                cout << dec << start << " " << end << " ";
                while (start < end) {
                    cout << setfill('0') << setw(2) << hex << (int) code[start] << " ";
                    start++;
                }
                cout << endl;
            }
            cout << endl;
            cout << "code len: " << dec << code.size() << endl;
        }

        void call_rax() {
            emit_code({0xFF, 0xD0});
        }

        void jz(int label) {
            emit_code([&]() {
                code.insert(code.end(), {0x0F, 0x84});
                fixups.push_back({label, offset()});
                code.insert(code.end(), {0xFF, 0xFF, 0xFF, 0xFF});
            });
        }

        void js(int label) {
            emit_code([&]() {
                code.insert(code.end(), {0x0F, 0x88});
                fixups.push_back({label, offset()});
                code.insert(code.end(), {0xFF, 0xFF, 0xFF, 0xFF});
            });
        }

        void jmp_rel(int label) {
            emit_code([&]() {
                code.push_back(0xE9);
                fixups.push_back({label, offset()});
                code.insert(code.end(), {0x00, 0x00, 0x00, 0x00});
            });
        }

        void mov_rdi_imm(int64_t imm) {
            emit_code([&]() {
                code.insert(code.end(), {0x48, 0xBF});
                emit(imm);
            });
        }

        void mov_rsi_imm(int64_t imm) {
            emit_code([&]() {
                code.insert(code.end(), {0x48, 0xBE});
                emit(imm);
            });
        }

        void mov_rdx_imm(int64_t imm) {
            emit_code([&]() {
                code.insert(code.end(), {0x48, 0xBA});
                emit(imm);
            });
        }

        void mov_rdx_rsp() {
            emit_code({0x48, 0x89, 0xe2});
        }

        void mov_rax_imm(int64_t imm) {
            emit_code([&]() {
                code.insert(code.end(), {0x48, 0xB8});
                emit(imm);
            });
        }

        void mov_rax_mem(uintptr_t mem) {
            emit_code([&]() {
                code.insert(code.end(), {0x48, 0xA1});
                emit(mem);
            });
        }

        void cmp_rax_1() {
            emit_code({0x48, 0x83, 0xF8, 0x01});
        }

        void mov_rdx_rax() {
            emit_code({0x48, 0x89, 0xC2});
        }

        void mov_rdi_rbx() {
            emit_code({0x48, 0x89, 0xDF});
        }

        void pop_rcx() {
            emit_code({0x59});
        }

        void pop_rdx() {
            emit_code({0x5a});
        }

        void push_rax() {
            emit_code({0x50});
        }

        void test_rax_rax() {
            emit_code({0x48, 0x85, 0xC0});
        }

        void ret() {
            emit_code({0xC3});
        }

        void int3() {
            emit_code({0xCC});
        }

        void mov_rcx_rsp_ptr() {
            emit_code({0x48, 0x8B, 0x0C, 0x24}); //mov rcx, [rsp]
        }

        void mov_rdx_rsp_ptr() {
            emit_code({0x48, 0x8B, 0x14, 0x24}); //mov rdx, [rsp]
        }

        void sub_rsp_8() {
            emit_code({0x48, 0x83, 0xEC, 0x08}); //sub rsp, 8
        }

        void add_rsp_8() {
            emit_code({0x48, 0x83, 0xC4, 0x08}); //add rsp, 8
        }

        //The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, and R9
        void entry_thunk() {
            //reason for thunk is to put current fiber in rbx
            //also to align the stack properly

            //on entry rdi = fbr, rsi = apply node, rdx = start addr
            emit_code({
                              0x53,                         // push   rbx       ; callee save
                              0x48, 0x89, 0xFB,             // mov    rbx,rdi   ; keep current fiber in rbx
                              0x48, 0x89, 0xD0,             // mov    rax,rdx   ; start addr
                              0x48, 0x83, 0xEC, 0x10,       // sub    rsp,0x10  ; align stack
                              0xFF,
                              0xD0,                         // call   rax       ; call into code, with context as 1st arg (already in rdi) and apply & as 2nd arg (already in rsi)
                              0x48, 0x83, 0xC4, 0x10,       // add    rsp,0x10  ; align stack
                              0x5B,                         // pop    rbx       ; restore rbx for caller
                              0xC3,                         // ret              ; return
                      });
        }

        void reentry_thunk() {
            //reason for thunk is to put current fiber in rbx
            //also to align the stack properly
            emit_code({
                              0x53,                         //push   rbx       ; callee save
                              0x48, 0x89, 0xFB,             //mov    rbx,rdi   ; keep current fiber in rbx
                              0x48, 0x89, 0xD0,             //mov    rax,rdx   ; ret code to be inspected by apply
                              0x48, 0x89, 0xF2,             //mov    rdx,rsi   ; start addr
                              0x48, 0x83, 0xEC, 0x10,       //sub    rsp,0x10  ; align stack
                              0xFF, 0xE2                    //jmp rdx
                      });
        }

        //The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, and R9
        void exit_thunk(int64_t exec_exit) {
            emit_code({0x48, 0x89, 0xD1}) ; //mov rcx, rdx (exit_code), tmp store
            emit_code({0x5a});              //pop rdx // pop return address into rdx, passing it as 3d arg to exec_exit
            emit_code({0x51});              //push rcx, save exit_code on stack across call to exec_exit
            emit_code({0x48, 0x89, 0xDF});  //mov rdi, rbx ; fiber first arg
            // rsi was already setup by caller to contain apply as 2nd arg
            // 
            emit_code({0x48, 0xB8});        //mov rax, exec_exit
            emit(exec_exit);
//            emit_code({0xcc});
            emit_code({0xFF, 0xD0});        //call rax (call exec_exit)
//            emit_code({0xcc});
            // exec_exit returned return address in rax
            // push it, so that following ret will jump to it
            emit_code({0x59}); //pop rcx    //first pop original exit_code back into rcx
            emit_code({0x50}); //push rax   //the jump address
            emit_code({0x48, 0x89, 0xc8}); // mov rax, rcx (return original exit_code)
            emit_code({
                              0xC3  // ret ; jumps out of interpreter level
                      });
        }

    };

}

#endif
