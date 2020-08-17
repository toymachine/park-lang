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

#include "integer.h"
#include "boolean.h"
#include "type.h"
#include "builtin.h"
#include "range.h"

namespace park {

    class RangeImpl : public ValueImpl<Range, RangeImpl> {
    private:
        int64_t start_;
        int64_t end_;
    public:
        RangeImpl(int64_t start, int64_t end) : start_(start), end_(end) {
            if (start > end) {
                throw std::runtime_error("invalid range");
            }
        }

        /*

        virtual const Value &first(Fiber &fbr) const override
        {
          if((end - start) > 0) {
            return Integer::from_integer(fbr, start);
          }
          else {
            throw std::runtime_error("empty range");
          }
        }

        virtual const Value &next(Fiber &fbr) const override
        {
          if((start + 1) <= end) {
            return *new(fbr) RangeImpl(start + 1, end);
          }
          else {
            throw std::runtime_error("end of range");
          }
        }
        */

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "(range " << start_ << " " << end_ << ")";
        }

        static void init(Runtime &runtime) {
            TYPE = runtime.create_type("Range");
        }

    };

    /*
    const Value &Range::from_range(Fiber &fbr, int64_t start, int64_t end) {
        return *new(fbr) RangeImpl(start, end);
    }
    */

    void Range::init(Runtime &runtime) {
        RangeImpl::init(runtime);
    }

}
