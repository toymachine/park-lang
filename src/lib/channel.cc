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

#include <deque>
#include <mutex>

#include "runtime.h"
#include "channel.h"
#include "fiber.h"
#include "type.h"
#include "boolean.h"
#include "frame.h"
#include "builtin.h"

namespace park {

    class ChannelImpl : public SharedValueImpl<Channel, ChannelImpl> {
    private:
        std::mutex lock_; //TODO use object locks array instead

        //use intrusive list on fibers for these:
        //TODO interactions of these with gc?, yes they would need write barrier
        std::deque<gc::ref<Fiber>> receivers_;
        std::deque<std::pair<gc::ref<Fiber>, gc::ref<Value>>> senders_;

        static gc::ref<Value> RECV;
        static gc::ref<Value> SEND;
        static gc::ref<Value> CHANNEL;

    public:
        static void init(Runtime &runtime);

        void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            lock_.lock();
            auto senders = senders_;
            auto receivers = receivers_;
            lock_.unlock();
            for(auto &item : receivers) {
                accept(item);
            }
            for(auto &item : senders) {
                accept(item.first);
                accept(item.second);
            }
        }

        static int64_t _recv(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<ChannelImpl> channel;

            return frame.check().
                single_dispatch(*RECV, *TYPE).
                argument_count(1).
                argument<ChannelImpl>(1, channel).
                cc_resume([channel](Fiber &fbr) {
                    std::lock_guard<std::mutex> lock_guard(channel.mutate()->lock_);

                    auto &receiver = fbr;

                    if (!channel->senders_.empty()) {
                        auto sending = channel->senders_.front(); 
                        channel.mutate()->senders_.pop_front();
                        auto &sender = sending.first;
                        receiver.stack.push<gc::ref<Value>>(sending.second);
                        sender.mutate()->resume_async([result=sending.second](Fiber &fbr) {
                            fbr.stack.push<gc::ref<Value>>(result);
                        }, 0);
                        return true; // no block, resume with result
                    }
                    else {
                        //no sender
                        channel.mutate()->receivers_.emplace_back(&receiver);
                        return false; // block
                    }
                });                
        }

        static int64_t _send(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<ChannelImpl> channel;
            gc::ref<Value> value;

            //TODO what happens with value if gc happens before block callback is called?
            return frame.check().
                single_dispatch(*SEND, *TYPE).
                argument_count(2).
                argument<ChannelImpl>(1, channel).
                argument<Value>(2, value).
                cc_resume([channel, value](Fiber &fbr) -> bool {
                    std::lock_guard<std::mutex> lock_guard(channel.mutate()->lock_);

                    auto &sender = fbr;

                    gc::make_shared(fbr.allocator(), value);

                    if (!channel->receivers_.empty()) {
                        // a receiver is present
                        auto receiver = channel->receivers_.front(); 
                        channel.mutate()->receivers_.pop_front();
                        receiver.mutate()->resume_async([value](Fiber &fbr) {
                            fbr.stack.push<gc::ref<Value>>(value);
                        }, 0);
                        sender.stack.push<gc::ref<Value>>(value);
                        return true; //continue
                    }
                    else {
                        // no receiver
                        channel.mutate()->senders_.emplace_back(&sender, value);
                        return false; //block
                    }
                });
        }

        static int64_t _channel(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            return frame.check().
                static_dispatch(*CHANNEL).
                argument_count(0).
                result<Value>([&]() {
                    return Channel::create(fbr);
                });
        }

        void repr(Fiber &fbr, std::ostream &out) const override {
            out << "<channel " << this << ">";
        }
    };

    gc::ref<Channel> Channel::create(Fiber &fbr) {
        return gc::make_shared_ref<ChannelImpl>(fbr.allocator());
    }

    gc::ref<Value> ChannelImpl::RECV;
    gc::ref<Value> ChannelImpl::SEND;
    gc::ref<Value> ChannelImpl::CHANNEL;

    void ChannelImpl::init(Runtime &runtime) {
        TYPE = runtime.create_type("Channel");

        RECV = runtime.builtin("recv");
        SEND = runtime.builtin("send");

        runtime.register_method(RECV, *TYPE, _recv);
        runtime.register_method(SEND, *TYPE, _send);

        CHANNEL = runtime.create_builtin<BuiltinStaticDispatch>("channel", _channel);
    }

    void Channel::init(Runtime &runtime) {
        ChannelImpl::init(runtime);
    }

}
