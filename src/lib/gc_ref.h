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

#ifndef GC_REF_H
#define GC_REF_H

#include "gc_base.h"

namespace gc {

template<typename T>
class ref {
	const T *ptr;

public:	
	ref(const T *ptr) : ptr(ptr) {}

	ref() : ptr(nullptr) {}

	const T* operator->() const
    {
    	assert(ptr != nullptr);
        return ptr;
    }

	const T& operator* () const
    {
    	assert(ptr != nullptr);
        return *ptr;
    }

    const T* get() const
    {
    	assert(ptr != nullptr);
    	return ptr;
    }

    T *mutate() const 
    {
    	assert(ptr != nullptr);
    	return const_cast<T *>(ptr);
    }

    bool operator==(const ref<T> &that) const
    {
    	return ptr == that.ptr;
    }

	operator bool () const { return ptr != nullptr; }

	//TODO on upcast perform dynamic check at debug time
    template<typename Y>
    operator const ref<Y>&() const { 
    	static_assert(std::is_base_of<Y,T>::value || std::is_base_of<T,Y>::value, "not in same type hierarchy" );
		//static_assert(std::is_base_of<Y,T>::value, "can only upcast" );
    	return reinterpret_cast<const ref<Y>&>(*this);
    }


};



template<typename Y, typename T>
const ref<Y> ref_cast(const ref<T> &other) {
	return static_cast<ref<Y>>(other);
}

template<typename Y, typename T>
const ref<Y> &ref_dynamic_cast(const ref<T> &other) {
	static_assert(std::is_base_of<Y,T>::value || std::is_base_of<T,Y>::value, "not in same type hierarchy" );
#ifndef NDEBUG
	auto ptr_y = dynamic_cast<const Y*>(other.get());
	if(ptr_y != nullptr) {
		return reinterpret_cast<const ref<Y>&>(other);
	}
	else {
		throw std::runtime_error("ref_dynamic_cast failed");
	}
#else
	return static_cast<const ref<Y>&>(other);
#endif
}


class collectable
{
public:
	virtual void finalize() {
#ifndef NDEBUG
		std::cerr << "pls implement finalize() for class " << typeid(*this).name() << std::endl;
		assert(false);
#endif
	};

	virtual void walk(const std::function<void(const ref<collectable> &)> &accept) = 0;
};



template<typename T>
class with_finalizer : public T
{
public:
	virtual ~with_finalizer() = default;

	void finalize() override {
		this->~with_finalizer();
	}
};

inline bool is_shared_ref(const collectable *r)
{
	return reinterpret_cast<intptr_t>(r) & SHARED_BIT_MASK;
}

inline bool is_shared_ref(gc::ref<collectable> r)
{
	return is_shared_ref(r.get());
}

}

namespace std
{
    template <typename T>
    struct hash<gc::ref<T>>
    {
        size_t operator()(const gc::ref<T>& k) const
        {
            return std::hash<const T *>{}(k.get());
        }
    };
}

#endif 