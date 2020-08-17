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

#ifndef __POOL_H
#define __POOL_H

template<typename T>
class Pool
{
private:
    std::vector<std::unique_ptr<T>> pool_;
    std::mutex lock_;

public:

    std::unique_ptr<T> acquire() 
    {
        std::lock_guard<std::mutex> guard(lock_);
        if(pool_.empty()) {
            return std::make_unique<T>();
        }
        else {
            auto obj = std::move(pool_.back());
            pool_.pop_back();
            return obj;
        }
    }

    void release(std::unique_ptr<T> ptr) {
        std::lock_guard<std::mutex> guard(lock_);
        pool_.emplace_back(std::move(ptr));
    }

};

#endif