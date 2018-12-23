/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Log.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Log.h"

#include <string>
#include <iostream>
#include <thread>
#include "Guards.h"
using namespace std;
using namespace dev;

// Logging
int dev::g_logVerbosity = 5;
map<type_info const*, bool> dev::g_logOverride;

/// Associate a name with each thread for nice logging.
struct ThreadLocalLogName
{
	ThreadLocalLogName(std::string const& _name) { m_name.reset(new string(_name)); }
	boost::thread_specific_ptr<std::string> m_name;
};

/// Associate a name with each thread for nice logging.
struct ThreadLocalLogContext
{
	ThreadLocalLogContext() = default;

	void push(std::string const& _name)
	{
		if (!m_contexts.get())
			m_contexts.reset(new vector<string>);
		m_contexts->push_back(_name);
	}

	void pop()
	{
		m_contexts->pop_back();
	}

	string join(string const& _prior)
	{
		string ret;
		if (m_contexts.get())
			for (auto const& i: *m_contexts)
				ret += _prior + i;
		return ret;
	}

	boost::thread_specific_ptr<std::vector<std::string>> m_contexts;
};

ThreadLocalLogContext g_logThreadContext;

ThreadLocalLogName g_logThreadName("main");

void dev::ThreadContext::push(string const& _n)
{
	g_logThreadContext.push(_n);
}

void dev::ThreadContext::pop()
{
	g_logThreadContext.pop();
}

string dev::ThreadContext::join(string const& _prior)
{
	return g_logThreadContext.join(_prior);
}

// foward declare without all of Windows.h
#ifdef _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* lpOutputString);
#endif

string dev::getThreadName()
{
#ifdef __linux__
	char buffer[128];
	pthread_getname_np(pthread_self(), buffer, 127);
	buffer[127] = 0;
	return buffer;
#else
	return g_logThreadName.m_name.get() ? *g_logThreadName.m_name.get() : "<unknown>";
#endif
}

void dev::setThreadName(string const& _n)
{
#ifdef __linux__
	pthread_setname_np(pthread_self(), _n.c_str());
#else
	g_logThreadName.m_name.reset(new std::string(_n));
#endif
}

void dev::simpleDebugOut(std::string const& _s, char const*)
{
	static Mutex s_lock;
	Guard l(s_lock);

	cerr << _s << endl << flush;

	// helpful to use OutputDebugString on windows
	#ifdef _WIN32
	{
		OutputDebugStringA(_s.data());
		OutputDebugStringA("\n");
	}
	#endif
}

std::function<void(std::string const&, char const*)> dev::g_logPost = simpleDebugOut;