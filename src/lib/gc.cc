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

#include "gc.h"

#include <cstring>
#include <stdint.h>

namespace gc {

const int LOCAL_COLLECT_TRESHOLD = 4 * 1024 * 1024;

void private_heap_t::ensure_capacity(allocator_t &allocator, size_t sz)
{
    assert(sz <= 512);

    while((end_ - cur_) < (sz + 16)) {
        //start with 512 byte chunk, increase by 2x of last chunk until it succeeds
        auto chunk_size =  chunks_.empty() ? 512 : 
            std::min(block_t::block_from_ptr(chunks_.back().get()).sz() * 2, 65536);
        chunks_.emplace_back(reinterpret_cast<char *>(allocator.alloc_local(chunk_size)));
        begin_ = chunks_.back().get();
        end_ = begin_ + chunk_size;
        cur_ = begin_;
        //std::cerr << "created chunk of sz: " << chunk_size << " ptr: " << reinterpret_cast<void *>(begin_) << std::endl;
    }
}

size_t private_heap_t::size()
{
    size_t sum = 0;
    for(auto &chunk : chunks_) {
        sum += block_t::block_from_ptr(chunk.get()).sz();
    }
    return sum;
}

std::array<std::mutex, 1024> block_locks;

std::unique_ptr<block_t> block_t::create(Type type, size_t sz, bool dirty)
{
    //static_assert(std::is_standard_layout<block_t>::value, "block_t must be std layout");

    assert(sz % 16 == 0);
    assert(sz > 0 && sz <= 65536);

    auto block_size = std::min(sz * 512, BLOCK_ALIGN);

    void *data;
    if(posix_memalign(&data, BLOCK_ALIGN * 2 , BLOCK_ALIGN * 2)) {
        exit(666);
    }
    
    //because we have made room for 2 blocks, this bit indicates 
    //wheter we place it in the first or second halve.
    //by doing this we can determine if a pointer is shared or local by looking at the bit
    if(type == shared_block) {
        data = reinterpret_cast<void *>(reinterpret_cast<intptr_t>(data) | SHARED_BIT_MASK);
    }


    /*
    void *data = std::aligned_alloc( BLOCK_ALIGN, BLOCK_ALIGN );
    if(data == nullptr) {
        exit(666);
    }
    */

    auto block = std::unique_ptr<block_t>(new (data) block_t(type, sz, block_size, dirty));

    //std::cerr << "create blk data: " << data << " tp: " << type << " sz: " << sz << " bit: " << bool((reinterpret_cast<intptr_t>(data) & SHARED_BIT_MASK)) << " block_size: " << block->block_size() << " cap " << block->capacity() << std::endl;

    return block;
}

//call without lock
void collector_t::start()
{
    std::unique_lock<std::mutex> lock(lock_);

    if(!workers_started_) {
        for(auto &worker : workers_) {
            worker.thread = std::thread([&]() {
                std::unique_lock<std::mutex> lock(lock_);
                while(true) {
                    stw_workers_wait_cv.wait(lock, [&]{ return !stw_work_todo.empty() || workers_stopped_;});
                    if(workers_stopped_) {
                        break;
                    }
                    perform_work(worker, lock);
                }
            });
        }
        workers_started_ = true;
    }
}

//call without lock
void collector_t::stop()
{
    std::unique_lock<std::mutex> lock(lock_);

    workers_stopped_ = true;
    stw_workers_wait_cv.notify_all();
    lock.unlock();
    for(auto &worker : workers_) {
        worker.thread.join();
    }
    lock.lock();
}

void collector_t::perform_work(worker_t &worker, std::unique_lock<std::mutex> &lock)
{ //needs to hold lock on entry
    while(!stw_work_todo.empty()) {
        auto work_item = stw_work_todo.front();
        stw_work_todo.pop_front();
        num_busy_workers_ += 1;
        lock.unlock();
        work_item(worker);
        lock.lock();
        num_busy_workers_ -= 1;
        stw_collector_wait_cv.notify_one();
    }
}

void collector_t::perform_all_work(std::unique_lock<std::mutex> &lock)
{
    //needs to hold lock on entry
    stw_workers_wait_cv.notify_all();
    stw_collector_wait_cv.wait(lock, [&]{ return stw_work_todo.empty() && num_busy_workers_ == 0;});
}

void collector_t::notify()
{
    stw_mutators_alloc_cv.notify_one();
}


void collector_t::mark_concurrent(std::vector<const collectable *> &grey)
{
    while(!grey.empty()) {
        auto r = grey.back();
        grey.pop_back();
        auto [block, idx] = block_t::block_and_index_from_ptr(r);
        if(!block.set_mark_concurrent(idx)) {
            const_cast<collectable *>(r)->walk([&](auto &r1) {
                grey.push_back(r1.get());
            });
        }
        if(grey.size() > 128) {
            auto half = grey.size() / 2;
            std::vector<const collectable *> grey2(grey.begin() + half, grey.end());
            grey.resize(half);
            {
                std::lock_guard<std::mutex> guard(lock_);
                stw_work_todo.emplace_back([this,grey2=std::move(grey2)](auto &worker) mutable {
                    mark_concurrent(grey2);
                });
                //std::cout << "# " << stw_work_todo.size() << std::endl;
                stw_workers_wait_cv.notify_one();
            }
        }
    }
}

void collector_t::parallel_scan(std::unique_lock<std::mutex> &lock, for_each_root_set_t for_each_root_set)
{
        //set up work items to scan shared roots for each root set
        //and build up shared root list per worker
        for_each_root_set([&](auto for_each_root) {
            stw_work_todo.emplace_back([for_each_root](auto &worker) {
                scan_shared_roots(for_each_root, worker);
            });
        });
        //do the actual scanning of roots
        perform_all_work(lock);

        //initial mark lists are now setup in each workers ref_list, queue concurrent marking tasks
		for(auto &worker : workers_) {
			stw_work_todo.emplace_back([this,grey=std::move(worker.ref_list_)](auto &worker) mutable {
				mark_concurrent(grey);
			});				
		}

        //do the actual marking of roots
        perform_all_work(lock);
}

void collector_t::collect_shared_final(for_each_allocator_t for_each_allocator)
{
	std::unique_lock<std::mutex> lock(lock_);

    for_each_allocator([&](auto &allocator) {
        stw_work_todo.emplace_back([&](auto &worker) {
            allocator.sweep_final();
        });
    });
    perform_all_work(lock);    
}

void collector_t::stats_t::show()
{
    if(num_local_collections > 0) {

        std::cout << "num local collections: " << num_local_collections <<
            " avg local collection time: " << std::chrono::duration_cast<std::chrono::microseconds>(local_collection_time_seconds / num_local_collections).count() << " us" <<
            " avg local mark time: " << std::chrono::duration_cast<std::chrono::microseconds>(local_collection_mark_time_seconds / num_local_collections).count() << " us" <<
            " avg local sweep time: " << std::chrono::duration_cast<std::chrono::microseconds>(local_collection_sweep_time_seconds / num_local_collections).count() << " us" << std::endl;
    }

    std::cout << "local allocated: " << num_local_allocated << " local freed: " << num_local_freed << " local shared: " << num_local_shared << " balance: " << (num_local_allocated - num_local_freed) << std::endl;
    std::cout << "local bytes allocated: " << num_local_allocated_bytes << " local freed bytes: " << num_local_freed_bytes << " balance: " << (num_local_allocated_bytes - num_local_freed_bytes) / 1e6 << " Mb, malloc: " << num_local_malloc_bytes / 1e6 << " Mb, used bytes: " << num_local_used_bytes / 1e6 << " Mb" << std::endl;
    std::cout << "#local blocks: " << num_local_blocks << " #local full blocks: " << num_local_full_blocks << " #local empty_blocks: " << num_local_empty_blocks << std::endl;

    std::cout << "num shared collections: " << num_shared_collections << std::endl;
    std::cout << "shared allocated: " << num_shared_allocated << " shared freed: " << num_shared_freed << " balance: " << (num_shared_allocated - num_shared_freed) << std::endl;
    std::cout << "shared allocated bytes: " << num_shared_allocated_bytes << " shared freed bytes: " << num_shared_freed_bytes << " balance: " << (num_shared_allocated_bytes - num_shared_freed_bytes) / 1e6 << " Mb, malloc: " << num_shared_malloc_bytes / 1e6 << " Mb, used bytes: " << num_shared_used_bytes / 1e6 << " Mb" << std::endl;
    std::cout << "#shared blocks: " << num_shared_blocks << " #shared full blocks: " << num_shared_full_blocks << " #shared empty_blocks: " << num_shared_empty_blocks << std::endl;

    std::cout << "longest mutator pause: " << std::chrono::duration_cast<std::chrono::microseconds>(longest_pause_seconds).count() << std::endl;
    std::cout << "current mutator pause: " << std::chrono::duration_cast<std::chrono::microseconds>(current_pause_seconds).count() << std::endl;

}

collector_t::stats_t collector_t::calc_stats(for_each_allocator_t for_each_allocator)
{
    stats_t stats;

    stats.current_pause_seconds = 0s;

    for_each_allocator([&](auto &allocator) {

        stats.num_local_collections += allocator.nr_collections_;
        stats.num_shared_collections = num_shared_collections;
        stats.local_collection_time_seconds += allocator.collection_time_seconds;
        stats.local_collection_mark_time_seconds += allocator.mark_time_seconds;
        stats.local_collection_sweep_time_seconds += allocator.sweep_time_seconds;
        
        stats.num_local_allocated += allocator.allocated_;
        stats.num_local_allocated_bytes += allocator.allocated_bytes_;
        stats.num_local_freed += allocator.freed_;
        stats.num_local_freed_bytes += allocator.freed_bytes_;
        stats.num_local_shared += allocator.shared_;

        stats.num_shared_allocated += allocator.shared_allocated_;
        stats.num_shared_allocated_bytes += allocator.shared_allocated_bytes_;

        stats.num_shared_freed += allocator.shared_freed_;
        stats.num_shared_freed_bytes += allocator.shared_freed_bytes_;

        if(allocator.longest_pause_seconds > stats.longest_pause_seconds) {
            stats.longest_pause_seconds = allocator.longest_pause_seconds;
        }

        if(allocator.current_pause_seconds > stats.current_pause_seconds) {
            stats.current_pause_seconds = allocator.current_pause_seconds;
        }

        allocator.local_heap_->count_blocks(stats.num_local_blocks, stats.num_local_full_blocks, stats.num_local_empty_blocks, stats.num_local_used_bytes, stats.num_local_malloc_bytes);
        allocator.shared_heap_->count_blocks(stats.num_shared_blocks, stats.num_shared_full_blocks, stats.num_shared_empty_blocks,  stats.num_shared_used_bytes, stats.num_shared_malloc_bytes);

    });

    return stats;
}

void collector_t::collect_shared(
    std::function<bool()> collecting, 
    std::function<int()> nr_mutators_to_stop, 
    std::function<void(int n)> stw_start, 
    std::function<void(int n)> stw_end, 
    for_each_root_set_t initial_root_sets, 
    std::function<bool()> has_incremental_root_sets, 
    for_each_root_set_t incremental_root_sets, 
    std::function<void()> incremental_root_sets_done, 
    for_each_allocator_t for_each_allocator)
{
	std::unique_lock<std::mutex> lock(lock_);

	while(true) {
		//wait till we reach threshold
		if(!stw_mutators_alloc_cv.wait_for(lock, 10s, [&]{ 
            //return delta_allocated_bytes_shared > (1 * 1024 * 1024) ;
            return delta_allocated_bytes_shared > (100 * 1024 * 1024) || !collecting() ;
        })) {
			std::cout << "collect on timeout delta_allocated_bytes_shared: " << delta_allocated_bytes_shared << std::endl; //timeout
			//continue;
		}

        if(!collecting()) {
            break;
        } 

        std::cout << "shared collect start with delta bytes: " << delta_allocated_bytes_shared << std::endl; //timeout

		num_shared_collections += 1;
		delta_allocated_bytes_shared = 0;

        auto start = std::chrono::high_resolution_clock::now();

       // std::cout << "1st stw start" << std::endl;
        //signal mutators and wait till they are checkedin
		stw_mutators_wait = true;
        stw_start(1);
		stw_collector_wait_cv.wait(lock, [&]{ return num_stopped_mutators_ == nr_mutators_to_stop() ;});
		//stw achieved (1st time)

        for_each_allocator([&](auto &allocator) {
            allocator.local_heap_->pop_empty_blocks();
            allocator.shared_heap_->pop_empty_blocks();
        });

        std::cout << "1st stw achieved" << std::endl;
#ifndef NDEBUG
        auto show_stats = true;
#else
        auto show_stats = true;
#endif
		if(show_stats) {
			auto stats = calc_stats(for_each_allocator);
			stats.show();
		}



        auto snapshot_start = std::chrono::high_resolution_clock::now();

        std::vector<std::vector<const gc::collectable *>> snapshots;
        initial_root_sets([&](auto for_each_root) {
            snapshots.push_back({});
            assert(snapshots.back().size() == 0);
            for_each_root([&](auto &r) {
                snapshots.back().push_back(r.get());
            });
            //std::cerr << "# snapshot with size: " << snapshots.back().size() << std::endl;
        });
        //std::cerr << "# snapshots: " << snapshots.size() << std::endl;

        //update write barrier for each allocator
        for_each_allocator([&](auto &allocator) {
            allocator.write_barrier_ = true;
            allocator.local_collect_barrier_ = true; //prevent local collection until we scanned roots
        });

        auto snapshot_end = std::chrono::high_resolution_clock::now();

 		//let mutators go (1st time) and do some mutating again
		stw_mutators_wait = false;
        stw_end(1);
		stw_mutators_wait_cv.notify_all();
        
		//concurrent/parallel mark/scan
        auto mark_start = std::chrono::high_resolution_clock::now();

        //first mark the snapshots, so that we can re-enable
        //local collection soon
        //e.g. local collection must be disabled to prevent concurrent modification
        //of the tree we are following here by local collection moveing stuff arround
        parallel_scan(lock, [&](auto for_each_root_set) {
            for(auto &snapshot : snapshots) {
                for_each_root_set([&](auto for_each_root) {
                    for(auto r : snapshot) {
                        for_each_root(r);
                    }
                });
            }
        });

        //snapshot marked, allow local collects again
        for_each_allocator([&](auto &allocator) {
            allocator.local_collect_barrier_ = false; 
        });

        //mark incremental rootsets (e.g. all the sleeping fibers
        //in batches, interleaved with stuff coming from write barrier
        while(has_incremental_root_sets()) {
            parallel_scan(lock, incremental_root_sets);
            incremental_root_sets_done();            

            //interleave with work coming in from write_barrier
            for_each_allocator([&](auto &allocator) {
                //std::cerr << "concur scan reflist# " << allocator.ref_list_.size() << std::endl;
                std::lock_guard<std::mutex> lock_guard(allocator.lock_);
                stw_work_todo.emplace_back([this,grey=std::move(allocator.ref_list_)](auto &worker) mutable {
                    mark_concurrent(grey);
                });				
            });
            perform_all_work(lock);
        }


		auto mark_end = std::chrono::high_resolution_clock::now();


        //prepare for 2nd stw
        //std::cout << "2nd stw start" << std::endl;
		stw_mutators_wait = true;
        stw_start(2);
		stw_collector_wait_cv.wait(lock, [&]{ return num_stopped_mutators_ == nr_mutators_to_stop() ;});
		//stw achieved (2nd time)
        //std::cout << "2nd stw achieved" << std::endl;

        auto remark_start = std::chrono::high_resolution_clock::now();

        //anything left in allocators from write barrier
        for_each_allocator([&](auto &allocator) {
            //std::cerr << "rescan reflist# " << allocator.ref_list_.size() << std::endl;
			stw_work_todo.emplace_back([this,grey=std::move(allocator.ref_list_)](auto &worker) mutable {
				mark_concurrent(grey);
			});				
		});
        perform_all_work(lock);        

		//sweep heads and reset dirty mask (used to do par here, but since sweep heads is so simple now, this is faster)
    
        for_each_allocator([&](auto &allocator) {
            assert(allocator.ref_list_.empty());
            allocator.write_barrier_ = false;
            allocator.dirty_mask_ = !allocator.dirty_mask_;
            allocator.sweep_heads();
		});

        auto remark_end = std::chrono::high_resolution_clock::now();

        //let mutators go 2nd time
		stw_mutators_wait = false;
        stw_end(2);
		stw_mutators_wait_cv.notify_all();

        //concurrent/parallel sweep rest
		auto sweep_start = std::chrono::high_resolution_clock::now();

        for_each_allocator([&](auto &allocator) {
            stw_work_todo.emplace_back([&](auto &worker) {
                allocator.sweep_concurrent();
            });
		});
        perform_all_work(lock);

		auto sweep_end = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double> snapshot_time = snapshot_end - snapshot_start;
		std::chrono::duration<double> mark_time = mark_end - mark_start;
		std::chrono::duration<double> remark_time = remark_end - remark_start;
		std::chrono::duration<double> sweep_time = sweep_end - sweep_start;

        auto end = std::chrono::high_resolution_clock::now();

        if(true) {
	    	std::cout << "shared collect done in " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us" << std::endl;
	    	std::cout << "stw. snapshot:  " << std::chrono::duration_cast<std::chrono::microseconds>(snapshot_time).count() << " us" << std::endl;
        	std::cout << "con. mark:  " << std::chrono::duration_cast<std::chrono::microseconds>(mark_time).count() << " us" << std::endl;
        	std::cout << "stw. remark:  " << std::chrono::duration_cast<std::chrono::microseconds>(remark_time).count() << " us" << std::endl;
	        std::cout << "con. sweep: " << std::chrono::duration_cast<std::chrono::microseconds>(sweep_time).count() << " us " << std::endl;
            std::cout << "total stw time: " << std::chrono::duration_cast<std::chrono::microseconds>(snapshot_time + remark_time).count() << " us " << std::endl;
            std::cout << "1st stw time: " << std::chrono::duration_cast<std::chrono::microseconds>(snapshot_time).count() << " us " << std::endl;
            std::cout << "2nd stw time: " << std::chrono::duration_cast<std::chrono::microseconds>(remark_time).count() << " us " << std::endl;

	    }
	}
}

//requires lock or stw
int allocator_t::sweep(block_t &block)
{
    if(block.dirty_cas(dirty_mask_)) {
        auto available_before = block.available();
        block.sweep();
        return block.available() - available_before;
    }
    else {
        return 0;
    }
}

void allocator_t::sweep_final()
{
    shared_heap_->for_each_block([](auto &block) {
        block.sweep();
    });
}

//initial sweep on stw
void allocator_t::sweep_heads()
{
    //this will just move any head blocks to rest_blocks where
    //they will be incrementally swept in the following phase (non-stw)
    //the first new allocation will interlock with gc to get a new head block
    shared_heap_->redistribute_heads_to_rest();
}

//runs on gc thread, runs interlocked with alloc_shared
void allocator_t::sweep_concurrent()
{
    auto sweep_ = [&](shared_heap_t::block_arr_t &blocks) {
        for(auto &block : blocks) {
            auto current = block.get();
            while(current) {
                lock_.unlock();
                auto &block_lock = block_lock_for(*current);
                block_lock.lock();
                auto freed = sweep(*current);
                block_lock.unlock();
                lock_.lock();
                shared_freed_ += freed;
                shared_freed_bytes_ += freed * current->sz();
                current = current->next.get();
            }
        }
    };

    lock_.lock();

    sweep_(shared_heap_->rest_blocks_);
    sweep_(shared_heap_->full_blocks_);

    shared_heap_->redistribute_full_blocks();

    lock_.unlock();
}

void allocator_t::share(const ref<collectable> &o)
{
    std::function<void(const ref<collectable> &)> share_ = [&](auto &r) {

        if(is_shared_ref(r)) {
            return;
        }

        auto &header = private_heap_t::header(r.get());

		assert(header.sz % 16 == 0);

        auto old_ptr = static_cast<void *>(const_cast<collectable *>(r.get()));
        auto new_ptr = alloc_shared(header.sz, false);

        shared_ += 1;

        std::memcpy(new_ptr, old_ptr, header.sz);

        static_cast<collectable *>(new_ptr)->walk(share_);

        const_cast<ref<collectable> &>(r) = static_cast<const collectable *>(new_ptr);

    };

    share_(o);
}

void scan_shared_roots(const for_each_root_t &for_each_root, worker_t &worker)
{
    //int level = 0;
    std::function<void(const collectable *)> scan_roots = [&](auto r) {
        assert(r != nullptr);


        if(is_shared_ref(r)) {
            /*
            for(int i = 0; i < level; i++) {
                std::cerr << " ";
            }
            std::cerr << level << "*";
            dump(r);
            std::cerr << " " << r << std::endl;
            */
            worker.ref_list_.push_back(r);  
        }
        else {
            /*
            for(int i = 0; i < level; i++) {
                std::cerr << " ";
            }
            std::cerr << level << " ";
            dump(r);
            std::cerr << " " << r << std::endl;

            level += 1;
            */
            const_cast<collectable *>(r)->walk([&](auto &r1) {
                scan_roots(r1.get());
            });
            //level -= 1;
        }
    };

    for_each_root([&](auto &r) {
        scan_roots(r.get());
    });
}


void allocator_t::collect_local(const for_each_root_t &for_each_root)
{
    collect_local_to_local(for_each_root);

    //TODO local to shared if over treshold

    /*
    if(over_treshold_) {
        collect_local_to_shared(for_each_root);
        assert(!over_treshold_);
    }
    *
    //else {
    //}
    */
}

void allocator_t::collect_local_to_shared(const for_each_root_t &for_each_root)
{
    std::cout << "collect local to shared" << std::endl;

    //auto start = std::chrono::high_resolution_clock::now();

    assert(false);
    /*
    //auto used_bytes_at_start = local_heap_->allocated_bytes_ - local_heap_->freed_bytes_;

    std::function<void(const ref<collectable> &)> mark_and_copy = [&](auto &r) {


        if(is_shared_ref(r)) {
            return;
        }

        auto [block, idx] = block_t::block_and_index_from_ptr(r.get());

        void *new_ptr;
        void *old_ptr = static_cast<void *>(const_cast<collectable *>(r.get()));

        if(block.marked(idx)) {
            //already copied then forwarding ptr is in place
            new_ptr = *reinterpret_cast<void **>(old_ptr);
        }
        else {
            //copy to new heap
            block.set_mark(idx);

            new_ptr = alloc_shared(block.sz(), false);

            std::memcpy(new_ptr, old_ptr, block.sz());

            *reinterpret_cast<void **>(old_ptr) = new_ptr; //in place of the object leave a forwarding pointer

            static_cast<collectable *>(new_ptr)->walk(mark_and_copy);
        }

        const_cast<ref<collectable> &>(r) = static_cast<const collectable *>(new_ptr);
    };

    for_each_root(mark_and_copy);

    //sweep
    local_heap_->for_each_block([&](auto &block) {
        local_heap_->freed_ += block.used();
        local_heap_->freed_bytes_ += block.used_bytes();
        block.clear();
    });

    if(write_barrier) {
        //all roots are now shared roots
        for_each_root([&](auto &r) {
            ref_list_.push_back(r.get());
        });
        collector_.forward_shared_roots(*this);
	    assert(ref_list_.empty());
    }

    //all of local_heap_ is now empty, this will move all blocks to empty_blocks_
    local_heap_->redistribute_blocks(local_heap_->head_blocks_);
    local_heap_->redistribute_blocks(local_heap_->rest_blocks_);
    local_heap_->redistribute_blocks(local_heap_->full_blocks_);

    auto end = std::chrono::high_resolution_clock::now();

    auto used_bytes_at_end = local_heap_->allocated_bytes_ - local_heap_->freed_bytes_;

    assert(used_bytes_at_end == 0);

    //auto freed_bytes = (used_bytes_at_start - used_bytes_at_end);

    //update stats
    nr_collections_ += 1;

    collection_time_seconds += end - start;

    last_allocated_bytes_ = local_heap_->allocated_bytes_;

    delta_allocated_bytes_shared_ = shared_allocated_bytes_ - last_allocated_bytes_shared_;
    last_allocated_bytes_shared_ = shared_allocated_bytes_;

    over_treshold_ =  used_bytes_at_end > LOCAL_COLLECT_TRESHOLD;

    //std::cout << "collect local to shared done in: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
    //std::cout << "collect local to shared bytes: " << freed_bytes << " used bytes at end: " << used_bytes_at_end << " over tresh: " << (used_bytes_at_end > LOCAL_COLLECT_TRESHOLD) << std::endl;
    */
}

void allocator_t::collect_local_to_local(const for_each_root_t &for_each_root)
{
    bool stats = false;

    if(stats) {
        std::cout << "collect local to local!" << std::endl;
    }

    auto start = std::chrono::high_resolution_clock::now();

    auto used_at_start = private_heap_->allocated_ - private_heap_->freed_;
    auto used_bytes_at_start = private_heap_->allocated_bytes_ - private_heap_->freed_bytes_;

    private_heap_t new_private_heap;  

    std::function<void(const ref<collectable> &)> mark_and_copy = [&](auto &r) {

        if(is_shared_ref(r)) {
            return;
        }

        auto &header = private_heap_t::header(r.mutate());

        void *new_ptr;
        void *old_ptr = static_cast<void *>(const_cast<collectable *>(r.get()));

        if(header.marked) {
            //then forwarding ptr is in place, could be that we find same ref twice
            //so we only copy once, and in this 2nd case update from the fwd ptr
            new_ptr = *reinterpret_cast<void **>(old_ptr);
        }
        else {
            //copy to new heap
            header.marked = true;

            new_ptr = new_private_heap.alloc(*this, header.sz);

            std::memcpy(new_ptr, old_ptr, header.sz);

            *reinterpret_cast<void **>(old_ptr) = new_ptr; //in place of the object leave a forwarding pointer

            static_cast<collectable *>(new_ptr)->walk(mark_and_copy);
        }

        const_cast<ref<collectable> &>(r) = static_cast<const collectable *>(new_ptr);
    };

    for_each_root(mark_and_copy);

    auto freed = used_at_start - new_private_heap.allocated_;
    auto freed_bytes = used_bytes_at_start - new_private_heap.allocated_bytes_;

    new_private_heap.allocated_ = private_heap_->allocated_;
    new_private_heap.allocated_bytes_ = private_heap_->allocated_bytes_;

    new_private_heap.freed_ = private_heap_->freed_ + freed;
    new_private_heap.freed_bytes_ = private_heap_->freed_bytes_ + freed_bytes;

    std::swap(*private_heap_, new_private_heap);

    local_heap_->redistribute_full_blocks();

    auto end = std::chrono::high_resolution_clock::now();

    //update stats
    nr_collections_ += 1;

    collection_time_seconds += end - start;

    delta_allocated_bytes_shared_ = shared_allocated_bytes_ - last_allocated_bytes_shared_;
    last_allocated_bytes_shared_ = shared_allocated_bytes_;

    auto used_at_end = private_heap_->allocated_ - private_heap_->freed_;
    auto used_bytes_at_end = private_heap_->allocated_bytes_ - private_heap_->freed_bytes_;

    //over_treshold_ =  used_bytes_at_end > LOCAL_COLLECT_TRESHOLD;

    if(stats) {
        std::cout << "collect local done in: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
        std::cout << " used bytes at start: " << used_bytes_at_start << std::endl;
        std::cout << " used bytes at end  : " << used_bytes_at_end << std::endl;
    //std::cout << "collect local freed bytes: " <<  used bytes at end: " << used_bytes_at_end << " pct clean: " << freed_bytes / (double)used_bytes_at_end << " over tresh: " << (used_bytes_at_end > LOCAL_COLLECT_TRESHOLD) << std::endl;
    }
}

//mutator checkin, call with lock
void collector_t::checkin_shared(allocator_t &allocator, std::unique_lock<std::mutex> &lock)
{
    auto start = std::chrono::high_resolution_clock::now();

    //shared gc requested stop the world pause, we will wait
    //and potentially be involved in parallel work
    num_stopped_mutators_ += 1;
    stw_collector_wait_cv.notify_one();
    //now we are 'checked in', and wait for gc to finish or give us something to do, or asks us to stop
    stw_mutators_wait_cv.wait(lock, [&]{ return !stw_mutators_wait;});

    num_stopped_mutators_ -= 1;
    stw_collector_wait_cv.notify_one();
    //stw pause done

    auto duration = std::chrono::high_resolution_clock::now() - start;
    if(duration > allocator.longest_pause_seconds) {
        allocator.longest_pause_seconds = duration;
    }

    auto alpha = 0.9;

    allocator.current_pause_seconds = allocator.current_pause_seconds * alpha + duration * (1.0 - alpha);
}

bool allocator_t::must_collect_local() {
    bool over_threshold = (private_heap_->allocated_bytes_ - private_heap_->freed_bytes_) > LOCAL_COLLECT_TRESHOLD;
    return over_threshold && !local_collect_barrier_;    
}

//mutator checkin
void collector_t::checkin_local(allocator_t &allocator, const for_each_root_t &for_each_root)
{
    if(allocator.must_collect_local()) {
        //local gc
        allocator.collect_local(for_each_root);
        //notify collector
        std::lock_guard<std::mutex> guard(lock_);
        delta_allocated_bytes_shared += allocator.delta_allocated_bytes_shared_;
        stw_mutators_alloc_cv.notify_one();
    }
}

void block_t::sweep() {
    assert(used() == count());

    select_ = 0;

    for(auto i = 0; i < 8; i++) {

#ifndef NDEBUG
        uint64_t freed = bitmap_[i] & ~marked_[i];
#endif

        bitmap_[i] = marked_[i];

        if(bitmap_[i] == 0xffffffffffffffffULL) {
            select_ |= mask8(i);
        }

        //run destructors if needed
        uint64_t to_finalize = finalize_[i] & ~marked_[i];
        if(to_finalize) {
            for(int j = 0; j < 64; j++) {
                auto idx = (i * 64) + j;
                if(idx < capacity_ && to_finalize & mask64(j)) {
                    auto collectable = reinterpret_cast<class collectable *>(data + idx * sz_);
                    //std::cout << "destructing: " << typeid(*collectable).name() << std::endl;
                    collectable->finalize();
                    finalize_[i] &= ~mask64(j);
                }
            }
        }

#ifndef NDEBUG		
        for(int j = 0; j < 64; j++) {
            auto idx = (i * 64) + j;
            if(idx < capacity_ && (freed & mask64(j))) {
                auto ptr = data + idx * sz_;
                char *block_start = reinterpret_cast<char *>(this);
                char *block_end = block_start + block_size_;
                assert(ptr >= block_start && ptr <= (block_end - sz_));
                //auto collectable = reinterpret_cast<class collectable *>(ptr);
                //if(!strcmp(typeid(*collectable).name(), "N4park11ClosureImplE")) {
                //    std::cout << "destructing: " << typeid(*collectable).name() << std::endl;
                //}
                fill_beef(ptr, sz_);
            }
        }
#endif
    }
    clear_marked();

    available_ = capacity_ - count();
    assert(used() == count());
}

}

