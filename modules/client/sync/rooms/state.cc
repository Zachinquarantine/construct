// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m::sync
{
	static void room_state_append(data &, json::stack::array &, const m::event &, const m::event::idx &);

	static bool room_state_phased_member_events(data &, json::stack::array &);
	static bool room_state_phased_events(data &);
	static bool room_state_polylog_events(data &);
	static bool _room_state_polylog(data &);
	static bool room_state_polylog(data &);
	static bool room_invite_state_polylog(data &);

	static bool room_state_linear_events(data &);
	static bool room_invite_state_linear(data &);
	static bool room_state_linear(data &);

	extern conf::item<int64_t> state_exposure_depth; //TODO: XXX
	extern const event::keys::include _default_keys;
	extern event::fetch::opts _default_fopts;

	extern item room_invite_state;
	extern item room_state;
}

ircd::mapi::header
IRCD_MODULE
{
	"Client Sync :Room State", []
	{
		ircd::m::sync::_default_fopts.query_json_force = true;
	}
};

decltype(ircd::m::sync::room_state)
ircd::m::sync::room_state
{
	"rooms.state",
	room_state_polylog,
	room_state_linear,
	{
		{ "phased", true },
	}
};

decltype(ircd::m::sync::room_invite_state)
ircd::m::sync::room_invite_state
{
	"rooms.invite_state",
	room_invite_state_polylog,
	room_invite_state_linear,
};

decltype(ircd::m::sync::_default_keys)
ircd::m::sync::_default_keys
{
	"content",
	"depth",
	"event_id",
	"origin_server_ts",
	"redacts",
	"room_id",
	"sender",
	"state_key",
	"type",
};

decltype(ircd::m::sync::_default_fopts)
ircd::m::sync::_default_fopts
{
	_default_keys
};

bool
ircd::m::sync::room_state_linear(data &data)
{
	return room_state_linear_events(data);
}

bool
ircd::m::sync::room_invite_state_linear(data &data)
{
	if(data.membership != "invite")
		return false;

	return room_state_linear_events(data);
}

//TODO: This has to be merged into the timeline conf items
decltype(ircd::m::sync::state_exposure_depth)
ircd::m::sync::state_exposure_depth
{
	{ "name",         "ircd.client.sync.rooms.state.exposure.depth" },
	{ "default",      20L                                           },
};

bool
ircd::m::sync::room_state_linear_events(data &data)
{
	if(!data.event_idx)
		return false;

	if(!data.room)
		return false;

	if(!data.membership)
		return false;

	assert(data.event);
	if(!json::get<"state_key"_>(*data.event))
		return false;

	// Figure out whether the event was included in the timeline or whether
	// to include it here in the state, which comes before the timeline.
	// Since linear-sync is already distinct from polylog-sync, the
	// overwhelming majority of state events coming through linear-sync will
	// use the timeline. We make an exception for past state events the server
	// only recently obtained, to hide them from the timeline.
	if(int64_t(state_exposure_depth) > -1)
		if(json::get<"depth"_>(*data.event) + int64_t(state_exposure_depth) >= data.room_depth)
			return false;

	json::stack::object rooms
	{
		*data.out, "rooms"
	};

	json::stack::object membership_
	{
		*data.out, data.membership
	};

	json::stack::object room_
	{
		*data.out, data.room->room_id
	};

	const auto &state_member_name
	{
		data.membership == "invite"?
			"invite_state": // "invite_state"_sv:
			"state"
	};

	json::stack::object state
	{
		*data.out, state_member_name
	};

	json::stack::array array
	{
		*data.out, "events"
	};

	room_state_append(data, array, *data.event, data.event_idx);
	return true;
}

bool
ircd::m::sync::room_state_polylog(data &data)
{
	if(data.membership == "invite")
		return false;

	return _room_state_polylog(data);
}

bool
ircd::m::sync::room_invite_state_polylog(data &data)
{
	if(data.membership != "invite")
		return false;

	return _room_state_polylog(data);
}

bool
ircd::m::sync::_room_state_polylog(data &data)
{
	if(!apropos(data, data.room_head))
		return false;

	return room_state_polylog_events(data);
}

bool
ircd::m::sync::room_state_polylog_events(data &data)
{
	if(data.phased && data.range.first == 0)
		return room_state_phased_events(data);

	const m::room &room{*data.room};
	const m::room::state state{room};
	json::stack::array array
	{
		*data.out, "events"
	};

	ctx::mutex mutex;
	std::array<event::idx, 64> md;
	std::vector<event::fetch> event(md.size() * 3);
	for(auto &fetch : event)
		fetch = event::fetch{_default_fopts};

	size_t _i(0);
	bool ret{false};
	const event::closure_idx each_idx{[&data, &array, &mutex, &ret, &event, &_i]
	(const m::event::idx event_idx)
	{
		const size_t i{_i++ % event.size()};
		if(unlikely(!seek(event.at(i), event_idx, std::nothrow)))
		{
			assert(data.room);
			log::error
			{
				log, "Failed to fetch event idx:%lu in room %s state.",
				event_idx,
				string_view{data.room->room_id}
			};

			return;
		}

		const std::lock_guard lock{mutex};
		room_state_append(data, array, event.at(i), event_idx);
		ret = true;
	}};

	ctx::concurrent<event::idx> concurrent
	{
		m::sync::pool, md, each_idx
	};

	state.for_each([&data, &concurrent, &each_idx]
	(const m::event::idx &event_idx)
	{
		if(!apropos(data, event_idx))
			return;

		concurrent(event_idx);
	});

	concurrent.wait_done();
	return ret;
}

bool
ircd::m::sync::room_state_phased_events(data &data)
{
	json::stack::array array
	{
		*data.out, "events"
	};

	bool ret{false};
	data.room->get(std::nothrow, "m.room.create", "", [&]
	(const m::event &event)
	{
		ret = true;
		room_state_append(data, array, event, index(event));
	});

	data.room->get(std::nothrow, "m.room.canonical_alias", "", [&]
	(const m::event &event)
	{
		room_state_append(data, array, event, index(event));
	});

	data.room->get(std::nothrow, "m.room.aliases", data.user.user_id.host(), [&]
	(const m::event &event)
	{
		room_state_append(data, array, event, index(event));
	});

	data.room->get(std::nothrow, "m.room.name", "", [&]
	(const m::event &event)
	{
		room_state_append(data, array, event, index(event));
	});

	data.room->get(std::nothrow, "m.room.avatar", "", [&]
	(const m::event &event)
	{
		room_state_append(data, array, event, index(event));
	});

	data.room->get(std::nothrow, "m.room.member", data.user.user_id, [&]
	(const m::event &event)
	{
		room_state_append(data, array, event, index(event));
	});

	ret |= room_state_phased_member_events(data, array);
	return ret;
}

bool
ircd::m::sync::room_state_phased_member_events(data &data,
                                               json::stack::array &array)
{
	static const auto count{10}, bufsz{48}, limit{10};
	std::array<char[bufsz], count> buf;
	std::array<string_view, count> last;
	size_t i(0), ret(0);
	m::room::messages it
	{
		*data.room
	};

	const auto already{[&last, &ret]
	(const string_view &sender) -> bool
	{
		return std::any_of(begin(last), begin(last)+ret, [&sender]
		(const auto &last)
		{
			return startswith(last, sender);
		});
	}};

	m::event::fetch event;
	for(; it && ret < count && i < limit; --it, ++i)
	{
		const auto &event_idx(it.event_idx());
		m::get(std::nothrow, event_idx, "sender", [&]
		(const auto &sender)
		{
			if(already(sender))
				return;

			last.at(ret) = strlcpy(buf.at(ret), sender);
			++ret;

			if(!seek(event, event_idx, std::nothrow))
				return;

			room_state_append(data, array, event, event_idx);
		});
	};

	return ret;
}

void
ircd::m::sync::room_state_append(data &data,
                                 json::stack::array &events,
                                 const m::event &event,
                                 const m::event::idx &event_idx)
{
	m::event_append_opts opts;
	opts.event_idx = &event_idx;
	opts.user_id = &data.user.user_id;
	opts.user_room = &data.user_room;
	opts.query_txnid = false;
	opts.room_depth = &data.room_depth;
	m::append(events, event, opts);
}
