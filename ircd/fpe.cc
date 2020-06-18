// The Construct
//
// Copyright (C) The Construct Developers, Authors & Contributors
// Copyright (C) 2016-2020 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#ifndef __GNUC__
#pragma STDC FENV_ACCESS on
#endif

std::fexcept_t
ircd::fpe::set(const ushort &flags)
{
	std::fexcept_t theirs;
	syscall(std::fesetexceptflag, &theirs, flags);
	return theirs;
}

void
ircd::fpe::throw_errors(const ushort &flags)
{
	if(!flags)
		return;

	thread_local char buf[128];
	throw std::domain_error
	{
		reflect(buf, flags)
	};
}

ircd::string_view
ircd::fpe::reflect(const mutable_buffer &buf,
                   const ushort &flags)
{
	window_buffer wb{buf};
	const auto append{[&wb](const auto &flag)
	{
		wb([&flag](const mutable_buffer &buf)
		{
			return strlcpy(buf, reflect(flag));
		});
	}};

	for(size_t i(0); i < sizeof(flags) * 8; ++i)
		if(flags & (1 << i))
			append(1 << i);

	return wb.completed();
}

ircd::string_view
ircd::fpe::reflect(const ushort &flag)
{
	switch(flag)
	{
		case 0:             return "";
		case FE_INVALID:    return "INVALID";
		case FE_DIVBYZERO:  return "DIVBYZERO";
		case FE_UNDERFLOW:  return "UNDERFLOW";
		case FE_OVERFLOW:   return "OVERFLOW";
		case FE_INEXACT:    return "INEXACT";
	}

	return "?????";
}

ircd::string_view
ircd::fpe::reflect_sicode(const int &code)
{
	switch(code)
	{
		#if defined(HAVE_SIGNAL_H) && defined(FPE_INTDIV)
		case FPE_INTDIV:  return "INTDIV";
		case FPE_INTOVF:  return "INTOVF";
		case FPE_FLTDIV:  return "FLTDIV";
		case FPE_FLTOVF:  return "FLTOVF";
		case FPE_FLTUND:  return "FLTUND";
		case FPE_FLTRES:  return "FLTRES";
		case FPE_FLTINV:  return "FLTINV";
		case FPE_FLTSUB:  return "FLTSUB";
		#endif // HAVE_SIGNAL_H
	}

	return "?????";
}

//
// errors_handle
//

ircd::fpe::errors_handle::errors_handle()
{
	syscall(std::fegetexceptflag, &theirs, FE_ALL_EXCEPT);
	clear_pending();
}

ircd::fpe::errors_handle::~errors_handle()
noexcept(false)
{
	const auto pending(this->pending());
	syscall(std::fesetexceptflag, &theirs, FE_ALL_EXCEPT);
	throw_errors(pending);
}

void
ircd::fpe::errors_handle::clear_pending()
{
	syscall(std::feclearexcept, FE_ALL_EXCEPT);
}

void
ircd::fpe::errors_handle::throw_pending()
const
{
	throw_errors(pending());
}

ushort
ircd::fpe::errors_handle::pending()
const
{
	return std::fetestexcept(FE_ALL_EXCEPT);
}