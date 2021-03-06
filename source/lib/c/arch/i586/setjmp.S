/**
 * $Id$
 * Copyright (C) 2008 - 2014 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <sys/asm.h>

.section .text

// int setjmp(jmp_buf env);
BEGIN_FUNC(setjmp)
	push	%ebp
	mov		%esp,%ebp

	// save regs (callee-save)
	mov		%ebx, 0(%eax)
	mov		%esp, 4(%eax)
	mov		%edi, 8(%eax)
	mov		%esi,12(%eax)
	mov		(%ebp),%ecx							// store ebp of the caller stack-frame
	mov		%ecx,16(%eax)
	pushfl										// load eflags
	popl	20(%eax)							// store
	mov		4(%ebp),%ecx						// store return-address
	mov		%ecx,24(%eax)

	mov		$0,%eax								// return 0
	leave
	ret
END_FUNC(setjmp)

// int longjmp(jmp_buf env,int val);
BEGIN_FUNC(longjmp)
	push	%ebp
	mov		%esp,%ebp

	// restore registers (callee-save)
	mov		8(%eax),%edi
	mov		12(%eax),%esi
	mov		16(%eax),%ebp
	mov		4(%eax),%esp
	add		$4,%esp								// undo 'push ebp'
	mov		0(%eax),%ebx
	pushl	20(%eax)							// get eflags
	popfl										// restore
	mov		24(%eax),%eax						// get return-address
	mov		%eax,(%esp)							// set return-address

	mov		%edx,%eax							// return val
	ret											// no leave here because we've already restored the
												// ebp of the caller stack-frame
END_FUNC(setjmp)
