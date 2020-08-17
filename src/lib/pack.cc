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

#include "builtin.h"

#include "visitor.h"

#include <boost/endian/conversion.hpp>

namespace park {

    namespace pack {

        static gc::ref<Value> PACK;
        static gc::ref<Value> UNPACK;

        uint8_t read_uint8_t(std::istream &ins) {
            uint8_t v;
            ins.read(reinterpret_cast<char *>(&v), sizeof(v));
            return v;
        }

        int32_t read_int32_t(std::istream &ins) {
            int32_t v;
            ins.read(reinterpret_cast<char *>(&v), sizeof(v));
            boost::endian::big_to_native_inplace(v);
            return v;
        }

        int64_t read_int64_t(std::istream &ins) {
            int64_t v;
            ins.read(reinterpret_cast<char *>(&v), sizeof(v));
            boost::endian::big_to_native_inplace(v);
            return v;
        }

        size_t read_map_header(std::istream &ins)
        {
            auto type = read_uint8_t(ins);

            if (type != 0xdf) {
                throw std::runtime_error("expected map32");
            }

            return read_int32_t(ins);
        }

        size_t read_array_header(std::istream &ins) {
            auto type = read_uint8_t(ins);

            if (type != 0xdd) {
                throw std::runtime_error("expected array32");
            }

            return read_int32_t(ins);
        }

        std::string read_string(std::istream &ins) {
            auto type = pack::read_uint8_t(ins);
            if (type != 0xdb) {
                throw std::runtime_error("expected string");
            }
            auto len = read_int32_t(ins);
            std::string str(len, '\0');
            ins.read(&str[0], len);
            return str;
        }

        int64_t read_integer(std::istream &ins) {
            auto type = pack::read_uint8_t(ins);
            if (type != 0xd3) {
                throw std::runtime_error("expected integer");
            }
            return read_int64_t(ins);
        }

        class Packer : public Visitor {
        protected:
            std::ostream &os;

            int32_t len_from_size(size_t size) {
                if (size < std::numeric_limits<int>::max()) {
                    return static_cast<int32_t>(size);
                } else {
                    throw std::out_of_range("to big to pack");
                }
            }

            void write_type(uint8_t type) {
                os.write(reinterpret_cast<const char *>(&type), sizeof(type));
            }

            void write_map_header(const Map &v) {
                write_type(0xdf); //map 32
                int32_t len = len_from_size(v.size());
                boost::endian::native_to_big_inplace(len);
                os.write(reinterpret_cast<const char *>(&len), sizeof(len));
            }

        public:
            explicit Packer(std::ostream &os) : os(os) {}

            const Value &visit(Fiber &fbr, const Map &v) override {
                write_map_header(v);
                v.iterate([&fbr, this](gc::ref<Value> key, gc::ref<Value> value) {
                    key->accept(fbr, *this);
                    value->accept(fbr, *this);
                });
                return v;
            }

            const Value &visit(Fiber &fbr, const Vector &v) override {
                write_type(0xdd); //array 32
                int32_t len = len_from_size(v.size());
                boost::endian::native_to_big_inplace(len);
                os.write(reinterpret_cast<const char *>(&len), sizeof(len));
                for (size_t i = 0; i < v.size(); i++) {
                    v.nth(i)->accept(fbr, *this);
                }
                return v;
            }

            const Value &visit(Fiber &fbr, const Integer &v) override {
                write_type(0xd3); //int 64

                int64_t vb = boost::endian::native_to_big(v.to_index(fbr));

                os.write(reinterpret_cast<const char *>(&vb), sizeof(vb));

                return v;
            }

            const Value &visit(Fiber &fbr, const Atom &v) override {
                write_type(0xc7); //ext 8
                write_type(0x00); //0 data len
                write_type(0x01); //atom type

                v.value()->accept(fbr, *this);

                return v;
            }

            const Value &visit(Fiber &fbr, const String &v) override {
                auto s = v.to_string(fbr);

                write_type(0xdb); //str 32

                int32_t len = len_from_size(s.length());
                boost::endian::native_to_big_inplace(len);
                os.write(reinterpret_cast<const char *>(&len), sizeof(len));
                os.write(s.data(), s.length());

                return v;
            }

            const Value &visit(Fiber &fbr, const Boolean &v) override {
                if (v.to_bool(fbr)) {
                    write_type(0xc3); //true
                } else {
                    write_type(0xc2); //false
                }
                return v;
            }
        };

        class NodePacker : public Packer {
        private:
            bool in_data_ = false;
        public:
            explicit NodePacker(std::ostream &os) : Packer(os) {}

            const Value &visit(Fiber &fbr, const Map &v) override {

                write_map_header(v);

                if(in_data_) {
                    //data map, keys can be anything
                    v.iterate([&](gc::ref<Value> key, gc::ref<Value> value) {
                        key->accept(fbr, *this);
                        value->accept(fbr, *this);
                    });
                }
                else {
                    //node map, keys should be string, 1 is type
                    v.iterate([&](gc::ref<Value> key, gc::ref<Value> value) {
                        if(key->to_string(fbr) == "type") {
                            key->accept(fbr, *this);
                            value->accept(fbr, *this);
                        }
                    });

                    v.iterate([&](gc::ref<Value> key, gc::ref<Value> value) {
                        if(key->to_string(fbr) != "type") {                    
                            key->accept(fbr, *this);
                            if(key->to_string(fbr) == "data") {
                                in_data_ = true;
                            }
                            value->accept(fbr, *this);
                            if(key->to_string(fbr) == "data") {                           
                                in_data_ = false;
                            }
                        }
                    });
                }

                return v;
            }

        };

        gc::ref<Value> unpack(Fiber &fbr, std::istream &ins) {
            uint8_t type;
            ins.read(reinterpret_cast<char *>(&type), sizeof(type));
            switch (type) {
                case 0xc3: { //true
                    return Boolean::create(true);
                }
                case 0xc2: { //false
                    return Boolean::create(false);
                }
                case 0xd3: { //int 64
                    int64_t v;
                    ins.read(reinterpret_cast<char *>(&v), sizeof(v));
                    return Integer::create(fbr, boost::endian::big_to_native(v));
                }
                case 0xdb: { //str 32
                    int32_t len;
                    ins.read(reinterpret_cast<char *>(&len), sizeof(len));
                    boost::endian::big_to_native_inplace(len);
                    std::string str(len, '\0');
                    ins.read(&str[0], len);
                    return String::create(fbr, str);
                }
                case 0xdd: { //array 32
                    int32_t len;
                    ins.read(reinterpret_cast<char *>(&len), sizeof(len));
                    boost::endian::big_to_native_inplace(len);
                    auto v = Vector::create(fbr);
                    for (auto i = 0; i < len; i++) {
                        v = v->conj(fbr, unpack(fbr, ins));
                    }
                    return v;
                }
                case 0xdf: { //map 32
                    int32_t len;
                    ins.read(reinterpret_cast<char *>(&len), sizeof(len));
                    boost::endian::big_to_native_inplace(len);
                    auto m = Map::create(fbr);
                    for (auto i = 0; i < len; i++) {
                        m = m->assoc(fbr, unpack(fbr, ins), unpack(fbr, ins));
                    }
                    return m;
                }
                case 0xc7: { // ext type
                    uint8_t ch;
                    ins.read(reinterpret_cast<char *>(&ch), sizeof(ch)); //data
                    assert(ch == 0);
                    ins.read(reinterpret_cast<char *>(&ch), sizeof(ch)); //type
                    assert(ch == 1); //for now only atom supported;
                    return Atom::create(fbr.allocator(), unpack(fbr, ins));                   
                }
                
                default:
                    throw std::runtime_error("unknown msgpack type");
            }
        }

        void pack(Fiber &fbr, const Value &value, std::ostream &outs) {
            Packer packer(outs);
            value.accept(fbr, packer);
        }

        void pack_node(Fiber &fbr, const Value &value, std::ostream &outs) {
            NodePacker packer(outs);
            value.accept(fbr, packer);
        }

        int64_t _pack(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Value> val;

            return frame.check().
                static_dispatch(*PACK).
                argument_count(1).
                argument<Value>(1, val).
                result<Value>([&]() {
                    std::stringstream ss;
                    pack(fbr, *val, ss);
                    return String::create(fbr, ss.str());
               });
        }

        int64_t _unpack(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<Value> arg;

            return frame.check().
                static_dispatch(*UNPACK).
                argument_count(1).
                argument<Value>(1, arg).
                result<Value>([&]() {
                    std::stringstream ins(arg->to_string(fbr));
                    return unpack(fbr, ins);
               });
        }

        void init(Runtime &runtime) {
            PACK = runtime.create_builtin<BuiltinStaticDispatch>("pack", _pack);
            UNPACK = runtime.create_builtin<BuiltinStaticDispatch>("unpack", _unpack);
        }
    }

}

