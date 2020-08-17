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

#include "map.h"
#include "vector.h"
#include "builtin.h"
#include "type.h"
#include "visitor.h"

#include <bitset>

namespace park {
namespace mapimpl {

using bitmap_t = std::bitset<32>;

size_t map_index(uint32_t bitmap, uint32_t bit) {
    return bitmap_t(bitmap & (bit - 1)).count();
}

uint32_t map_mask(size_t hash, size_t shift) {
    return (hash >> shift) & 0x01fu;

}

uint32_t map_bitpos(size_t hash, size_t shift) {
    return 1u << map_mask(hash, shift);
}

class Node : public gc::collectable {
public:

    using assoc_ret_t = std::tuple<gc::ref<Node>, bool>;
    using find_ret_t = std::optional<gc::ref<Node>>;

    virtual assoc_ret_t
    assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const = 0;

    virtual find_ret_t 
    find(Fiber &fbr, size_t hash, const Value &key) const = 0;

    virtual void 
    iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> value)> f) const = 0;

    virtual size_t get_hash() const = 0;
};


void insert_leaf_node_at_idx(Fiber &, const gc::ref<Node> nodes_src[], size_t src_size, gc::ref<Node> nodes_dst[], uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash);

class FullNode : public Node {
    using full_nodes_t = std::array<gc::ref<Node>, 32>;

    const full_nodes_t _nodes;
    const size_t _shift;
    const size_t _hash;

public:
    FullNode(const gc::ref<Node> nodes[], size_t size, size_t shift)
        : _shift(shift), _hash(nodes[0]->get_hash()) {
        assert(size == 32);
        auto &__nodes = const_cast<full_nodes_t &>(_nodes);
        std::copy_n(nodes, 32, __nodes.begin());
    }

    FullNode(const gc::ref<Node> nodes[], size_t size, size_t shift, uint32_t idx, gc::ref<Node> n)
        : _shift(shift), _hash(nodes[0]->get_hash())
    {
        assert(size == 32);
        auto &__nodes = const_cast<full_nodes_t &>(_nodes);
        std::copy_n(nodes, 32, __nodes.begin());
        __nodes[idx] = n;
    }

     FullNode(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, size_t shift, uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash)
        : _shift(shift), _hash(nodes[0]->get_hash())
    {
        assert(size == 31);
        auto &__nodes = const_cast<full_nodes_t &>(_nodes);
        insert_leaf_node_at_idx(fbr, nodes, size, __nodes.data(), idx, key, val, hash);
    }


    static gc::ref<FullNode> create(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, size_t shift)
    {
        return gc::make_ref<FullNode>(fbr.allocator(), nodes, size, shift);        
    }

    static gc::ref<FullNode> create(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, size_t shift, uint32_t idx, gc::ref<Node> n)
    {
        return gc::make_ref<FullNode>(fbr.allocator(), nodes, size, shift, idx, n);
    }

    static gc::ref<FullNode> create(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, size_t shift, uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash)
    {
        return gc::make_ref<FullNode>(fbr.allocator(), fbr, nodes, size, shift, idx, key, val, hash);
    }   


    assoc_ret_t
    assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const override
    {
        auto idx = map_mask(hash, _shift);

        auto [n, leaf_added] = _nodes[idx]->assoc(fbr, _shift + 5, hash, key, val);

        if (n == _nodes[idx]) {
            return {this, leaf_added};
        } else {           
            return {create(fbr, _nodes.data(), _nodes.size(), _shift, idx, n), leaf_added};
        }
    }

    find_ret_t 
    find(Fiber &fbr, size_t hash, const Value &key) const override {
        return _nodes[map_mask(hash, _shift)]->find(fbr, hash, key);
    }

    void 
    iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> value)> f) const override
    {
        for(auto &node : _nodes) {
            node->iterate(f);
        }
    }

    size_t get_hash() const override {
        return _hash;
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
        for(auto &node : _nodes) {
            accept(node);
        }
    }

};

class BitmapIndexedNode : public Node {

    const uint32_t _bitmap;
    const size_t _size;
    const size_t _shift;
    const size_t _hash;

    alignas(16)
    gc::ref<Node> _nodes[];

public:
    BitmapIndexedNode(const gc::ref<Node> nodes[], size_t size, uint32_t bitmap, size_t shift)
            : _bitmap(bitmap), _size(size), _shift(shift), _hash(nodes[0]->get_hash()) {
        assert(size > 0 && size <=31);
        std::copy_n(nodes, size, _nodes);
    }   

    BitmapIndexedNode(const gc::ref<Node> nodes[], size_t size,  uint32_t bitmap, size_t shift, uint32_t idx, gc::ref<Node> n)
        : _bitmap(bitmap), _size(size), _shift(shift), _hash(nodes[0]->get_hash())
    {
        assert(size > 0 && size <=31);
        std::copy_n(nodes, size, _nodes);
        _nodes[idx] = n;
//        assert(idx != 0); //otherwise we would need to get the hash here
    }

    BitmapIndexedNode(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, uint32_t bitmap, size_t shift, uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash)
        : _bitmap(bitmap), _size(size + 1), _shift(shift), _hash(nodes[0]->get_hash())
    {
        assert(size > 0 && size <=31);
        insert_leaf_node_at_idx(fbr, nodes, size, _nodes, idx, key, val, hash);
    }
         
    assoc_ret_t
    static create(Fiber & fbr, gc::ref<Node> branch, size_t shift, gc::ref<Value> key, gc::ref<Value> val, size_t hash) {
        auto new_node = gc::make_ref_fam<BitmapIndexedNode, gc::ref<Node>>(fbr.allocator(), 1, &branch, 1, map_bitpos(branch->get_hash(), shift), shift);
        return new_node->assoc(fbr, shift, hash, key, val);
    }

    static gc::ref<BitmapIndexedNode> create(Fiber & fbr, const gc::ref<Node> nodes[], size_t size,  uint32_t bitmap, size_t shift, uint32_t idx, gc::ref<Node> n)
    {
        return gc::make_ref_fam<BitmapIndexedNode, gc::ref<Node>>(fbr.allocator(), size, nodes, size, bitmap, shift, idx, n);
    }

    static gc::ref<BitmapIndexedNode> create(Fiber & fbr, const gc::ref<Node> nodes[], size_t size, uint32_t bitmap, size_t shift, uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash)
    {
        return gc::make_ref_fam<BitmapIndexedNode, gc::ref<Node>>(fbr.allocator(), size + 1, fbr, nodes, size, bitmap, shift, idx, key, val, hash);
    }

    assoc_ret_t
    assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const override;

    find_ret_t 
    find(Fiber &fbr, size_t hash, const Value &key) const override {
        auto bit = map_bitpos(hash, _shift);
        if (_bitmap & bit) {
            return _nodes[map_index(_bitmap, bit)]->find(fbr, hash, key);
        } else {
            return std::nullopt;
        }
    }

    void 
    iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> value)> f) const override
    {
        for(auto i = 0; i < _size; i++) {
            _nodes[i]->iterate(f);
        }
    }

    size_t get_hash() const override {
        return _hash;
    }

   void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {  
        for(auto i = 0; i < _size; i++) {
            accept(_nodes[i]);
        }
    }

};

class LeafNode : public Node {
public:

    const size_t _hash;
    const gc::ref<Value> key_;
    const gc::ref<Value> val_;

    LeafNode(size_t hash, gc::ref<Value> key, gc::ref<Value> val)
            : _hash(hash), key_(key), val_(val) {
    }

    assoc_ret_t
    assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const override
    {
        if (hash == _hash) {
            if (this->key_->map_key_equals(fbr, *key)) {
                //replacing
                return {gc::make_ref<LeafNode>(fbr.allocator(), hash, key, val), false};
            } else {
                //hash collision - same hash, different keys
                /*
                LeafNode newLeaf = new LeafNode(hash, key, val);
                addedLeaf.val = newLeaf;
                return new HashCollisionNode(hash, this, newLeaf);
                */
                std::cerr << "collision!!! in leafnode" << std::endl;
                throw std::runtime_error("todo possible collision");
            }
        } else {
            return BitmapIndexedNode::create(fbr, this, level_shift, key, val, hash);
        }
    }

    find_ret_t 
    find(Fiber &fbr, size_t hash, const Value &key) const override {
        if (hash == _hash && key_->map_key_equals(fbr, key)) {
            return gc::ref<Node>(this);
        } else {
            return std::nullopt;
        }    
    }

    void 
    iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> value)> f) const override
    {
        f(key_, val_);
    }


    size_t get_hash() const override {
        return _hash;
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
        accept(key_);
        accept(val_);
    }

};

void insert_leaf_node_at_idx(Fiber & fbr, const gc::ref<Node> nodes_src[], size_t src_size, gc::ref<Node> nodes_dst[], uint32_t idx, gc::ref<Value> key, gc::ref<Value> val, size_t hash)
{
    std::copy_n(nodes_src, idx, nodes_dst);

    nodes_dst[idx] = gc::make_ref<LeafNode>(fbr.allocator(), hash, key, val);

    std::copy_n(nodes_src + idx, src_size - idx, nodes_dst + idx + 1);
}


Node::assoc_ret_t
BitmapIndexedNode::assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const
{
    auto bit = map_bitpos(hash, _shift);

    auto idx = map_index(_bitmap, bit);

    if (_bitmap & bit) {
        auto [n, leaf_added] = _nodes[idx]->assoc(fbr, _shift + 5, hash, key, val);

        if (n == _nodes[idx]) {
            return {this, leaf_added};
        } else {
            return {BitmapIndexedNode::create(fbr, _nodes, _size, _bitmap, _shift, idx, n), leaf_added};
        }
    } else {

        auto new_bitmap = _bitmap | bit;

        if (new_bitmap == 0xffffffff) {
            return {FullNode::create(fbr, _nodes, _size, _shift, idx, key, val, hash), true};
        }
        else {
            return {BitmapIndexedNode::create(fbr, _nodes, _size, new_bitmap, _shift, idx, key, val, hash), true};
        }
    }
}




class EmptyNode : public Node {
public:
    assoc_ret_t
    assoc(Fiber & fbr, size_t level_shift, size_t hash, gc::ref<Value> key, gc::ref<Value> val) const override
    {
        auto leaf_added = gc::make_ref<LeafNode>(fbr.allocator(), hash, key, val);
        return {leaf_added, true};
    }

    find_ret_t 
    find(Fiber &fbr, size_t hash, const Value &key) const override {
        return std::nullopt;
    }

    void 
    iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> value)> f) const override
    {
    }

    size_t get_hash() const override {
        return 0;
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {}

};




} //map impl


using namespace mapimpl;

class MapImpl : public ValueImpl<Map, MapImpl>  {

    static gc::ref<MapImpl> EMPTY;
    static gc::ref<Value> MAP2;
    static gc::ref<Value> ASSOC;
    static gc::ref<Value> LENGTH;
    static gc::ref<Value> GET;
    static gc::ref<Value> ITERATOR;
    static gc::ref<Value> CONTAINS;

    const size_t count_;
    const gc::ref<Node> root_;

public:
    MapImpl(size_t count, gc::ref<Node> root)
         : count_(count), root_(root) {}

    virtual size_t size() const override {
        return count_;
    }

    const Value &accept(Fiber &fbr, Visitor &visitor) const override {
        return visitor.visit(fbr, *this);
    }

    gc::ref<Map> assoc(Fiber & fbr, gc::ref<Value> key, gc::ref<Value> val) const override {
        size_t hash = key->map_key_hash(fbr);
        auto [new_root, leaf_added] = root_->assoc(fbr, 0, hash, key, val);
        if (new_root == root_) {
            return this;
        } else {
            return gc::make_ref<MapImpl>(fbr.allocator(), leaf_added ? count_ + 1 : count_, new_root);
        }
    }

    std::optional<gc::ref<Value>> get(Fiber &fbr, const Value &key) const override {
        size_t hash = key.map_key_hash(fbr);
        if (auto found = root_->find(fbr, hash, key)) {
            auto node = gc::ref_dynamic_cast<LeafNode>(*found);
            return node->val_;
        } else {
            return std::nullopt;
        }
    }

    void iterate(std::function<void(gc::ref<Value> key, gc::ref<Value> val)> f) const override {
        root_->iterate(f);
    }

    void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
        accept(root_);
    }

    void repr(Fiber &fbr, std::ostream &out) const override {
        out << "{";
        iterate([&](gc::ref<Value> key, gc::ref<Value> val) {
            key->repr(fbr, out);
            out << ": ";
            val->repr(fbr, out);
            out << ", ";
        });
        out << "}";
    }

    static int64_t _map2(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        auto checked = frame.check().
            static_dispatch(*MAP2).
            argument_count(0);

        if(!checked) {
            return checked.result();                
        }

        return frame.result<Value>(EMPTY);
    }

    static int64_t _assoc(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<MapImpl> map;
        gc::ref<Value> key;
        gc::ref<Value> val;

        auto checked = frame.check().
            single_dispatch(*ASSOC, *TYPE).
            argument_count(3).
            argument<MapImpl>(1, map).
            argument<Value>(2, key).
            argument<Value>(3, val);

        if(!checked) {
            return checked.result();
        }   

        return frame.result<Value>(map->assoc(fbr, key, val));
    }

    static int64_t _length(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<MapImpl> self;

        auto checked = frame.check().
            single_dispatch(*LENGTH, *TYPE).
            argument_count(1).
            argument<MapImpl>(1, self);

        if(!checked) {
            return checked.result();
        }

        return frame.result<int64_t>(self->count_);
    }

    static int64_t _get(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<MapImpl> self;
        gc::ref<Value> key;
        gc::ref<Value> default_value;

        auto checked = frame.check().
            single_dispatch(*GET, *TYPE).
            argument_count(2, 3).
            argument<MapImpl>(1, self).
            argument<Value>(2, key).
            optional_argument<Value>(3, default_value);

        if(!checked) {
            return checked.result();
        }

        if (auto found = self->get(fbr, *key)) {
            return frame.result<Value>(*found);
        } 
        else if(default_value) {
            return frame.result<Value>(default_value);
        }

        throw Error::key_not_found(fbr, *key);
    }

    static int64_t _iterator(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<MapImpl> self;

        auto checked = frame.check().
            single_dispatch(*ITERATOR, *TYPE).
            argument_count(1).
            argument<MapImpl>(1, self);

        if(!checked) {
            return checked.result();
        }

        auto keys = Vector::create(fbr);
        self->root_->iterate([&](gc::ref<Value> key, gc::ref<Value> value) {
            keys = keys->conj(fbr, key); 
        });
 
        return frame.result<Value>(keys);
    }

    static int64_t _contains(Fiber &fbr, const AST::Apply &apply) {
        Frame frame(fbr, apply);

        gc::ref<MapImpl> self;
        gc::ref<Value> key;

        auto checked = frame.check().
            single_dispatch(*CONTAINS, *TYPE).
            argument_count(2).
            argument<MapImpl>(1, self).
            argument<Value>(2, key);

        if(!checked) {
            return checked.result();
        }

        if (auto found = self->get(fbr, *key)) {
            return frame.result<bool>(true);
        } 
        else {
            return frame.result<bool>(false);
        }
    }

    static gc::ref<Map> create() {
        return EMPTY;
    }

    static void init(Runtime &runtime) {
        TYPE = runtime.create_type("Map");

        MAP2 = runtime.create_builtin<BuiltinStaticDispatch>("map2", _map2);

        ASSOC = runtime.builtin("assoc");
        runtime.register_method(ASSOC, *TYPE, _assoc);

        LENGTH = runtime.builtin("length");
        runtime.register_method(LENGTH, *TYPE, _length);

        GET = runtime.builtin("get");
        runtime.register_method(GET, *TYPE, _get);

        ITERATOR = runtime.builtin("iterator");
        runtime.register_method(ITERATOR, *TYPE, _iterator);

        CONTAINS = runtime.builtin("contains");
        runtime.register_method(CONTAINS, *TYPE, _contains);

        EMPTY = runtime.create_root<MapImpl>([&](gc::allocator_t &allocator) {
            return gc::make_shared_ref<MapImpl>(allocator, 0, gc::make_shared_ref<EmptyNode>(allocator));
        });
    }

};

gc::ref<MapImpl> MapImpl::EMPTY;
gc::ref<Value> MapImpl::MAP2;
gc::ref<Value> MapImpl::ASSOC;
gc::ref<Value> MapImpl::LENGTH;
gc::ref<Value> MapImpl::GET;
gc::ref<Value> MapImpl::ITERATOR;
gc::ref<Value> MapImpl::CONTAINS;

void Map::init(Runtime &runtime) {
    MapImpl::init(runtime);
}

gc::ref<Map> Map::create(Fiber &fbr) {
    return MapImpl::create();
}


}

