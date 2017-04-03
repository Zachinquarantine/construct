/*
 * Copyright (C) 2016 Charybdis Development Team
 * Copyright (C) 2016 Jason Volk <jason@zemos.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once
#define HAVE_IRCD_DB_COLUMN_H

namespace ircd {
namespace db   {

// Columns add the ability to run multiple LevelDB's in synchrony under the same database
// (directory). Each column is a fully distinct key/value store; they are merely joined
// for consistency.
//
// [GET] may be posted to a separate thread which incurs the time of IO while the calling
// ircd::context yields.
//
// [SET] usually occur without yielding your context because the DB is write-log oriented.
//
struct column
{
	struct delta;
	struct const_iterator;
	using key_type = string_view;
	using mapped_type = string_view;
	using value_type = std::pair<key_type, mapped_type>;
	using pointer = value_type *;
	using reference = value_type &;
	using difference_type = size_t;
	using iterator = const_iterator;

  protected:
	std::shared_ptr<database::column> c;

  public:
	explicit operator const database &() const;
	explicit operator const database::column &() const;

	explicit operator database &();
	explicit operator database::column &();

	operator bool() const                        { return bool(c);                                 }
	bool operator!() const                       { return !c;                                      }

	// [GET] Iterations
	const_iterator cbegin(const gopts & = {});
	const_iterator cend(const gopts & = {});
	const_iterator begin(const gopts & = {});
	const_iterator end(const gopts & = {});
	const_iterator find(const string_view &key, const gopts & = {});
	const_iterator lower_bound(const string_view &key, const gopts & = {});
	const_iterator upper_bound(const string_view &key, const gopts & = {});

	// [GET] Get cell
	cell operator[](const string_view &key) const;

	// [GET] Perform a get into a closure. This offers a reference to the data with zero-copy.
	using view_closure = std::function<void (const string_view &)>;
	void operator()(const string_view &key, const view_closure &func, const gopts & = {});
	void operator()(const string_view &key, const gopts &, const view_closure &func);

	// [SET] Perform operations in a sequence as a single transaction.
	void operator()(const delta &, const sopts & = {});
	void operator()(const std::initializer_list<delta> &, const sopts & = {});
	void operator()(const sopts &, const std::initializer_list<delta> &);
	void operator()(const op &, const string_view &key, const string_view &val = {}, const sopts & = {});

	explicit column(std::shared_ptr<database::column> c);
	column(database::column &c);
	column(database &, const string_view &column);
	column() = default;
};

struct column::delta
:std::tuple<op, string_view, string_view>
{
	delta(const enum op &op, const string_view &key, const string_view &val = {})
	:std::tuple<enum op, string_view, string_view>{op, key, val}
	{}

	delta(const string_view &key, const string_view &val, const enum op &op = op::SET)
	:std::tuple<enum op, string_view, string_view>{op, key, val}
	{}
};

struct column::const_iterator
{
	using value_type = column::value_type;
	using iterator_category = std::bidirectional_iterator_tag;

  private:
	gopts opts;
	std::shared_ptr<database::column> c;
	std::unique_ptr<rocksdb::Iterator> it;
	mutable value_type val;

	friend class column;
	const_iterator(std::shared_ptr<database::column>, std::unique_ptr<rocksdb::Iterator> &&, gopts = {});

  public:
	operator const database::column &() const    { return *c;                                   }
	operator const database::snapshot &() const  { return opts.snapshot;                        }
	explicit operator const gopts &() const      { return opts;                                 }

	operator database::column &()                { return *c;                                   }
	explicit operator database::snapshot &()     { return opts.snapshot;                        }

	operator bool() const;
	bool operator!() const;

	const value_type *operator->() const;
	const value_type &operator*() const;

	const_iterator &operator++();
	const_iterator &operator--();

	const_iterator();
	const_iterator(const_iterator &&) noexcept;
	const_iterator &operator=(const_iterator &&) noexcept;
	~const_iterator() noexcept;

	friend bool operator==(const const_iterator &, const const_iterator &);
	friend bool operator!=(const const_iterator &, const const_iterator &);
	friend bool operator<(const const_iterator &, const const_iterator &);
	friend bool operator>(const const_iterator &, const const_iterator &);

	template<class pos> friend void seek(column::const_iterator &, const pos &);
	friend void seek(column::const_iterator &, const string_view &key);
};

// Get property data of a db column.
// R can optionally be uint64_t for some values.
template<class R = std::string> R property(column &, const string_view &name);
template<> std::string property(column &, const string_view &name);
template<> uint64_t property(column &, const string_view &name);

// Information about a column
const std::string &name(const column &);
size_t file_count(column &);
size_t bytes(column &);

// [GET] Tests if key exists
bool has(column &, const string_view &key, const gopts & = {});

// [GET] Convenience functions to copy data into your buffer.
// The signed char buffer is null terminated; the unsigned is not.
size_t read(column &, const string_view &key, uint8_t *const &buf, const size_t &max, const gopts & = {});
string_view read(column &, const string_view &key, char *const &buf, const size_t &max, const gopts & = {});
std::string read(column &, const string_view &key, const gopts & = {});

// [SET] Write data to the db
void write(column &, const string_view &key, const string_view &value, const sopts & = {});
void write(column &, const string_view &key, const uint8_t *const &buf, const size_t &size, const sopts & = {});

// [SET] Remove data from the db. not_found is never thrown.
void del(column &, const string_view &key, const sopts & = {});

// [SET] Flush memory tables to disk (this column only).
void flush(column &, const bool &blocking = false);

} // namespace db
} // namespace ircd

inline
ircd::db::column::operator database::column &()
{
	return *c;
}

inline
ircd::db::column::operator database &()
{
	return database::get(*c);
}

inline
ircd::db::column::operator const database::column &()
const
{
	return *c;
}

inline
ircd::db::column::operator const database &()
const
{
	return database::get(*c);
}
