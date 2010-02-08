/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
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

#ifndef CONF_H_
#define CONF_H_

#include <esc/common.h>

#define CONF_TIMER_FREQ		0
#define CONF_MAX_PROCS		1
#define CONF_MAX_THREADS	2
#define CONF_MAX_FDS		3

/**
 * Gets the value of a kernel-configuration
 *
 * @param id the id of the config-value (CONF_*)
 * @return s32 the value or the negative error-code
 */
s32 getConf(u32 id);

#endif /* CONF_H_ */
