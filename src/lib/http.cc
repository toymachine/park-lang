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

#include <iostream>

#include "http.h"
#include "fiber.h"
#include "vector.h"
#include "string.h"
#include "integer.h"
#include "builtin.h"
#include "type.h"
#include "error2.h"
#include "pool.h"

#include <set>
#include <queue>
#include <unordered_set>

#include <boost/asio/buffer.hpp>

#include "http_parser.h"

namespace park {
    namespace http {

        static gc::ref<Value> HTTP_SERVER;
        static gc::ref<Value> HTTP_ACCEPT_CONNECTION;
        static gc::ref<Value> HTTP_READ_REQUEST;
        static gc::ref<Value> HTTP_KEEP_ALIVE;
        static gc::ref<Value> HTTP_RESPONSE_FINISH;
        static gc::ref<Value> WRITE;
        static gc::ref<Value> CLOSE;

        class HTTPServer : public Value {
        };

        class HTTPRequest : public Value {
        };

        class HTTPConnection : public Value {
        };

        class HTTPConnectionImpl;

        extern struct http_parser_settings settings;

        class HTTPRequestImpl : public ValueImpl<HTTPRequest, HTTPRequestImpl> {
        private:
        public:
            HTTPRequestImpl() {};

            void repr(Fiber &fbr, std::ostream &out) const override {
                out << "<HTTPRequest>";
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            }

        };

        class HTTPServerImpl;

        //ToDO prevent concurrent usage of conneciton
        class HTTPConnectionImpl : public SharedValueImpl<HTTPConnection, HTTPConnectionImpl> {
        private:
            static Pool<std::array<char, 8192>> buffer_pool;

            enum class prev_header_cb_t {
                none, field, value
            };

            enum class state_t {
                INITIAL,
                HEADERS_COMPLETE
            };

            state_t state_;

            gc::ref<HTTPServerImpl> server_;
        
            std::unique_ptr<std::array<char, 8192>> buffer_; //TOO buffer pool

            http_parser parser_;
            prev_header_cb_t prev_header_cb;

            using header_pair = std::pair<const char *, size_t>;
            std::optional<header_pair> header_field_;
            std::optional<header_pair> header_value_;

            std::vector<std::pair<header_pair, header_pair>> headers_;

            void append_header();

        public:
            boost::asio::ip::tcp::socket socket_;

            HTTPConnectionImpl(gc::ref<HTTPServer> server, boost::asio::ip::tcp::socket socket)
                    : state_(state_t::INITIAL),
                      server_(server),
                      parser_(),
                      prev_header_cb(prev_header_cb_t::none),
                      socket_(std::move(socket))
            {
                socket_.non_blocking(true);
                assert(socket_.non_blocking());
                http_parser_init(&parser_, HTTP_REQUEST);
                parser_.data = this;
            }

            ~HTTPConnectionImpl() {
                release_buffer();
            }

            void ensure_buffer() {
                if(!buffer_) {
                    buffer_ = buffer_pool.acquire();
                }
            }

            void release_buffer() {
                if(buffer_) {
                    buffer_pool.release(std::move(buffer_));
                }
            }

            void close() {
                boost::system::error_code ec;
                socket_.close(ec);
                assert(!ec); //TODO
            }

            void repr(Fiber &fbr, std::ostream &out) const override {
                out << "<HTTPConnection " << this << ">";
            }

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
                accept(server_);
            }

            std::optional<gc::ref<Value>> read_request(Fiber &fbr, bool &throws);
            void read_request_async(Fiber &fbr);

            std::optional<gc::ref<Value>> write(Fiber &fbr, gc::ref<String> data);
            void write_async(Fiber &fbr, gc::ref<String> data);

            void response_finish();

            void parse_some(size_t bytes_transferred);
            bool keep_alive() const;

            int on_message_begin();

            int on_message_complete();

            int on_url(const char *at, size_t length);

            int on_header_field(const char *at, size_t length);

            int on_header_value(const char *at, size_t length);

            int on_headers_complete();

            int on_body(const char *at, size_t length);


        };

        class HTTPServerImpl : public SharedValueImpl<HTTPServer, HTTPServerImpl> {


            Runtime &runtime;
            boost::asio::ip::tcp::acceptor acceptor;

        public:

            HTTPServerImpl(Runtime &runtime,
                           const std::string &address, const std::string &port);

            void repr(Fiber &fbr, std::ostream &out) const override;

            void walk(const std::function<void(const gc::ref<gc::collectable> &ref)> &accept) override {
            }

            std::optional<gc::ref<Value>> accept_connection(Fiber &fbr);
            void accept_connection_async(Fiber &fbr);

            static int64_t _http_server(Fiber &fbr, const AST::Apply &apply);
        };

        Pool<std::array<char, 8192>> HTTPConnectionImpl::buffer_pool;

        int HTTPConnectionImpl::on_message_begin() {
            //std::cerr << "on_message_begin!" << std::endl;
            return 0;
        }

        int HTTPConnectionImpl::on_message_complete() {
            //std::cerr << "on_message_complete!" << std::endl;
            return 0;
        }

        int HTTPConnectionImpl::on_url(const char *at, size_t length) {
            //std::cerr << "on_url!: " << std::string(at, length) << std::endl;
            return 0;
        }

        bool HTTPConnectionImpl::keep_alive() const
        {
            return http_should_keep_alive(&parser_) && socket_.is_open();
        }

        void HTTPConnectionImpl::response_finish()
        {
            state_ = state_t::INITIAL;
        }


         std::optional<gc::ref<Value>> HTTPConnectionImpl::write(Fiber &fbr, gc::ref<String> data) {
            if (state_ != state_t::HEADERS_COMPLETE) {
                throw std::runtime_error("cannot send response in this connection state");
            }

            boost::system::error_code ec;

            auto bytes_written = boost::asio::write(socket_, data->buffer(), ec);
            assert(!ec);
            //std::cerr << "written bytes: " << bytes_written << std::endl;

            if(!ec)
            {
                //sync write completed
                return gc::ref_cast<Value>(Integer::create(fbr, bytes_written));
            }

            throw std::runtime_error("TODO sync write handle errors");
         }

         void HTTPConnectionImpl::write_async(Fiber &fbr, gc::ref<String> data) {

            if (state_ != state_t::HEADERS_COMPLETE) {
                throw std::runtime_error("cannot send response in this connection state");
            }


            throw std::runtime_error("TODO async write");

         }



        //should have connection->lock
        void HTTPConnectionImpl::append_header() {
            assert(state_ == state_t::INITIAL);
            headers_.emplace_back(*header_field_, *header_value_);
        }

        //locked trough read_some
        int HTTPConnectionImpl::on_header_field(const char *at, size_t length) {
            assert(state_ == state_t::INITIAL);
            switch (prev_header_cb) {
                case prev_header_cb_t::none: {
                    header_field_ = header_pair(at, length);
                    break;
                }
                case prev_header_cb_t::value: {
                    append_header();
                    header_field_ = header_pair(at, length);
                    header_value_ = std::nullopt;
                    break;
                }
                case prev_header_cb_t::field: {
                    //header_field.append(at, length);
                    assert(false);
                    break;
                }
            }
            prev_header_cb = prev_header_cb_t::field;
            return 0;
        }

        //locked trough read_some
        int HTTPConnectionImpl::on_header_value(const char *at, size_t length) {
            assert(state_ == state_t::INITIAL);
            switch (prev_header_cb) {
                case prev_header_cb_t::none: {
                    throw std::runtime_error("bad state");
                }
                case prev_header_cb_t::field: {
                    header_value_ = header_pair(at, length);
                    break;
                }
                case prev_header_cb_t::value: {
                    //header_value.append(at, length);
                    assert(false);
                    break;
                }
            }
            prev_header_cb = prev_header_cb_t::value;
            return 0;
        }


        //has lock trough read_some
        int
        HTTPConnectionImpl::on_headers_complete() {

            assert(state_ == state_t::INITIAL);

            append_header(); //finalize headers

            /*
            std::cout << "on_headers_complete, http: " << parser_.http_major << "." << parser_.http_minor
                      << " should ka?: " << http_should_keep_alive(&parser_) << " #headers: " << headers_.size() <<  std::endl;

            for(auto &header : headers_) {
                std::cerr << std::string(header.first.first, header.first.second) << ": " << 
                    std::string(header.second.first, header.second.second) << std::endl;
            }
            */

            state_ = state_t::HEADERS_COMPLETE;

            return 0;
        }


        int HTTPConnectionImpl::on_body(const char *at, size_t length) {
            std::cout << "on_body!" << std::string(at, length) << std::endl;
            assert(false);
            return 0;
        }

        HTTPServerImpl::HTTPServerImpl(
                Runtime &runtime,
                const std::string &address,
                const std::string &port)
                : runtime(runtime),
                  acceptor(runtime.io_service) {

            boost::asio::ip::tcp::resolver resolver(runtime.io_service);
            boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve({address, port});
            acceptor.open(endpoint.protocol());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.non_blocking(true);
            acceptor.listen();
            assert(acceptor.non_blocking());
        }

        void HTTPServerImpl::repr(Fiber &fbr, std::ostream &out) const {
            out << "<HTTPServer>";

        }

        int on_message_begin(http_parser *parser) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_message_begin();
        }

        int on_message_complete(http_parser *parser) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_message_complete();
        }

        int on_url(http_parser *parser, const char *at, size_t length) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_url(at, length);
        }

        int on_header_field(http_parser *parser, const char *at, size_t length) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_header_field(at, length);
        }

        int on_header_value(http_parser *parser, const char *at, size_t length) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_header_value(at, length);
        }

        int on_headers_complete(http_parser *parser) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_headers_complete();
        }

        int on_body(http_parser *parser, const char *at, size_t length) {
            return static_cast<HTTPConnectionImpl *>(parser->data)->on_body(at, length);
        }

        struct http_parser_settings settings = {
                .on_message_begin = on_message_begin,
                .on_url = on_url,
                .on_header_field = on_header_field,
                .on_header_value = on_header_value,
                .on_headers_complete = on_headers_complete,
                .on_body = on_body,
                .on_message_complete = on_message_complete
        };

        int64_t HTTPServerImpl::_http_server(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<String> address;
            gc::ref<String> port;

            return frame.check().
                static_dispatch(*HTTP_SERVER).
                argument_count(2).
                argument<String>(1, address).
                argument<String>(2, port).
                result<Value>([&]() {
                    auto server = gc::make_shared_ref<HTTPServerImpl>(fbr.allocator(), 
                        Runtime::from_fbr(fbr), address->to_string(fbr), port->to_string(fbr));
                    return server;
                });
        }

        std::optional<gc::ref<Value>> HTTPServerImpl::accept_connection(Fiber &fbr) {

            boost::system::error_code ec;

            auto socket = acceptor.accept(ec);
            if(!ec)
            {
                //server accepted sync
                return gc::ref_cast<Value>(gc::make_shared_ref<HTTPConnectionImpl>(fbr.allocator(), this, std::move(socket)));
            }

            if(ec.value() == boost::system::errc::operation_would_block) {
                return std::nullopt;
            }

            std::cerr << ec.message() << std::endl;
            throw std::runtime_error("TODO other errors accept_connection");
        }

        void HTTPServerImpl::accept_connection_async(Fiber &fbr) {
            auto socket = std::make_unique<boost::asio::ip::tcp::socket>(runtime.io_service);
            acceptor.async_accept(*socket,
                [&fbr, this, socket=std::move(socket)](boost::system::error_code ec) {
                    if(!ec) {
                        fbr.resume_sync([&](Fiber &fbr) {
                            fbr.stack.push<gc::ref<Value>>(gc::make_shared_ref<HTTPConnectionImpl>(fbr.allocator(), this, std::move(*socket)));
                        }, 0);
                        return;
                    }
                    std::cerr << ec.message() << std::endl;
                    throw std::runtime_error("TODO other errors accept_connection_async");
                });
        }

        int64_t _http_accept_connection(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPServerImpl> server;

            return frame.check().
                static_dispatch(*HTTP_ACCEPT_CONNECTION).
                argument_count(1).
                argument<HTTPServerImpl>(1, server).
                result_or_block<Value>(
                    [&](bool &throws) {
                        return server.mutate()->accept_connection(fbr);
                    },
                    [server](Fiber &fbr) {
                        server.mutate()->accept_connection_async(fbr);
                    });
        }

        void HTTPConnectionImpl::parse_some(size_t bytes_transferred)
        {
            auto nparsed = http_parser_execute(&parser_, &settings, buffer_->data(), bytes_transferred);
            //std::cerr << "http_parser_executed!, nparsed: " << nparsed << std::endl;
            if (nparsed != bytes_transferred) {
                throw std::runtime_error("TODO check nparsed");
            }
            if(state_ != state_t::HEADERS_COMPLETE) {
                throw std::runtime_error("TODO read more");
            }
        }

        std::optional<gc::ref<Value>> HTTPConnectionImpl::read_request(Fiber &fbr, bool &throws) {
            
            //std::cerr << "conn read request sync!" << std::endl;
            assert(state_ == state_t::INITIAL);
            
            header_field_ = std::nullopt;
            header_value_ = std::nullopt;
            headers_.clear();

            boost::system::error_code ec;

            ensure_buffer();
            auto bytes_transferred = socket_.read_some(boost::asio::buffer(*buffer_), ec);
            if(!ec) {
                //read some synchronousely
                //std::cerr << "read_request: " << bytes_transferred << " bytes" << std::endl;
                parse_some(bytes_transferred);
                assert(state_ == state_t::HEADERS_COMPLETE);
                release_buffer();
                return gc::ref_cast<Value>(gc::make_ref<HTTPRequestImpl>(fbr.allocator()));
            }

            if(ec.value() == boost::system::errc::operation_would_block) {
                //std::cerr << "read request would block" << std::endl;
                return std::nullopt;
            }

            //some error
            throws = true;
            return gc::ref_cast<Value>(Error2::create(fbr, ec.message()));
        }

        void HTTPConnectionImpl::read_request_async(Fiber &fbr)
        {
           /*std::cerr << "conn read request async!" << std::endl;*/
           socket_.async_read_some(boost::asio::buffer(*buffer_), 
                [&](boost::system::error_code ec, std::size_t bytes_transferred) {
                if(!ec) {
                    parse_some(bytes_transferred);
                    assert(state_ == state_t::HEADERS_COMPLETE);
                    release_buffer();
                    fbr.resume_sync([&](Fiber &fbr) {
                        fbr.stack.push<gc::ref<Value>>(gc::make_ref<HTTPRequestImpl>(fbr.allocator()));
                    }, 0);
                }
                else {
                    fbr.resume_sync([&](Fiber &fbr) {
                        //std::cerr << "read_request_async async with 1 msg: " << ec.message() << std::endl;
                        fbr.stack.push<gc::ref<Value>>(Error2::create(fbr, ec.message()));
                    }, 1);
                }
           });
        }

        int64_t _http_read_request(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPConnectionImpl> connection;

            return frame.check().
                static_dispatch(*HTTP_READ_REQUEST).
                argument_count(1).
                argument<HTTPConnectionImpl>(1, connection).
                result_or_block<Value>(
                    [&](bool &throws) {
                        //std::cerr << "ret read request, sync" << std::endl;
                        return connection.mutate()->read_request(fbr, throws);
                    },
                    [connection](Fiber &fbr) {
                        //std::cerr << "ret read request, aasync" << std::endl;
                        connection.mutate()->read_request_async(fbr);
                    });
        }

        int64_t _http_response_finish(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPConnectionImpl> connection;

            return frame.check().
                static_dispatch(*HTTP_RESPONSE_FINISH).
                argument_count(1).
                argument<HTTPConnectionImpl>(1, connection).
                result<bool>(
                    [&]() {
                        connection.mutate()->response_finish();
                        return true;
                    });
        }

        int64_t _http_keep_alive(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPConnectionImpl> connection;

            return frame.check().
                static_dispatch(*HTTP_KEEP_ALIVE).
                argument_count(1).
                argument<HTTPConnectionImpl>(1, connection).
                result<bool>(
                    [&]() {
                        return connection->keep_alive();
                    });
        }

        int64_t _write(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPConnectionImpl> connection;
            gc::ref<String> data;

            return frame.check().
                single_dispatch(*WRITE, *HTTPConnectionImpl::TYPE).
                argument_count(2).
                argument<HTTPConnectionImpl>(1, connection).
                argument<String>(2, data).
                result_or_block<Value>(
                    [&](bool &throws) {
                        return connection.mutate()->write(fbr, data);
                    },
                    [connection, data](Fiber &fbr) {
                        connection.mutate()->write_async(fbr, data);
                    });
        }

        int64_t _close(Fiber &fbr, const AST::Apply &apply) {
            Frame frame(fbr, apply);

            gc::ref<HTTPConnectionImpl> connection;

            return frame.check().
                single_dispatch(*CLOSE, *HTTPConnectionImpl::TYPE).
                argument_count(1).
                argument<HTTPConnectionImpl>(1, connection).
                result<bool>(
                    [&]() {
                        connection.mutate()->close();
                        return true; //TODO what is return type?
                    });
        }

        void init(Runtime &runtime) {
            HTTPServerImpl::TYPE = runtime.create_type("HTTPServer");

            HTTP_SERVER = runtime.create_builtin<BuiltinStaticDispatch>("http_server", HTTPServerImpl::_http_server);
            HTTP_ACCEPT_CONNECTION = runtime.create_builtin<BuiltinStaticDispatch>("http_accept_connection", _http_accept_connection);
            HTTP_READ_REQUEST = runtime.create_builtin<BuiltinStaticDispatch>("http_read_request", _http_read_request);
            HTTP_KEEP_ALIVE = runtime.create_builtin<BuiltinStaticDispatch>("http_keepalive", _http_keep_alive);
            HTTP_RESPONSE_FINISH = runtime.create_builtin<BuiltinStaticDispatch>("http_response_finish", _http_response_finish);

            HTTPConnectionImpl::TYPE = runtime.create_type("HTTPConnection");

            WRITE = runtime.builtin("write");
            runtime.register_method(WRITE, *HTTPConnectionImpl::TYPE, _write);

            CLOSE = runtime.builtin("close");
            runtime.register_method(CLOSE, *HTTPConnectionImpl::TYPE, _close);
        }
    }
}
