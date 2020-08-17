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

#ifndef __GC_BASE_H
#define __GC_BASE_H

#include <functional>
#include <array>
#include <cassert>
#include <stdlib.h>

namespace gc {

const size_t BLOCK_ALIGN = 1 << 20;
const intptr_t SHARED_BIT_MASK = 0x0000000000100000ULL;
const intptr_t BLOCK_MASK =      0xfffffffffff00000ULL;
const intptr_t OFFSET_MASK =     0x00000000000fffffULL;

/*
const size_t BLOCK_ALIGN = 1 << 24;
const intptr_t SHARED_BIT_MASK = 0x0000000001000000ULL;
const intptr_t BLOCK_MASK =      0xffffffffff000000ULL;
const intptr_t OFFSET_MASK =     0x0000000000ffffffULL;
*/

inline void fill_beef(char *ptr, size_t sz) {
	assert(sz % 4 == 0);
	for(auto cur = ptr; cur < (ptr+sz); cur += 4) {
		*reinterpret_cast<uint32_t *>(cur) = 0xEFBEADDE;
	}
}


//align to next 16 byte
template<typename T>
inline T align(T sz)
{
	return ((sz + 15) & -16);
}

template<typename T>
inline T *align(T* sz)
{
	return reinterpret_cast<T *>(((reinterpret_cast<intptr_t>(sz) + 15) & -16));
}

class block_t {

public:
	enum Type { local_block, shared_block };

public:
	std::unique_ptr<class block_t> next; //allows us to build linked lists

private:
#ifndef NDEBUG
	Type type_;
#endif
	bool dirty_ = 0;
	uint8_t select_ = 0;
	uint64_t bitmap_[8] = {};
	uint64_t marked_[8] = {};
	uint64_t finalize_[8] = {};

	int sz_;	
	size_t block_size_;

	int available_;
	int capacity_;

	alignas(16)  char data[];

public:

	//prevent stack allocation
	block_t(const block_t &) = delete; 
    block_t & operator=(const block_t &) = delete;
    block_t(block_t &&) = delete; 
    block_t & operator=(block_t &&) = delete; 

    int sz() const {
    	return sz_;
    }

	size_t block_size() const {
		return block_size_;
	}

	size_t data_size() const {
        return  reinterpret_cast<const char *>(this) + block_size_ - data;
	}

	bool full() const {
		return available_ == 0;
	}

	bool empty() const {
		return available_ == capacity_;
	}

	int used() const {
		return capacity_ - available_;
	}

	int capacity() const {
		return capacity_;
	}

	int used_bytes() const {
		return used() * sz_;
	}

	int available() const {
 		return available_;
	}

	int count() const {
		return 
			__builtin_popcountll(bitmap_[0]) +
			__builtin_popcountll(bitmap_[1]) +
			__builtin_popcountll(bitmap_[2]) +
			__builtin_popcountll(bitmap_[3]) +
			__builtin_popcountll(bitmap_[4]) +
			__builtin_popcountll(bitmap_[5]) +
			__builtin_popcountll(bitmap_[6]) +
			__builtin_popcountll(bitmap_[7]);
	}

	uint64_t mask64(int idx) const {
		assert(idx >= 0 && idx < 64);
		return uint64_t{1} << idx;
	}

	uint8_t mask8(int idx) const {
		assert(idx >= 0 && idx < 8);
		return uint8_t{1} << idx;
	}

	std::pair<int, int> split(int idx) const {
		// returns bitmap index and offset in bitmap from idx
		auto idx0 = idx / 64;
		auto idx1 = idx & 0x3f; 
		return {idx0, idx1};
	}

 	bool marked(int idx) const {
		auto [idx0, idx1] = split(idx);
		return marked_[idx0] & mask64(idx1);
	}

	void set_mark(int idx) {
		auto [idx0, idx1] = split(idx);
		marked_[idx0] |= mask64(idx1);
	}

	void unset_mark(int idx) {
		auto [idx0, idx1] = split(idx);		
		marked_[idx0] &= ~mask64(idx1);
	}

	bool set_mark_concurrent(int idx) {
		auto [idx0, idx1] = split(idx);
		auto mask = mask64(idx1);	
		return __sync_fetch_and_or(&marked_[idx0], mask) & mask;
	}

	bool unset_mark_concurrent(int idx) {
		auto [idx0, idx1] = split(idx);	
		auto mask = mask64(idx1);	
		return __sync_fetch_and_and(&marked_[idx0], ~mask) & mask;
	}

	void free(int idx)
	{
		assert(!empty());
		assert(used() == count());
		auto [idx0, idx1] = split(idx);		
		assert((bitmap_[idx0] & mask64(idx1)));
		assert(!(marked_[idx0] & mask64(idx1)));
		assert(!(finalize_[idx0] & mask64(idx1)));
		bitmap_[idx0] &= ~mask64(idx1);
		select_ &= ~mask8(idx0);
		available_ += 1;
		assert(!full());
		assert(used() == count());
	}

	void *alloc(bool with_finalizer, bool marked)
	{
		assert(!full());
		assert(used() == count());

		auto idx0 = __builtin_ffsl(~select_) - 1;
		assert(idx0 >= 0 && idx0 < 8);

		auto idx1 = __builtin_ffsll(~bitmap_[idx0]) - 1;
		assert(idx1 >= 0 && idx1 < 64);

		assert((bitmap_[idx0] & mask64(idx1)) == 0); //bit is really empty

		if((bitmap_[idx0] |= mask64(idx1)) == 0xffffffffffffffffULL) {
			select_ |= mask8(idx0);
		}

		if(with_finalizer) {
			finalize_[idx0] |= mask64(idx1);
		}

		if(marked) {
			marked_[idx0] |= mask64(idx1);
		}

		auto ptr = data + (((idx0 * 64) + idx1) * sz_);

		assert(((unsigned long)ptr & 15) == 0);
		assert(ptr >= data && ptr <= (reinterpret_cast<char *>(this) + block_size_ - sz_));

		available_ -= 1;

		assert(used() == count());

		return ptr;
	}

	void clear_marked() 
	{
		std::fill(std::begin(marked_), std::end( marked_), 0);
	}

	void clear_data()
    {
#ifndef NDEBUG
		fill_beef(data, data_size());
#endif
    }

	void clear() {
		std::fill( std::begin( bitmap_ ), std::end( bitmap_ ), 0 );
		clear_marked();
		std::fill( std::begin( finalize_ ), std::end( finalize_ ), 0 );
		select_ = 0;
		available_ = capacity_;
		assert(used() == count());
        clear_data();
	}

	void sweep();

    bool dirty_cas(bool mask)
    {
    	if(dirty_ == !mask) {
    		dirty_ = mask;
    		return true;
    	}
    	else {
    		return false;
    	}
    }

	static std::unique_ptr<block_t> create(Type type, size_t sz, bool dirty);

	void operator delete(void *data)
	{
		//make sure the shared/local bit is 0 so that data points to the original allocation
		::free(reinterpret_cast<void *>(reinterpret_cast<intptr_t>(data) & ~SHARED_BIT_MASK));
	}

	static block_t &block_from_ptr(const void *r)
	{
		assert(r != nullptr);
		auto &block = *reinterpret_cast<block_t *>((intptr_t)r & BLOCK_MASK);
		assert(reinterpret_cast<intptr_t>(r) & SHARED_BIT_MASK ? block.type_ == block_t::Type::shared_block : block.type_ == block_t::Type::local_block);
		return block;
	}

	static std::pair<block_t &, int> block_and_index_from_ptr(const void *r)
	{
		assert(r != nullptr);
		
		auto &block = block_from_ptr(r);

		uint32_t offset = ((intptr_t)r & OFFSET_MASK) - sizeof(block_t);

		//TODO if we know that sz is always multiple of 2, then we can shift
		//possibly  put that in templat param
		auto idx = offset / (float)block.sz_; //TODO check that float works always correct, it seems to be the fastest division (compared to int div)

		return {block, idx};
	}

private:
	block_t(Type type, size_t sz, size_t block_size, bool dirty)
	: dirty_(dirty), sz_(sz), block_size_(block_size), capacity_((block_size - sizeof(block_t)) / sz)
	{
#ifndef NDEBUG
		type_ = type;
#endif
		//std::cout << "new block: sz: " << sz << " cap: " << capacity_ << " data_sz: " << data_sz << std::endl;
		assert(sz % 16 == 0);
		assert(capacity_ >= 0 && capacity_ <= 512);
		available_= capacity_;
        clear_data();
	}
};


template<block_t::Type BlockType, int N, typename SZI>
struct heap_t
{
	using block_arr_t = std::array<std::unique_ptr<block_t>, N>;

	block_arr_t head_blocks_;
	block_arr_t rest_blocks_;
	block_arr_t empty_blocks_;
	block_arr_t full_blocks_; 

	int64_t allocated_ = 0;
	int64_t allocated_bytes_ = 0;

	int64_t freed_ = 0;
	int64_t freed_bytes_ = 0;

 	heap_t() : head_blocks_(), rest_blocks_(), empty_blocks_(), full_blocks_() {};

	int64_t used() const {
		assert(allocated_ >= freed_);
		return allocated_ - freed_;
	}

	int64_t used_bytes() const {
		assert(allocated_bytes_ >= freed_bytes_);
		return allocated_bytes_ - freed_bytes_;
	}

	bool valid_head(std::unique_ptr<block_t> &head)
	{
		return head && !head->full();
	}

	bool valid_head(size_t sz)
	{
		assert(sz % 16 == 0);
		return valid_head(head_blocks_[SZI()(sz)]);
	}

	block_t & next_head(size_t sz, bool dirty)
	{
		assert(sz % 16 == 0);
		auto szi = SZI()(sz);
		auto &head = head_blocks_[szi];
		while(!valid_head(head)) {	
			if(!head) {
				//check rest
				if(auto &rest_head = rest_blocks_[szi]) {
					//take from rest
					head = std::move(rest_head);
					rest_head = std::move(head->next);
				}
				else if(auto &empty_head = empty_blocks_[szi]) {
					//use available empty block
					head = std::move(empty_head);
					empty_head = std::move(head->next);
				}
				else {
					//create new
					head = block_t::create(BlockType, sz, dirty);
				}
			}
			else if(head->full()) {
				//move to full list
				auto full_block = std::move(head);
				head = std::move(full_block->next);
				full_block->next = std::move(full_blocks_[szi]);
				full_blocks_[szi] = std::move(full_block);
			}
		}
		return *head;
	}

	block_t &head(size_t sz)
	{
		assert(sz % 16 == 0);

		auto &head = head_blocks_[SZI()(sz)];

		return *head;
	}

	void count_blocks(int &num_blocks, int &num_full_blocks, int &num_empty_blocks, uint64_t &used_bytes, uint64_t &malloc_bytes)
	{
		auto count = [&](block_arr_t &blocks, int &block_cnt)
		{
			for(auto &block : blocks) {
				auto current = block.get();
				while(current) {
					block_cnt += 1;
					used_bytes += current->used_bytes();
					malloc_bytes += current->block_size();
					current = current->next.get();
				}
			}
		};

		count(head_blocks_, num_blocks);
		count(rest_blocks_, num_blocks);
		count(full_blocks_, num_full_blocks);
		count(empty_blocks_, num_empty_blocks);
	}

	template<typename Visitor>
	void for_each_block(Visitor visit)
	{
		//but not empty blocks
		auto visit_blocks = [&](block_arr_t &blocks)
		{
			for(auto &block : blocks) {
				auto current = block.get();
				while(current) {
					visit(*current);
					current = current->next.get();
				}
			}

		};

		visit_blocks(head_blocks_);
		visit_blocks(rest_blocks_);
		visit_blocks(full_blocks_);

	}	

	void redistribute_blocks(std::unique_ptr<block_t> &src, block_arr_t &rest_blocks, block_arr_t &full_blocks, block_arr_t &empty_blocks) {

		while(src) {
			//unlink head
			auto head = std::move(src);
			src = std::move(head->next);

			auto szi = SZI()(head->sz());

			if(head->full()) {
				auto &queue = full_blocks[szi];
				head->next = std::move(queue);
				queue = std::move(head);
			}
			else if(head->empty()) {
				auto &queue = empty_blocks[szi];
				head->next = std::move(queue);
				queue = std::move(head);
			}
			else {
				auto &queue = rest_blocks[szi];
				head->next = std::move(queue);
				queue = std::move(head);
			}
		}
	}

	void pop_empty_blocks()
	{
		for(auto &lst : empty_blocks_) {
			while(lst) {
				lst = std::move(lst->next);	
			}
		}
	}

	void redistribute_blocks(block_arr_t &src) {
		for(auto &lst : src) {
			redistribute_blocks(lst, rest_blocks_, full_blocks_, empty_blocks_);
		}
	}

	void redistribute_full_blocks() {

		block_arr_t new_full_blocks;

		for(auto &lst : full_blocks_) {
			redistribute_blocks(lst, rest_blocks_, new_full_blocks, empty_blocks_);
		}

		full_blocks_ = std::move(new_full_blocks);
	}

	void redistribute_heads_to_rest() {
		for(auto &head : head_blocks_) {
			if(head) {
				assert(!head->next);
				auto &rest = rest_blocks_[SZI()(head->sz())];
				head->next = std::move(rest);
				rest = std::move(head);
			}
		}
	}

};

struct szi_shared {
	int operator() (size_t sz) { return (sz / 16) - 1; }
};

struct szi_local {
	int operator() (size_t sz) { 
		assert(sz >= 512 && sz <= 1024 * 64); 
		assert((__builtin_ctz(sz) - 9) >= 0);
		assert((__builtin_ctz(sz) - 9) < 8);
		return __builtin_ctz(sz) - 9;
	}
};

using shared_heap_t = heap_t<block_t::Type::shared_block, 32, szi_shared>;  // 32 blocks of sz in 16 byte increments (16-512)
using local_heap_t = heap_t<block_t::Type::local_block, 8, szi_local>;


struct allocator_t;

struct chunk_deleter {
	void operator()(const char* b) { 
		auto [block, idx] = block_t::block_and_index_from_ptr(b);
		//std::cerr << "del chunk: " << reinterpret_cast<const void *>(b) << " blk sz: " << block.sz() << std::endl;
		block.free(idx);
	}
};

struct private_heap_t
{
	char *begin_ = nullptr;
	char *end_ = nullptr;
	char *cur_ = nullptr;

	int64_t allocated_ = 0;
	int64_t allocated_bytes_ = 0;

	int64_t freed_ = 0;
	int64_t freed_bytes_ = 0;

	struct header_t
	{
	    int32_t sz : 32;
	 	bool marked: 1;
		int64_t pad: 31;
	};

	static_assert(sizeof(header_t) == 8, "header_t should be 8 bytes");

	std::vector<std::unique_ptr<char, chunk_deleter>> chunks_;

	void ensure_capacity(allocator_t &allocator, size_t sz);

	size_t size();

	void *alloc(allocator_t &allocator, size_t sz)
	{
		//std::cerr << "private alloc: " << sz << std::endl;

		if((end_ - cur_) < (sz + 16)) {
			ensure_capacity(allocator, sz);
		}

		assert((end_ - cur_) >= (sz + 16));

		assert(reinterpret_cast<intptr_t>(cur_) % 16 == 0);
		cur_ += 8;
		reinterpret_cast<header_t *>(cur_)->sz = sz;
		reinterpret_cast<header_t *>(cur_)->marked = 0;
		cur_ += 8;
		assert(reinterpret_cast<intptr_t>(cur_) % 16 == 0);
		auto ptr = cur_;
		cur_ += sz;
		assert(reinterpret_cast<intptr_t>(cur_) % 16 == 0);

		allocated_bytes_ += sz;
		allocated_ += 1;

		return ptr;
	}

	void clear() {
		chunks_.clear();
	}

	static header_t &header(void *ptr)
	{
		return *(reinterpret_cast<header_t *>(reinterpret_cast<char *>(ptr) - 8));
	}

	static const header_t &header(const void *ptr)
	{
		return *(reinterpret_cast<const header_t *>(reinterpret_cast<const char *>(ptr) - 8));
	}

};

}



#endif
