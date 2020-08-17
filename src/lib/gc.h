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

#ifndef __GC_H
#define __GC_H


#include <deque>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <vector>
#include <unordered_set>

#include "gc_ref.h"
//IDEAS: 
//TODO:

namespace gc {

using namespace std::chrono_literals;

using for_each_root_t = std::function<void(const std::function<void(const ref<collectable> &)> &)>;
using for_each_root_set_t = std::function<void(const std::function<void(const for_each_root_t)>)>;

using ref_list_t = std::vector<const collectable *>;

extern std::array<std::mutex, 1024> block_locks;

inline std::mutex &block_lock_for(block_t &block) {
    //TODO how good is the hash wrt high bits always being 00?
    auto idx = std::hash<void *>{}(&block) % 1024;
    return block_locks[idx];
}

struct collector_t;

struct allocator_t 
{
	explicit allocator_t(collector_t &collector) :
		collector_(collector),
		private_heap_(std::make_unique<private_heap_t>()),
		local_heap_(std::make_unique<local_heap_t>()),
		shared_heap_(std::make_unique<shared_heap_t>()) {}


	bool over_treshold_ = false;

	std::mutex lock_;

	collector_t &collector_;

	bool dirty_mask_ = false;

    std::unique_ptr<private_heap_t> private_heap_; 
    std::unique_ptr<local_heap_t> local_heap_; 
	std::unique_ptr<shared_heap_t> shared_heap_;

	bool write_barrier_ = false;
	std::atomic<bool> local_collect_barrier_ = false;

    ref_list_t ref_list_;

	int64_t allocated_ = 0;
	int64_t allocated_bytes_ = 0;

	int64_t last_allocated_bytes_ = 0;
	int64_t last_allocated_bytes_shared_ = 0;
	int64_t delta_allocated_bytes_shared_ = 0;

	int64_t freed_ = 0;
	int64_t freed_bytes_ = 0;

	int64_t shared_ = 0;
	int64_t shared_allocated_ = 0;
	int64_t shared_allocated_bytes_ = 0;

	int64_t shared_freed_ = 0;
	int64_t shared_freed_bytes_ = 0;

	std::chrono::duration<double> collection_time_seconds {0};
	std::chrono::duration<double> mark_time_seconds {0};
	std::chrono::duration<double> sweep_time_seconds {0};

	std::chrono::duration<double> current_pause_seconds {0};
	std::chrono::duration<double> longest_pause_seconds {0};

	size_t nr_collections_ = 0;

    void *alloc_private(size_t sz)
	{

		void *ptr = private_heap_->alloc(*this, sz);

		//std::cerr << "pvt ptr returned: " << ptr << " of sz: " << private_heap_t::sz(ptr) << std::endl;
		
		return ptr;
	}

    void *alloc_local(size_t sz)
    {
		allocated_ += 1;
		allocated_bytes_ += sz;

		if(local_heap_->valid_head(sz)) {
			return local_heap_->head(sz).alloc(false, false);
		}
		else {
			return local_heap_->next_head(sz, false).alloc(false, false);
		}
    }


    void *alloc_shared(size_t sz, bool with_finalizer)
	{
		shared_allocated_ += 1;
		shared_allocated_bytes_ += sz;
		if(shared_heap_->valid_head(sz)) {
			return shared_heap_->head(sz).alloc(with_finalizer, write_barrier_);
		}
		else {
		    //this is interleaved with the background gc doing sweep,
		    //we only call this for every 512 allocations of this sz
            //e.g. amortized over number of slots in block (512)
			lock_.lock();
			auto &head = shared_heap_->next_head(sz, false);
			lock_.unlock();
			auto &block_lock = block_lock_for(head);
			block_lock.lock();
			auto freed = sweep(head);
			block_lock.unlock();
			lock_.lock();
			shared_freed_ += freed;
			shared_freed_bytes_ += freed * head.sz();		
			lock_.unlock();
			return head.alloc(with_finalizer, write_barrier_);
		}
	}

	void share(const ref<collectable> &o);


	//requires lock or stw
	int sweep(block_t &block);
	void sweep_heads();
	void sweep_concurrent();
	void sweep_final();

	bool must_collect_local();
    void collect_local(const for_each_root_t &for_each_root);
    void collect_local_to_local(const for_each_root_t &for_each_root);
    void collect_local_to_shared(const for_each_root_t &for_each_root);

};

//the write barrier
template<typename T>
inline void ref_write(allocator_t &allocator, ref<T> &slot, ref<T> src)
{	
    allocator.share(src);
	if(allocator.write_barrier_) {
		std::lock_guard<std::mutex> lock_guard(allocator.lock_);		
		allocator.ref_list_.push_back(slot.get());
		allocator.ref_list_.push_back(src.get());
    }
    slot = src;
}

inline void ref_share(allocator_t &allocator, ref<collectable> &r) {
    allocator.share(r);
}

struct worker_t
{
    std::thread thread;
    ref_list_t ref_list_;
};

extern void dump(const collectable *r);

using for_each_allocator_t = std::function<void(const std::function<void(allocator_t &)> &)>;

struct collector_t
{
	std::mutex &lock_;

	std::condition_variable stw_mutators_alloc_cv;
	std::condition_variable stw_mutators_wait_cv;
	std::condition_variable stw_workers_wait_cv;
	std::condition_variable stw_collector_wait_cv;
	std::atomic<bool> stw_mutators_wait = false;

	std::deque<std::function<void(worker_t &worker)>> stw_work_todo;

	std::array<worker_t, 4> workers_;

	bool workers_started_ = false;
	bool workers_stopped_ = false;

	int num_busy_workers_ = 0;
    int num_stopped_mutators_ = 0;

	uint64_t delta_allocated_bytes_shared = 0;
	uint64_t num_shared_collections = 0;

	struct stats_t {
		//stats
		uint64_t num_local_collections = 0;
		uint64_t num_shared_collections = 0;

		uint64_t num_local_allocated = 0;
		uint64_t num_local_allocated_bytes = 0;
		uint64_t num_local_freed = 0;
		uint64_t num_local_freed_bytes = 0;
		uint64_t num_local_shared = 0;

		uint64_t num_shared_allocated = 0;
		uint64_t num_shared_allocated_bytes = 0;

		uint64_t num_shared_freed = 0;
		uint64_t num_shared_freed_bytes = 0;

		uint64_t num_local_malloc_bytes = 0;
		uint64_t num_shared_malloc_bytes = 0;

		uint64_t num_local_used_bytes = 0;
		uint64_t num_shared_used_bytes = 0;

		int num_local_blocks = 0;
		int num_local_full_blocks = 0;
		int num_local_empty_blocks = 0;

		int num_shared_blocks = 0;
		int num_shared_full_blocks = 0;
		int num_shared_empty_blocks = 0;

		std::chrono::duration<double> local_collection_time_seconds {0};
		std::chrono::duration<double> local_collection_mark_time_seconds {0};
		std::chrono::duration<double> local_collection_sweep_time_seconds {0};
		std::chrono::duration<double> longest_pause_seconds {0};
		std::chrono::duration<double> current_pause_seconds {0};

		void show();
	};

	collector_t(std::mutex &lock)
	    : lock_(lock) {}

    void start();
    void stop();
	void notify();

	void perform_work(worker_t &worker, std::unique_lock<std::mutex> &lock);
	void perform_all_work(std::unique_lock<std::mutex> &lock);

	//mutator checkin
	void checkin_local(allocator_t &allocator, const for_each_root_t &for_each_root);
    void checkin_shared(allocator_t &allocator, std::unique_lock<std::mutex> &lock);

	void parallel_scan(std::unique_lock<std::mutex> &lock, for_each_root_set_t for_each_root_set);

	stats_t calc_stats(for_each_allocator_t for_each_allocator);

	void mark_concurrent(std::vector<const collectable *> &grey);

	void collect_shared(std::function<bool()> collecting, 
					    std::function<int()> nr_mutators_to_stop, 
						std::function<void(int n)> stw_start, 
						std::function<void(int n)> stw_end, 
						for_each_root_set_t initial_root_sets, 
						std::function<bool()> has_incremental_root_sets, 
						for_each_root_set_t incremental_root_set, 
						std::function<void()> incremental_root_sets_done,
						for_each_allocator_t for_each_allocator);

	void collect_shared_final(for_each_allocator_t for_each_allocator);
};



inline void make_shared(allocator_t &allocator, const ref<collectable> &r) {
	allocator.share(const_cast<ref<collectable> &>(r));
}

template<typename T, typename... Args> 
const ref<T> make_ref(allocator_t &allocator, Args&&... args)
{
	static_assert(std::is_base_of<collectable, T>::value, "can only make ref to collectable types");
	static_assert(sizeof(T) <= 512, "can only allocate up to and including 512 bytes");
	static_assert(std::is_trivially_destructible<T>::value, "type must be trivially destructable");
	auto sz = align(sizeof(T));
	auto slot = allocator.alloc_private(sz);
	//std::cerr << "alloccing " << typeid(T).name() << std::endl;
	new (slot) T(std::forward<Args>(args)...);
	return ref<T>(reinterpret_cast<T *>(slot));
}

template<typename T, typename... Args> 
const ref<T> make_shared_ref(allocator_t &allocator, Args&&... args)
{
	static_assert(std::is_base_of<collectable, T>::value, "can only make ref to collectable types");
	static_assert(std::is_trivially_destructible<T>::value || std::has_virtual_destructor<T>::value, "virtual destructor needed, and make sure to call it from overridden finalize method");
	static_assert(sizeof(T) <= 512, "can only allocate up to and including 512 bytes");
	//TODO call destructor in sweep
	auto sz = align(sizeof(T));
	auto slot = allocator.alloc_shared(sz, !std::is_trivially_destructible<T>::value);
	new (slot) T(std::forward<Args>(args)...);
	return ref<T>(reinterpret_cast<T *>(slot));
}

template<typename T, typename ELT, typename... Args> 
const ref<T> make_ref_fam(allocator_t &allocator, size_t num_elt, Args&&... args)
{
	static_assert(std::is_base_of<collectable, T>::value, "can only make ref to collectable types");
	static_assert(std::is_trivially_destructible<T>::value, "type must be trivially destructable");

	//std::cout << "allocsz: " << sizeof(T) << " + sz: " << sz << std::endl;
	assert((sizeof(T) + num_elt * sizeof(ELT)) <= 512);
	auto sz = align(sizeof(T) + num_elt * sizeof(ELT));
	auto slot = allocator.alloc_private(sz);
	new (slot) T(std::forward<Args>(args)...);
	return ref<T>(reinterpret_cast<T *>(slot));
}

template<typename T, typename ELT, typename... Args> 
const ref<T> make_shared_ref_fam(allocator_t &allocator, size_t num_elt, Args&&... args)
{
	static_assert(std::is_base_of<collectable, T>::value, "can only make ref to collectable types");
	static_assert(std::is_trivially_destructible<T>::value || std::has_virtual_destructor<T>::value, "virtual destructor needed, and make sure to call it from overridden finalize method");
	assert((sizeof(T) + num_elt * sizeof(ELT)) <= 512);
	auto sz = align(sizeof(T) + num_elt * sizeof(ELT));
	auto slot = allocator.alloc_shared(sz, !std::is_trivially_destructible<T>::value);
	new (slot) T(std::forward<Args>(args)...);
	return ref<T>(reinterpret_cast<T *>(slot));
}

}


#endif
