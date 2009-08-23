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

#include <common.h>
#include <vfs.h>
#include <vfsnode.h>
#include <vfsreal.h>
#include <vfsinfo.h>
#include <vfsreq.h>
#include <vfsdrv.h>
#include <vfsrw.h>
#include <proc.h>
#include <paging.h>
#include <util.h>
#include <kheap.h>
#include <sched.h>
#include <video.h>
#include <kevent.h>
#include <string.h>
#include <sllist.h>
#include <assert.h>
#include <errors.h>
#include <messages.h>

/* max number of open files */
#define FILE_COUNT					(PROC_COUNT * 16)
/* the processes node */
#define PROCESSES()					(nodes + 8)
/* the services node */
#define SERVICES()					(nodes + 11)
/* the drivers-node */
#define DRIVERS()					(nodes + 20)

/* an entry in the global file table */
typedef struct {
	/* read OR write; flags = 0 => entry unused */
	u8 flags;
	/* the owner of this file; sharing of a file is not possible for different threads
	 * (an exception is the inheritance to a child-thread) */
	tTid owner;
	/* number of references */
	u16 refCount;
	/* current position in file */
	u32 position;
	/* node-number; if MSB = 1 => virtual, otherwise real inode-number */
	tVFSNodeNo nodeNo;
} sGFTEntry;

/**
 * Searches for a free file for the given flags and node-number
 *
 * @param tid the thread to use
 * @param flags the flags (read, write)
 * @param nodeNo the node-number to open
 * @return the file-number on success or the negative error-code
 */
static tFileNo vfs_getFreeFile(tTid tid,u8 flags,tVFSNodeNo nodeNo);

/* global file table */
static sGFTEntry globalFileTable[FILE_COUNT];

void vfs_init(void) {
	sVFSNode *root,*sys;
	vfsn_init();

	/*
	 *  /
	 *   file:
	 *   system:
	 *     |-pipe
	 *     |-processes
	 *     |-devices
	 *     |-bin
	 *   services:
	 *   devices:
	 */
	root = vfsn_createDir(NULL,(char*)"");
	vfsn_createServiceNode(KERNEL_TID,root,(char*)"file",0);
	sys = vfsn_createDir(root,(char*)"system");
	vfsn_createPipeCon(sys,(char*)"pipe");
	vfsn_createDir(sys,(char*)"processes");
	vfsn_createDir(root,(char*)"services");
	vfsn_createDir(sys,(char*)"devices");
	vfsn_createDir(sys,(char*)"bin");
	vfsn_createDir(root,(char*)"drivers");
}

s32 vfs_hasAccess(tTid tid,tVFSNodeNo nodeNo,u8 flags) {
	sVFSNode *n = nodes + VIRT_INDEX(nodeNo);
	/* kernel is allmighty :P */
	if(tid == KERNEL_TID)
		return 0;

	if(n->owner == tid) {
		if((flags & VFS_READ) && !(n->mode & MODE_OWNER_READ))
			return ERR_NO_READ_PERM;
		if((flags & VFS_WRITE) && !(n->mode & MODE_OWNER_WRITE))
			return ERR_NO_WRITE_PERM;
	}
	else {
		if((flags & VFS_READ) && !(n->mode & MODE_OTHER_READ))
			return ERR_NO_READ_PERM;
		if((flags & VFS_WRITE) && !(n->mode & MODE_OTHER_WRITE))
			return ERR_NO_WRITE_PERM;
	}
	return 0;
}

tFileNo vfs_inheritFileNo(tTid tid,tFileNo file) {
	sGFTEntry *e = globalFileTable + file;
	sVFSNode *n = vfsn_getNode(e->nodeNo);
	/* we can't share service-usages since each thread has his own node */
	if((n->mode & MODE_TYPE_SERVUSE)) {
		sVFSNode *child;
		tVFSNodeNo nodeNo;
		tFileNo newFile;
		s32 err = vfsn_createServiceUse(tid,n->parent,&child);
		if(err < 0)
			return -1;

		nodeNo = NADDR_TO_VNNO(child);
		newFile = vfs_openFile(tid,e->flags,nodeNo);
		if(newFile < 0) {
			vfsn_removeNode(child);
			return -1;
		}
		return newFile;
	}
	/* if a pipe is inherited we need a new file for it (position should be different )*/
	else if(n->mode & MODE_TYPE_PIPE) {
		tFileNo newFile;
		/* we'll get a new file since the tid is different */
		newFile = vfs_openFile(tid,e->flags,e->nodeNo);
		if(newFile < 0)
			return -1;
		return newFile;
	}
	else {
		/* just increase references */
		e->refCount++;
		return file;
	}
}

s32 vfs_incRefs(tFileNo file) {
	sGFTEntry *e;
	/* invalid file-number? */
	if(file < 0 || file >= FILE_COUNT)
		return ERR_INVALID_FILE;

	e = globalFileTable + file;
	if(e->flags == 0)
		return ERR_INVALID_FILE;

	e->refCount++;
	return 0;
}

tVFSNodeNo vfs_getNodeNo(tFileNo file) {
	sGFTEntry *e;

	/* invalid file-number? */
	if(file < 0 || file >= FILE_COUNT)
		return ERR_INVALID_FILE;

	/* not in use? */
	e = globalFileTable + file;
	if(e->flags == 0)
		return ERR_INVALID_FILE;

	return e->nodeNo;
}

tFileNo vfs_openFile(tTid tid,u8 flags,tVFSNodeNo nodeNo) {
	sGFTEntry *e;
	sVFSNode *n = NULL;

	/* determine free file */
	tFileNo f = vfs_getFreeFile(tid,flags,nodeNo);
	if(f < 0)
		return f;

	if(IS_VIRT(nodeNo)) {
		s32 err;
		n = nodes + VIRT_INDEX(nodeNo);
		if((err = vfs_hasAccess(tid,nodeNo,flags)) < 0)
			return err;
	}

	/* unused file? */
	e = globalFileTable + f;
	if(e->flags == 0) {
		/* count references of virtual nodes */
		if(IS_VIRT(nodeNo))
			n->refCount++;
		e->owner = tid;
		e->flags = flags;
		e->refCount = 1;
		e->position = 0;
		e->nodeNo = nodeNo;
	}
	else
		e->refCount++;

	return f;
}

static tFileNo vfs_getFreeFile(tTid tid,u8 flags,tVFSNodeNo nodeNo) {
	tFileNo i;
	tFileNo freeSlot = ERR_NO_FREE_FD;
	bool isServUse = false;
	sGFTEntry *e = &globalFileTable[0];

	vassert(flags & (VFS_READ | VFS_WRITE),"flags empty");
	vassert(!(flags & ~(VFS_READ | VFS_WRITE)),"flags contains invalid bits");
	/* ensure that we don't increment usages of an unused slot */
	vassert(flags != 0,"No flags given");

	if(IS_VIRT(nodeNo)) {
		vassert(VIRT_INDEX(nodeNo) < NODE_COUNT,"nodeNo invalid");
		sVFSNode *n = vfsn_getNode(nodeNo);
		/* we can add pipes here, too, since every open() to a pipe will get a new node anyway */
		isServUse = (n->mode & (MODE_TYPE_SERVUSE | MODE_TYPE_PIPE)) ? true : false;
	}

	for(i = 0; i < FILE_COUNT; i++) {
		/* used slot and same node? */
		if(e->flags != 0) {
			/* we don't want to share files with different threads */
			/* this is allowed only if we create a child-threads. he will inherit the files.
			 * in this case we trust the threads that they know what they do :) */
			if(e->nodeNo == nodeNo && e->owner == tid) {
				/* service-usages may use a file twice for reading and writing because we
				 * will prevent trouble anyway */
				if(isServUse && e->flags == flags)
					return i;

				/* someone does already write to this file? so it's not really good
				 * to use it atm, right? */
				if(!isServUse && e->flags & VFS_WRITE)
					return ERR_FILE_IN_USE;

				/* if the flags are different we need a different slot */
				if(e->flags == flags)
					return i;
			}
		}
		/* remember free slot */
		else if(freeSlot == ERR_NO_FREE_FD) {
			freeSlot = i;
			/* just for performance: if we've found an unused file and want to use a service,
			 * use this slot because it doesn't really matter wether we use a new file or an
			 * existing one (if there even is any) */
			/* note: we can share a file for writing in this case! */
			if(isServUse)
				break;
		}

		e++;
	}

	return freeSlot;
}

tFileNo vfs_openFileForKernel(tTid tid,tVFSNodeNo nodeNo) {
	sVFSNode *node = vfsn_getNode(nodeNo);
	sVFSNode *n = NODE_FIRST_CHILD(node);

	/* not not already present? */
	if(n == NULL || n->owner != KERNEL_TID) {
		n = vfsn_createNode((char*)SERVICE_CLIENT_KERNEL);
		if(n == NULL)
			return ERR_NOT_ENOUGH_MEM;

		/* init node */
		/* the service has read/write permission */
		n->mode = MODE_TYPE_SERVUSE | MODE_OTHER_READ | MODE_OTHER_WRITE;
		n->readHandler = NULL;
		n->writeHandler = NULL;
		n->owner = KERNEL_TID;

		/* insert as first child */
		n->prev = NULL;
		n->next = node->firstChild;
		node->firstChild = n;
		if(node->lastChild == NULL)
			node->lastChild = n;
		n->parent = node;
	}

	/* open the file and return it */
	return vfs_openFile(tid,VFS_READ | VFS_WRITE,NADDR_TO_VNNO(n));
}

bool vfs_eof(tTid tid,tFileNo file) {
	sGFTEntry *e = globalFileTable + file;
	bool eof = true;

	if(IS_VIRT(e->nodeNo)) {
		tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
		sVFSNode *n = nodes + i;

		if(n->mode & MODE_TYPE_SERVUSE) {
			if(n->parent->mode & MODE_SERVICE_DRIVER)
				eof = n->parent->data.service.isEmpty;
			else if(n->parent->owner == tid)
				eof = sll_length(n->data.servuse.sendList) == 0;
			else
				eof = sll_length(n->data.servuse.recvList) == 0;
		}
		else
			eof = e->position >= n->data.def.pos;
	}
	else {
		/* TODO */
	}

	return eof;
}

s32 vfs_seek(tTid tid,tFileNo file,u32 position) {
	sGFTEntry *e = globalFileTable + file;
	UNUSED(tid);

	if(IS_VIRT(e->nodeNo)) {
		tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
		sVFSNode *n = nodes + i;

		if(n->mode & MODE_TYPE_SERVUSE) {
			if(n->parent->mode & MODE_SERVICE_DRIVER)
				e->position = position;
			else
				return ERR_SERVUSE_SEEK;
		}
		else {
			/* set position */
			if(n->data.def.pos == 0)
				e->position = 0;
			else
				e->position = MIN((u32)(n->data.def.pos) - 1,position);
		}
	}
	else {
		/* since the fs-service validates the position anyway we can simply set it */
		e->position = position;
	}

	return 0;
}

s32 vfs_readFile(tTid tid,tFileNo file,u8 *buffer,u32 count) {
	s32 err,readBytes;
	sGFTEntry *e = globalFileTable + file;

	if(IS_VIRT(e->nodeNo)) {
		tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
		sVFSNode *n = nodes + i;

		if(!(e->flags & VFS_READ))
			return ERR_NO_READ_PERM;
		if((err = vfs_hasAccess(tid,e->nodeNo,VFS_READ)) < 0)
			return err;

		/* node not present anymore or no read-handler? */
		if(n->name == NULL || n->readHandler == NULL)
			return ERR_INVALID_FILE;

		/* use the read-handler */
		readBytes = n->readHandler(tid,n,buffer,e->position,count);
		e->position += readBytes;
	}
	else {
		/* query the fs-service to read from the inode */
		readBytes = vfsr_readFile(tid,e->nodeNo,buffer,e->position,count);
		if(readBytes > 0)
			e->position += readBytes;
	}

	return readBytes;
}

s32 vfs_writeFile(tTid tid,tFileNo file,const u8 *buffer,u32 count) {
	s32 err,writtenBytes;
	sVFSNode *n;
	sGFTEntry *e = globalFileTable + file;

	if(IS_VIRT(e->nodeNo)) {
		tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
		n = nodes + i;

		if(!(e->flags & VFS_WRITE))
			return ERR_NO_WRITE_PERM;
		if((err = vfs_hasAccess(tid,e->nodeNo,VFS_WRITE)) < 0)
			return err;

		/* node not present anymore or no write-handler? */
		if(n->name == NULL || n->writeHandler == NULL)
			return ERR_INVALID_FILE;

		/* write to the node */
		writtenBytes = n->writeHandler(tid,n,buffer,e->position,count);
		if(writtenBytes < 0)
			return writtenBytes;

		e->position += writtenBytes;
	}
	else {
		/* query the fs-service to write to the inode */
		writtenBytes = vfsr_writeFile(tid,e->nodeNo,buffer,e->position,count);
		if(writtenBytes > 0)
			e->position += writtenBytes;
	}

	return writtenBytes;
}

s32 vfs_ioctl(tTid tid,tFileNo file,u32 cmd,u8 *data,u32 size) {
	s32 err;
	sGFTEntry *e = globalFileTable + file;

	if(!IS_VIRT(e->nodeNo))
		return ERR_INVALID_FILE;

	tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
	sVFSNode *n = nodes + i;

	/* TODO keep this? */
	if((err = vfs_hasAccess(tid,e->nodeNo,VFS_WRITE)) < 0)
		return err;

	/* node not present anymore? */
	if(n->name == NULL || !(n->mode & MODE_TYPE_SERVUSE) || !(n->parent->mode & MODE_SERVICE_DRIVER))
		return ERR_INVALID_FILE;

	return vfsdrv_ioctl(tid,n,cmd,data,size);
}

s32 vfs_sendMsg(tTid tid,tFileNo file,tMsgId id,const u8 *data,u32 size) {
	s32 err;
	sGFTEntry *e = globalFileTable + file;

	if(!IS_VIRT(e->nodeNo))
		return ERR_INVALID_FILE;

	tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
	sVFSNode *n = nodes + i;

	if((err = vfs_hasAccess(tid,e->nodeNo,VFS_WRITE)) < 0)
		return err;

	/* node not present anymore? */
	if(n->name == NULL)
		return ERR_INVALID_FILE;

	/* send the message */
	return vfs_serviceUseWriteHandler(tid,n,id,data,size);
}

s32 vfs_receiveMsg(tTid tid,tFileNo file,tMsgId *id,u8 *data,u32 size) {
	s32 err;
	sGFTEntry *e = globalFileTable + file;

	if(!IS_VIRT(e->nodeNo))
		return ERR_INVALID_FILE;

	tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
	sVFSNode *n = nodes + i;

	if((err = vfs_hasAccess(tid,e->nodeNo,VFS_READ)) < 0)
		return err;

	/* node not present anymore? */
	if(n->name == NULL)
		return ERR_INVALID_FILE;

	/* send the message */
	return vfs_serviceUseReadHandler(tid,n,id,data,size);
}

void vfs_closeFile(tTid tid,tFileNo file) {
	sGFTEntry *e = globalFileTable + file;

	/* decrement references */
	if(--(e->refCount) == 0) {
		tVFSNodeNo i = VIRT_INDEX(e->nodeNo);
		sVFSNode *n = nodes + i;

		if(IS_VIRT(e->nodeNo)) {
			if(n->name != NULL) {
				/* last usage? */
				if(--(n->refCount) == 0) {
					/* notify the driver, if it is one */
					if((n->mode & MODE_TYPE_SERVUSE) && (n->parent->mode & MODE_SERVICE_DRIVER))
						vfsdrv_close(tid,n);

					/* if there are message for the service we don't want to throw them away */
					/* if there are any in the receivelist (and no references of the node) we
					 * can throw them away because no one will read them anymore
					 * (it means that the client has already closed the file) */
					/* note also that we can assume that the service is still running since we
					 * would have deleted the whole service-node otherwise */
					if((n->mode & MODE_TYPE_SERVUSE) || (n->mode & MODE_TYPE_PIPE)) {
						if(!(n->mode & MODE_TYPE_SERVUSE) || sll_length(n->data.servuse.sendList) == 0)
							vfsn_removeNode(n);
					}
				}
			}
		}
		else
			vfsr_closeFile(e->nodeNo);

		/* mark unused */
		e->flags = 0;
	}
}

tServ vfs_createService(tTid tid,const char *name,u32 type) {
	sVFSNode *serv;
	sVFSNode *n;
	u32 len;
	s32 res;
	char *hname;

	/* determine position in VFS */
	if(type & MODE_SERVICE_DRIVER)
		serv = DRIVERS();
	else
		serv = SERVICES();
	n = serv->firstChild;

	vassert(name != NULL,"name == NULL");

	/* we don't want to have exotic service-names */
	if((len = strlen(name)) == 0 || !isalnumstr(name))
		return ERR_INV_SERVICE_NAME;

	while(n != NULL) {
		/* entry already existing? */
		if(strcmp(n->name,name) == 0)
			return ERR_SERVICE_EXISTS;
		n = n->next;
	}

	/* copy name to kernel-heap */
	hname = (char*)kheap_alloc(len + 1);
	if(hname == NULL)
		return ERR_NOT_ENOUGH_MEM;
	strncpy(hname,name,len);
	hname[len] = '\0';

	/* create node */
	n = vfsn_createServiceNode(tid,serv,hname,type);
	if(n != NULL) {
		tVFSNodeNo nodeNo = NADDR_TO_VNNO(n);
		if(type & MODE_SERVICE_DRIVER) {
			res = vfsdrv_register(n);
			if(res < 0) {
				vfsn_removeNode(n);
				kheap_free(hname);
				return res;
			}
		}
		/* TODO that's not really nice ;) */
		else if(strcmp(name,"fs") == 0)
			vfsr_init(nodeNo);
		return nodeNo;
	}

	/* failed, so cleanup */
	kheap_free(hname);
	return ERR_NOT_ENOUGH_MEM;
}

s32 vfs_setDataReadable(tTid tid,tVFSNodeNo nodeNo,bool readable) {
	sVFSNode *n = nodes + nodeNo;
	if(n->name == NULL || !(n->mode & MODE_SERVICE_DRIVER))
		return ERR_INVALID_NODENO;
	if(n->owner != tid)
		return ERR_NOT_OWN_SERVICE;

	n->data.service.isEmpty = !readable;
	if(readable)
		thread_wakeupAll(EV_DATA_READABLE | EV_RECEIVED_MSG);
	return 0;
}

bool vfs_msgAvailableFor(tTid tid,u8 events) {
	sVFSNode *n;
	sThread *t = thread_getById(tid);
	bool isService = false;
	bool isClient = false;
	tFD i;

	/* at first we check wether the process is a service */
	if(events & EV_CLIENT) {
		sVFSNode *rn[2] = {SERVICES(),DRIVERS()};
		for(i = 0; i < 2; i++) {
			n = NODE_FIRST_CHILD(rn[i]);
			while(n != NULL) {
				if(n->owner == tid) {
					tVFSNodeNo nodeNo = NADDR_TO_VNNO(n);
					tVFSNodeNo client = vfs_getClient(tid,&nodeNo,1);
					isService = true;
					if(vfsn_isValidNodeNo(client))
						return true;
				}
				n = n->next;
			}
		}
	}

	/* now search through the file-descriptors if there is any message */
	if(events & EV_RECEIVED_MSG) {
		for(i = 0; i < MAX_FD_COUNT; i++) {
			if(t->fileDescs[i] != -1) {
				sGFTEntry *e = globalFileTable + t->fileDescs[i];
				if(IS_VIRT(e->nodeNo)) {
					n = vfsn_getNode(e->nodeNo);
					/* service-usage and a message in the receive-list? */
					/* we don't want to check that if it is our own service. because it makes no
					 * sense to read from ourself ;) */
					if((n->mode & MODE_TYPE_SERVUSE) && n->parent->owner != tid) {
						isClient = true;
						if(n->data.servuse.recvList != NULL && sll_length(n->data.servuse.recvList) > 0)
							return true;
					}
				}
			}
		}
	}

	/* if we are no client and no service we'll never receive a message */
	/*if(isClient || isService)*/
		return false;
	/*return true;*/
}

s32 vfs_getClient(tTid tid,tVFSNodeNo *vfsNodes,u32 count) {
	sVFSNode *n,*node;
	u32 i;
	for(i = 0; i < count; i++) {
		if(!vfsn_isValidNodeNo(vfsNodes[i]))
			return ERR_INVALID_NODENO;

		node = nodes + vfsNodes[i];
		if(node->owner != tid || !(node->mode & MODE_TYPE_SERVICE))
			return ERR_NOT_OWN_SERVICE;

		/* search for a slot that needs work */
		n = NODE_FIRST_CHILD(node);
		while(n != NULL) {
			/* data available? */
			if(n->data.servuse.sendList != NULL && sll_length(n->data.servuse.sendList) > 0)
				break;
			n = n->next;
		}

		if(n != NULL)
			return NADDR_TO_VNNO(n);
	}
	return ERR_NO_CLIENT_WAITING;;
}

tFileNo vfs_openClientThread(tTid tid,tVFSNodeNo nodeNo,tTid clientId) {
	sVFSNode *n,*node;
	/* check if the node is valid */
	if(!vfsn_isValidNodeNo(nodeNo))
		return ERR_INVALID_NODENO;
	node = nodes + nodeNo;
	if(node->owner != tid || !(node->mode & MODE_TYPE_SERVICE))
		return ERR_NOT_OWN_SERVICE;

	/* search for a slot that needs work */
	n = NODE_FIRST_CHILD(node);
	while(n != NULL) {
		if(n->owner == clientId)
			break;
		n = n->next;
	}

	/* not found? */
	if(n == NULL)
		return ERR_VFS_NODE_NOT_FOUND;

	/* open file */
	return vfs_openFile(tid,VFS_READ | VFS_WRITE,NADDR_TO_VNNO(n));
}

tFileNo vfs_openClient(tTid tid,tVFSNodeNo *vfsNodes,u32 count,tVFSNodeNo *servNode) {
	sVFSNode *n;
	tVFSNodeNo client = vfs_getClient(tid,vfsNodes,count);
	/* error? */
	if(!vfsn_isValidNodeNo(client))
		return client;

	/* open a file for it so that the service can read and write with it */
	n = vfsn_getNode(client);
	*servNode = NADDR_TO_VNNO(n->parent);
	return vfs_openFile(tid,VFS_READ | VFS_WRITE,client);
}

s32 vfs_removeService(tTid tid,tVFSNodeNo nodeNo) {
	sVFSNode *n = nodes + nodeNo;

	vassert(vfsn_isValidNodeNo(nodeNo),"Invalid node number %d",nodeNo);

	if(n->owner != tid || !(n->mode & MODE_TYPE_SERVICE))
		return ERR_NOT_OWN_SERVICE;

	/* unregister driver */
	if(n->mode & MODE_SERVICE_DRIVER)
		vfsdrv_unregister(n);

	/* remove service-node including all service-usages */
	vfsn_removeNode(n);
	return 0;
}

sVFSNode *vfs_createProcess(tPid pid,fRead handler) {
	char *name;
	sVFSNode *proc = PROCESSES();
	sVFSNode *n = proc->firstChild;
	sVFSNode *dir,*tdir;

	/* build name */
	name = (char*)kheap_alloc(sizeof(char) * 12);
	if(name == NULL)
		return NULL;

	itoa(name,pid);

	/* go to last entry */
	while(n != NULL) {
		/* entry already existing? */
		if(strcmp(n->name,name) == 0)
			goto errorName;
		n = n->next;
	}

	/* create dir */
	dir = vfsn_createDir(proc,name);
	if(dir == NULL)
		goto errorName;

	/* invalidate cache */
	if(proc->data.def.cache != NULL) {
		kheap_free(proc->data.def.cache);
		proc->data.def.cache = NULL;
	}

	/* create process-info-node */
	n = vfsn_createInfo(KERNEL_TID,dir,(char*)"info",handler);
	if(n == NULL)
		goto errorDir;

	/* create virt-mem-info-node */
	n = vfsn_createInfo(KERNEL_TID,dir,(char*)"virtmem",vfsinfo_virtMemReadHandler);
	if(n == NULL)
		goto errorDir;

	/* create threads-dir */
	tdir = vfsn_createDir(dir,(char*)"threads");
	if(tdir == NULL)
		goto errorDir;

	return tdir;

errorDir:
	vfsn_removeNode(dir);
errorName:
	kheap_free(name);
	return NULL;
}

void vfs_removeProcess(tPid pid) {
	sVFSNode *proc = PROCESSES();
	sProc *p = proc_getByPid(pid);
	char name[12];
	itoa(name,pid);

	/* remove from system:/processes */
	vfsn_removeNode(p->threadDir->parent);

	/* invalidate cache */
	if(proc->data.def.cache != NULL) {
		kheap_free(proc->data.def.cache);
		proc->data.def.cache = NULL;
	}
}

bool vfs_createThread(tTid tid,fRead handler) {
	char *name;
	sVFSNode *n;
	sThread *t = thread_getById(tid);

	/* build name */
	name = (char*)kheap_alloc(sizeof(char) * 12);
	if(name == NULL)
		return false;
	itoa(name,tid);

	/* create thread-node */
	n = vfsn_createInfo(tid,t->proc->threadDir,name,handler);
	if(n == NULL) {
		kheap_free(name);
		return false;
	}

	return true;
}

void vfs_removeThread(tTid tid) {
	sThread *t = thread_getById(tid);
	sVFSNode *rn[2] = {SERVICES(),DRIVERS()};
	sVFSNode *n,*tn;
	u32 i;
	char *name;

	/* check if the thread is a service */
	for(i = 0; i < 2; i++) {
		n = NODE_FIRST_CHILD(rn[i]->firstChild);
		while(n != NULL) {
			if((n->mode & MODE_TYPE_SERVICE) && n->owner == tid) {
				tn = n->next;
				vfs_removeService(tid,NADDR_TO_VNNO(n));
				n = tn;
			}
			else
				n = n->next;
		}
	}

	/* build name */
	name = (char*)kheap_alloc(sizeof(char) * 12);
	if(name == NULL)
		return;
	itoa(name,tid);

	/* search for thread-node and remove it */
	n = NODE_FIRST_CHILD(t->proc->threadDir);
	while(n != NULL) {
		if(strcmp(n->name,name) == 0) {
			vfsn_removeNode(n);
			break;
		}
		n = n->next;
	}

	kheap_free(name);
}

/* #### TEST/DEBUG FUNCTIONS #### */
#if DEBUGGING

u32 vfs_dbg_getGFTEntryCount(void) {
	u32 i,count = 0;
	for(i = 0; i < FILE_COUNT; i++) {
		if(globalFileTable[i].flags != 0)
			count++;
	}
	return count;
}

void vfs_dbg_printGFT(void) {
	tFileNo i;
	sGFTEntry *e = globalFileTable;
	vid_printf("Global File Table:\n");
	for(i = 0; i < FILE_COUNT; i++) {
		if(e->flags != 0) {
			sVFSNode *n = vfsn_getNode(e->nodeNo);
			vid_printf("\tfile @ index %d\n",i);
			vid_printf("\t\tread: %d\n",(e->flags & VFS_READ) ? true : false);
			vid_printf("\t\twrite: %d\n",(e->flags & VFS_WRITE) ? true : false);
			vid_printf("\t\tenv: %s\n",IS_VIRT(e->nodeNo) ? "virtual" : "real");
			vid_printf("\t\tnodeNo: %d\n",VIRT_INDEX(e->nodeNo));
			vid_printf("\t\tpos: %d\n",e->position);
			vid_printf("\t\trefCount: %d\n",e->refCount);
			vid_printf("\t\towner: %d\n",e->owner);
			if((n->mode & MODE_TYPE_SERVUSE))
				vid_printf("\t\tService-Usage of %s @ %s\n",n->name,n->parent->name);
		}
		e++;
	}
}

#endif
