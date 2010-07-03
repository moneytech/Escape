// -*- C++ -*- Exception handling and frame unwind runtime interface routines.
// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
// Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

// This is derived from the C++ ABI for IA-64.  Where we diverge
// for cross-architecture compatibility are noted with "@@@".

#ifndef _UNWIND_CXX_H
#define _UNWIND_CXX_H 1

// Level 2: C++ ABI

#include <stddef.h>
#include "typeinfo"
#include "exception"
#include "unwind.h"

// --- from bits/atomic_word ---
typedef int _Atomic_word;

#pragma GCC visibility push(default)

namespace __cxxabiv1 {

	// A primary C++ exception object consists of a header, which is a wrapper
	// around an unwind object header with additional C++ specific information,
	// followed by the exception object itself.

	struct __cxa_exception {
		// Manage the exception object itself.
		std::type_info *exceptionType;
		void (*exceptionDestructor)(void *);

		// The C++ standard has entertaining rules wrt calling set_terminate
		// and set_unexpected in the middle of the exception cleanup process.
		std::unexpected_handler unexpectedHandler;
		std::terminate_handler terminateHandler;

		// The caught exception stack threads through here.
		__cxa_exception *nextException;

		// How many nested handlers have caught this exception.  A negated
		// value is a signal that this object has been rethrown.
		int handlerCount;

		// Cache parsed handler data from the personality routine Phase 1
		// for Phase 2 and __cxa_call_unexpected.
		int handlerSwitchValue;
		const unsigned char *actionRecord;
		const unsigned char *languageSpecificData;
		_Unwind_Ptr catchTemp;
		void *adjustedPtr;

		// The generic exception header.  Must be last.
		_Unwind_Exception unwindHeader;
	};

	struct __cxa_refcounted_exception {
		// Manage this header.
		_Atomic_word referenceCount;
		// __cxa_exception must be last, and no padding can be after it.
		__cxa_exception exc;
	};

	// A dependent C++ exception object consists of a wrapper around an unwind
	// object header with additional C++ specific information, containing a pointer
	// to a primary exception object.

	struct __cxa_dependent_exception {
		// The primary exception this thing depends on.
		void *primaryException;

		// The C++ standard has entertaining rules wrt calling set_terminate
		// and set_unexpected in the middle of the exception cleanup process.
		std::unexpected_handler unexpectedHandler;
		std::terminate_handler terminateHandler;

		// The caught exception stack threads through here.
		__cxa_exception *nextException;

		// How many nested handlers have caught this exception.  A negated
		// value is a signal that this object has been rethrown.
		int handlerCount;

		// Cache parsed handler data from the personality routine Phase 1
		// for Phase 2 and __cxa_call_unexpected.
		int handlerSwitchValue;
		const unsigned char *actionRecord;
		const unsigned char *languageSpecificData;
		_Unwind_Ptr catchTemp;
		void *adjustedPtr;

		// The generic exception header.  Must be last.
		_Unwind_Exception unwindHeader;
	};

	// Each thread in a C++ program has access to a __cxa_eh_globals object.
	struct __cxa_eh_globals {
		__cxa_exception *caughtExceptions;
		unsigned int uncaughtExceptions;
	};

	// The __cxa_eh_globals for the current thread can be obtained by using
	// either of the following functions.  The "fast" version assumes at least
	// one prior call of __cxa_get_globals has been made from the current
	// thread, so no initialization is necessary.
	extern "C" __cxa_eh_globals *__cxa_get_globals() throw ();
	extern "C" __cxa_eh_globals *__cxa_get_globals_fast() throw ();

	// Allocate memory for the primary exception plus the thrown object.
	extern "C" void *__cxa_allocate_exception(std::size_t thrown_size) throw ();

	// Free the space allocated for the primary exception.
	extern "C" void __cxa_free_exception(void *thrown_exception) throw ();

	// Allocate memory for a dependent exception.
	extern "C" __cxa_dependent_exception* __cxa_allocate_dependent_exception()  throw();

	// Free the space allocated for the dependent exception.
	extern "C" void __cxa_free_dependent_exception(__cxa_dependent_exception *ex) throw();

	// Throw the exception.
	extern "C" void __cxa_throw (void *thrown_exception,std::type_info *tinfo,
			void (*dest) (void *)) __attribute__((noreturn));

	// Used to implement exception handlers.
	extern "C" void *__cxa_get_exception_ptr (void *) throw();
	extern "C" void *__cxa_begin_catch (void *) throw();
	extern "C" void __cxa_end_catch ();
	extern "C" void __cxa_rethrow () __attribute__((noreturn));

	// These facilitate code generation for recurring situations.
	extern "C" void __cxa_bad_cast ();
	extern "C" void __cxa_bad_typeid ();

	// @@@ These are not directly specified by the IA-64 C++ ABI.

	// Handles re-checking the exception specification if unexpectedHandler
	// throws, and if bad_exception needs to be thrown.  Called from the
	// compiler.
	extern "C" void __cxa_call_unexpected (void *) __attribute__((noreturn));
	extern "C" void __cxa_call_terminate (void*) __attribute__((noreturn));

	// Invokes given handler, dying appropriately if the user handler was
	// so inconsiderate as to return.
	extern void __terminate(std::terminate_handler) __attribute__((noreturn));
	extern void __unexpected(std::unexpected_handler) __attribute__((noreturn));

	// The current installed user handlers.
	extern std::terminate_handler __terminate_handler;
	extern std::unexpected_handler __unexpected_handler;

	// These are explicitly GNU C++ specific.

	// Acquire the C++ exception header from the C++ object.
	static inline __cxa_exception *__get_exception_header_from_obj (void *ptr) {
		return reinterpret_cast<__cxa_exception *>(ptr) - 1;
	}

	// Acquire the C++ exception header from the generic exception header.
	static inline __cxa_exception *__get_exception_header_from_ue (_Unwind_Exception *exc) {
		return reinterpret_cast<__cxa_exception *>(exc + 1) - 1;
	}

	// Acquire the C++ refcounted exception header from the C++ object.
	static inline __cxa_refcounted_exception *__get_refcounted_exception_header_from_obj (
			void *ptr) {
		return reinterpret_cast<__cxa_refcounted_exception *>(ptr) - 1;
	}

	// Acquire the C++ refcounted exception header from the generic exception
	// header.
	static inline __cxa_refcounted_exception *__get_refcounted_exception_header_from_ue (
			_Unwind_Exception *exc) {
		return reinterpret_cast<__cxa_refcounted_exception *>(exc + 1) - 1;
	}

	static inline __cxa_dependent_exception *__get_dependent_exception_from_ue (
			_Unwind_Exception *exc) {
		return reinterpret_cast<__cxa_dependent_exception *>(exc + 1) - 1;
	}

	// This is the primary exception class we report -- "GNUCC++\0".
	const _Unwind_Exception_Class __gxx_primary_exception_class = ((((((((_Unwind_Exception_Class) 'G'
															<< 8 | (_Unwind_Exception_Class) 'N')
													<< 8 | (_Unwind_Exception_Class) 'U')
											<< 8 | (_Unwind_Exception_Class) 'C')
									<< 8 | (_Unwind_Exception_Class) 'C')
							<< 8 | (_Unwind_Exception_Class) '+')
					<< 8 | (_Unwind_Exception_Class) '+')
			<< 8 | (_Unwind_Exception_Class) '\0');

	// This is the dependent (from std::rethrow_exception) exception class we report
	// "GNUCC++\x01"
	const _Unwind_Exception_Class __gxx_dependent_exception_class = ((((((((_Unwind_Exception_Class) 'G'
															<< 8 | (_Unwind_Exception_Class) 'N')
													<< 8 | (_Unwind_Exception_Class) 'U')
											<< 8 | (_Unwind_Exception_Class) 'C')
									<< 8 | (_Unwind_Exception_Class) 'C')
							<< 8 | (_Unwind_Exception_Class) '+')
					<< 8 | (_Unwind_Exception_Class) '+')
			<< 8 | (_Unwind_Exception_Class) '\x01');

	static inline bool __is_gxx_exception_class(_Unwind_Exception_Class c) {
		return c == __gxx_primary_exception_class
				|| c == __gxx_dependent_exception_class;
	}

	// Only checks for primary or dependent, but not that it is a C++ exception at
	// all.
	static inline bool __is_dependent_exception(_Unwind_Exception_Class c) {
		return (c & 1);
	}

#define __GXX_INIT_PRIMARY_EXCEPTION_CLASS(c) c = __gxx_primary_exception_class
#define __GXX_INIT_DEPENDENT_EXCEPTION_CLASS(c) c = __gxx_dependent_exception_class

	// GNU C++ personality routine, Version 0.
	extern "C" _Unwind_Reason_Code __gxx_personality_v0(int, _Unwind_Action, _Unwind_Exception_Class,
			struct _Unwind_Exception *, struct _Unwind_Context *);

	// GNU C++ sjlj personality routine, Version 0.
	extern "C" _Unwind_Reason_Code __gxx_personality_sj0(int, _Unwind_Action, _Unwind_Exception_Class,
			struct _Unwind_Exception *, struct _Unwind_Context *);

	static inline void *__gxx_caught_object(_Unwind_Exception* eo) {
		// Bad as it looks, this actually works for dependent exceptions too.
		__cxa_exception* header = __get_exception_header_from_ue (eo);
		return header->adjustedPtr;
	}

	static inline void *__get_object_from_ue(_Unwind_Exception* eo) throw() {
		return __is_dependent_exception (eo->exception_class) ?
				__get_dependent_exception_from_ue (eo)->primaryException : eo + 1;
	}

	static inline void *__get_object_from_ambiguous_exception(__cxa_exception *p_or_d) throw() {
		return __get_object_from_ue (&p_or_d->unwindHeader);
	}

} /* namespace __cxxabiv1 */

#pragma GCC visibility pop

#endif // _UNWIND_CXX_H
