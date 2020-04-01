// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

using namespace ircd;

mapi::header
IRCD_MODULE
{
	"federation state_ids"
};

m::resource
state_ids_resource
{
	"/_matrix/federation/v1/state_ids/",
	{
		"federation state_ids",
		resource::DIRECTORY,
	}
};

m::resource::response
get__state_ids(client &client,
               const m::resource::request &request)
{
	if(request.parv.size() < 1)
		throw m::NEED_MORE_PARAMS
		{
			"room_id path parameter required"
		};

	m::room::id::buf room_id
	{
		url::decode(room_id, request.parv[0])
	};

	if(m::room::server_acl::enable_read && !m::room::server_acl::check(room_id, request.node_id))
		throw m::ACCESS_DENIED
		{
			"You are not permitted by the room's server access control list."
		};

	m::event::id::buf event_id;
	if(request.query["event_id"])
		event_id = url::decode(event_id, request.query.at("event_id"));

	const m::room room
	{
		room_id, event_id
	};

	if(!visible(room, request.node_id))
		throw m::ACCESS_DENIED
		{
			"You are not permitted to view the room at this event"
		};

	const m::room::state state
	{
		room
	};

	const m::room::auth::chain ac
	{
		event_id?
			m::index(event_id):
			m::head_idx(room)
	};

	m::resource::response::chunked response
	{
		client, http::OK
	};

	json::stack out
	{
		response.buf, response.flusher()
	};

	json::stack::object top{out};

	// auth_chain
	if(request.query.get<bool>("auth_chain_ids", true))
	{
		json::stack::array auth_chain_ids
		{
			top, "auth_chain_ids"
		};

		ac.for_each([&auth_chain_ids]
		(const m::event::idx &event_idx)
		{
			m::event_id(std::nothrow, event_idx, [&auth_chain_ids]
			(const auto &event_id)
			{
				auth_chain_ids.append(event_id);
			});

			return true;
		});
	}

	// pdu_ids
	if(request.query.get<bool>("pdu_ids", true))
	{
		json::stack::array pdu_ids
		{
			top, "pdu_ids"
		};

		state.for_each(m::event::id::closure{[&pdu_ids]
		(const m::event::id &event_id)
		{
			pdu_ids.append(event_id);
			return true;
		}});
	}

	return std::move(response);
}

m::resource::method
method_get
{
	state_ids_resource, "GET", get__state_ids,
	{
		method_get.VERIFY_ORIGIN
	}
};
