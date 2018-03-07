// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_SERVER_REQUEST_H

/// The interface for when IRCd plays the role of client to other servers
///
namespace ircd::server
{
	struct in;
	struct out;
	struct request;

	size_t size(const in &);
	size_t size(const out &);

	void submit(const hostport &, request &);
	bool cancel(request &);
}

/// Request data and options related to transmitting the request. This
/// is where buffers must be supplied to send data to the server.
///
struct ircd::server::out
{
	const_buffer head;
	const_buffer content;

	/// The progress closure is an optional callback invoked every time more
	/// content is written to the socket. The first argument is a view of the
	/// data most recently written. The second argument is a view of all data
	/// written so far. This is only invoked for content. At the first
	/// invocation, the head has been fully written.
	std::function<void (const_buffer, const_buffer) noexcept> progress;
};

/// Request data and options related to the receive side of the request.
/// This is where buffers are supplied to receive data from the remote
/// server.
///
/// As a feature, when content == head, the head buffer is considered
/// as a contiguous buffer for both head and content; the content buffer
/// will be updated to point to any data after the head is received.
///
struct ircd::server::in
{
	mutable_buffer head;
	mutable_buffer content {head};

	/// The progress closure is an optional callback invoked every time more
	/// content is read from the socket. The first argument is a view of the
	/// data most recently received. The second argument is a view of all data
	/// received so far. This is only invoked for content, not for the head;
	/// however the first time it is invoked it is safe to view the in.head
	std::function<void (const_buffer, const_buffer) noexcept> progress;

	/// The dynamic buffer is a convenience that allows for the content buffer
	/// to be allocated on demand once the head is received and the length is
	/// known. To use dynamic, set the content buffer to nothing (i.e default
	/// constructed mutable_buffer).
	unique_buffer<mutable_buffer> dynamic;
};

/// This is a handle for being a client to another server. This handle will
/// attempt to find an existing connection pool for the remote server otherwise
/// one will be created. Then it will multiplex your request and demultiplex
/// your response with all the other requests pending in the pipelines to
/// the remote.
///
struct ircd::server::request
:ctx::future<http::code>
{
	struct opts;

	static const opts opts_default;

	server::tag *tag {nullptr};

  public:
	/// Transmission data
	server::out out;

	/// Reception data
	server::in in;

	/// Options
	const opts *opt { &opts_default };

	request(const net::hostport &,
	        server::out,
	        server::in,
	        const opts *const & = nullptr);

	request() = default;
	request(request &&) noexcept;
	request(const request &) = delete;
	request &operator=(request &&) noexcept;
	request &operator=(const request &) = delete;
	~request() noexcept;
};

struct ircd::server::request::opts
{
	/// When true, HTTP responses above the 200's are thrown as exceptions
	/// from the future::get() on this object. Otherwise, if false any code
	/// received is returned in the value and exceptions are thrown when no
	/// code can be returned.
	bool http_exceptions {true};

	/// Only applies when using the dynamic content allocation feature; this
	/// limits the size of that allocation in case the remote sends a larger
	/// content-length value. If the remote sends more content, the behavior
	/// is the same as if specifying an in.content buffer of this size.
	size_t content_length_maxalloc {256_MiB};
};

inline
ircd::server::request::request(const net::hostport &hostport,
                               server::out out,
                               server::in in,
                               const opts *const &opt)
:tag{nullptr}
,out{std::move(out)}
,in{std::move(in)}
,opt{opt?: &opts_default}
{
	submit(hostport, *this);
}

inline
ircd::server::request::request(request &&o)
noexcept
:ctx::future<http::code>{std::move(o)}
,tag{std::move(o.tag)}
,out{std::move(o.out)}
,in{std::move(o.in)}
,opt{std::move(o.opt)}
{
	if(tag)
		associate(*this, *tag, std::move(o));

	assert(!o.tag);
}

inline ircd::server::request &
ircd::server::request::operator=(request &&o)
noexcept
{
	this->~request();

	ctx::future<http::code>::operator=(std::move(o));
	out = std::move(o.out);
	in = std::move(o.in);
	tag = std::move(o.tag);
	opt = std::move(o.opt);

	if(tag)
		associate(*this, *tag, std::move(o));

	assert(!o.tag);
	return *this;
}

inline
ircd::server::request::~request()
noexcept
{
	if(tag)
		cancel(*this);

	if(tag)
		disassociate(*this, *tag);

	assert(!tag);
}

inline size_t
ircd::server::size(const in &in)
{
	return size(in.head) + size(in.content);
}

inline size_t
ircd::server::size(const out &out)
{
	return size(out.head) + size(out.content);
}
