/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
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

#ifndef ECO32MACHINE_H_
#define ECO32MACHINE_H_

#include <esc/common.h>
#include "../../machine.h"

class ECO32Machine : public Machine {
public:
	ECO32Machine()
		: Machine() {
	};
	virtual ~ECO32Machine() {
	};

	virtual void reboot(Progress &pg);
	virtual void shutdown(Progress &pg);
};

#endif /* ECO32MACHINE_H_ */