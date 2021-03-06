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

#pragma once

#include <vfs/node.h>
#include <vfs/info.h>
#include <common.h>
#include <ostringstream.h>

/**
 * Represents a IRQ.
 */
class VFSIRQ : public VFSNode {
public:
	/**
	 * Creates an IRQ file.
	 *
	 * @param u the user
	 * @param parent the parent-node
	 * @param irq the IRQ number
	 * @param mode the mode to set
	 * @param success whether the constructor succeeded (is expected to be true before the call!)
	 */
	explicit VFSIRQ(const fs::User &u,VFSNode *parent,int irq,uint mode,bool &success)
		: VFSNode(u,buildName(irq),S_IFIRQ | (mode & MODE_PERM),success), _irq(irq) {
		if(!success)
			return;

		append(parent);
	}

	/**
	 * @return the IRQ number of this file
	 */
	int getIRQ() const {
		return _irq;
	}

	virtual bool isDeletable() const override {
		return false;
	}

	virtual ssize_t read(OpenFile *,void *buffer,off_t offset,size_t count) override {
		ssize_t res = VFSInfo::readHelper(this,buffer,offset,count,0,readCallback);
		acctime = Timer::getTime();
		return res;
	}

private:
	static char *buildName(int irq) {
		char name[12];
		itoa(name,sizeof(name),irq);
		return strdup(name);
	}

	static void readCallback(VFSNode *node,size_t *dataSize,void **buffer) {
		OStringStream os;
		int irq = atoi(node->getName());
		Interrupts::printIRQ(irq,os);
		*buffer = os.keepString();
		*dataSize = os.getLength();
	}

	int _irq;
};
