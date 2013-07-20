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

#include <sys/common.h>
#include <sys/task/elf.h>
#include <sys/task/proc.h>
#include <sys/task/fd.h>
#include <sys/mem/paging.h>
#include <sys/mem/pmem.h>
#include <sys/mem/vmm.h>
#include <sys/mem/cache.h>
#include <sys/vfs/vfs.h>
#include <sys/log.h>
#include <sys/util.h>
#include <sys/video.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

int ELF::loadFromMem(const void *code,size_t length,StartupInfo *info) {
	size_t j,loadSegNo = 0;
	uintptr_t datPtr;
	sElfEHeader *eheader = (sElfEHeader*)code;
	sElfPHeader *pheader = NULL;

	/* check magic */
	if(memcmp(eheader->e_ident,ELFMAG,4) != 0) {
		vid_printf("[LOADER] Invalid magic-number '%02x%02x%02x%02x'\n",
				eheader->e_ident[0],eheader->e_ident[1],eheader->e_ident[2],eheader->e_ident[3]);
		return -ENOEXEC;
	}

	/* load the LOAD segments. */
	datPtr = (uintptr_t)code + eheader->e_phoff;
	for(j = 0; j < eheader->e_phnum; datPtr += eheader->e_phentsize, j++) {
		pheader = (sElfPHeader*)datPtr;
		/* check if all stuff is in the binary */
		if((uintptr_t)pheader + sizeof(sElfPHeader) >= (uintptr_t)code + length) {
			vid_printf("[LOADER] Unexpected end; pheader %d not finished\n",j);
			return -ENOEXEC;
		}

		if(pheader->p_type == PT_LOAD) {
			if(pheader->p_vaddr + pheader->p_filesz >= (uintptr_t)code + length) {
				vid_printf("[LOADER] Unexpected end; load segment %d not finished\n",loadSegNo);
				return -ENOEXEC;
			}
			if(addSegment(NULL,pheader,loadSegNo,TYPE_PROG) < 0)
				return -ENOEXEC;
			/* copy the data and zero the rest, if necessary */
			paging_copyToUser((void*)pheader->p_vaddr,(void*)((uintptr_t)code + pheader->p_offset),
					pheader->p_filesz);
			paging_zeroToUser((void*)(pheader->p_vaddr + pheader->p_filesz),
					pheader->p_memsz - pheader->p_filesz);
			loadSegNo++;
		}
	}

	if(finishFromMem(code,length,info) < 0)
		return -ENOEXEC;

	info->linkerEntry = info->progEntry = eheader->e_entry;
	return 0;
}

int ELF::doLoadFromFile(const char *path,int type,StartupInfo *info) {
	Thread *t = Thread::getRunning();
	Proc *p = t->getProc();
	sFile *file;
	size_t j,loadSeg = 0;
	uintptr_t datPtr;
	sElfEHeader eheader;
	sElfPHeader pheader;
	sFileInfo finfo;
	ssize_t readRes;
	int res,fd;
	char *interpName;

	res = vfs_openPath(p->getPid(),VFS_READ,path,&file);
	if(res < 0) {
		vid_printf("[LOADER] Unable to open path '%s': %s\n",path,strerror(-res));
		return -ENOEXEC;
	}

	/* fill bindesc */
	if((res = vfs_fstat(p->getPid(),file,&finfo)) < 0) {
		vid_printf("[LOADER] Unable to stat '%s': %s\n",path,strerror(-res));
		goto failed;
	}
	/* set suid and sgid */
	if(finfo.mode & S_ISUID)
		p->setSUid(finfo.uid);
	if(finfo.mode & S_ISGID)
		p->setSGid(finfo.gid);

	/* first read the header */
	if((readRes = vfs_readFile(p->getPid(),file,&eheader,sizeof(sElfEHeader))) != sizeof(sElfEHeader)) {
		vid_printf("[LOADER] Reading ELF-header of '%s' failed: %s\n",path,strerror(-readRes));
		goto failed;
	}

	/* check magic */
	if(memcmp(eheader.e_ident,ELFMAG,4) != 0) {
		vid_printf("[LOADER] Invalid magic-number '%02x%02x%02x%02x' in '%s'\n",
				eheader.e_ident[0],eheader.e_ident[1],eheader.e_ident[2],eheader.e_ident[3],path);
		goto failed;
	}

	/* by default set the same; the dl will overwrite it when needed */
	if(type == TYPE_PROG)
		info->linkerEntry = info->progEntry = eheader.e_entry;
	else
		info->linkerEntry = eheader.e_entry;

	/* load the LOAD segments. */
	datPtr = eheader.e_phoff;
	for(j = 0; j < eheader.e_phnum; datPtr += eheader.e_phentsize, j++) {
		/* go to header */
		if(vfs_seek(p->getPid(),file,(off_t)datPtr,SEEK_SET) < 0) {
			vid_printf("[LOADER] Seeking to position 0x%Ox failed\n",(off_t)datPtr);
			goto failed;
		}
		/* read pheader */
		if((readRes = vfs_readFile(p->getPid(),file,&pheader,sizeof(sElfPHeader))) != sizeof(sElfPHeader)) {
			vid_printf("[LOADER] Reading program-header %d of '%s' failed: %s\n",
					j,path,strerror(-readRes));
			goto failed;
		}

		if(pheader.p_type == PT_INTERP) {
			/* has to be the first segment and is not allowed for the dynamic linker */
			if(loadSeg > 0 || type != TYPE_PROG) {
				vid_printf("[LOADER] PT_INTERP segment is not first or we're loading the dynlinker\n");
				goto failed;
			}
			/* read name of dynamic linker */
			interpName = (char*)cache_alloc(pheader.p_filesz);
			if(interpName == NULL) {
				vid_printf("[LOADER] Allocating memory for dynamic linker name failed\n");
				goto failed;
			}
			Thread::addHeapAlloc(interpName);
			if(vfs_seek(p->getPid(),file,pheader.p_offset,SEEK_SET) < 0) {
				vid_printf("[LOADER] Seeking to dynlinker name (%Ox) failed\n",pheader.p_offset);
				goto failedInterpName;
			}
			if(vfs_readFile(p->getPid(),file,interpName,pheader.p_filesz) != (ssize_t)pheader.p_filesz) {
				vid_printf("[LOADER] Reading dynlinker name failed\n");
				goto failedInterpName;
			}
			vfs_closeFile(p->getPid(),file);
			/* now load him and stop loading the 'real' program */
			res = doLoadFromFile(interpName,TYPE_INTERP,info);
			Thread::remHeapAlloc(interpName);
			cache_free(interpName);
			return res;
		}

		if(pheader.p_type == PT_LOAD || pheader.p_type == PT_TLS) {
			if(addSegment(file,&pheader,loadSeg,type) < 0)
				goto failed;
			if(pheader.p_type == PT_TLS) {
				uintptr_t tlsStart;
				if(t->getTLSRange(&tlsStart,NULL)) {
					/* read tdata */
					if(vfs_seek(p->getPid(),file,(off_t)pheader.p_offset,SEEK_SET) < 0) {
						vid_printf("[LOADER] Seeking to load segment %d (%Ox) failed\n",
								loadSeg,pheader.p_offset);
						goto failed;
					}
					if((readRes = vfs_readFile(p->getPid(),file,(void*)tlsStart,pheader.p_filesz)) < 0) {
						vid_printf("[LOADER] Reading load segment %d failed: %s\n",
								loadSeg,strerror(-readRes));
						goto failed;
					}
					/* clear tbss */
					paging_zeroToUser((void*)(tlsStart + pheader.p_filesz),
							pheader.p_memsz - pheader.p_filesz);
				}
			}
			loadSeg++;
		}
	}

	/* introduce a file-descriptor during finishing; this way we'll close the file when segfaulting */
	if((fd = fd_assoc(file)) < 0)
		goto failed;
	if(finishFromFile(file,&eheader,info) < 0) {
		assert(fd_unassoc(fd) != NULL);
		goto failed;
	}
	assert(fd_unassoc(fd) != NULL);
	vfs_closeFile(p->getPid(),file);
	return 0;

failedInterpName:
	Thread::remHeapAlloc(interpName);
	cache_free(interpName);
failed:
	vfs_closeFile(p->getPid(),file);
	return -ENOEXEC;
}

int ELF::addSegment(sFile *file,const sElfPHeader *pheader,size_t loadSegNo,int type) {
	Thread *t = Thread::getRunning();
	int res,prot = 0,flags = type == TYPE_INTERP ? 0 : MAP_FIXED;
	sVMRegion *vm;
	size_t memsz = pheader->p_memsz;

	/* determine protection flags */
	if(pheader->p_flags & PF_R)
		prot |= PROT_READ;
	if(pheader->p_flags & PF_W)
		prot |= PROT_WRITE;
	if(pheader->p_flags & PF_X)
		prot |= PROT_EXEC;

	/* determine type */
	if(loadSegNo == 0) {
		/* dynamic linker has a special entrypoint */
		if(type == TYPE_INTERP && pheader->p_vaddr != INTERP_TEXT_BEGIN) {
			vid_printf("[LOADER] Dynamic linker text does not start at %p\n",INTERP_TEXT_BEGIN);
			return -ENOEXEC;
		}
		/* text regions are shared */
		flags |= MAP_SHARED;
	}
	else if(pheader->p_type == PT_TLS) {
		/* not allowed for the dynamic linker */
		if(type == TYPE_INTERP) {
			vid_printf("[LOADER] TLS segment not allowed for dynamic linker\n");
			return -ENOEXEC;
		}
		/* we need the thread-control-block at the end */
		memsz += sizeof(void*);
		/* tls needs no binary */
		file = NULL;
		flags &= ~MAP_FIXED;
		flags |= MAP_TLS;
		/* the linker seems to think that readible is enough for TLS. so set the protection explicitly */
		prot = PROT_READ | PROT_WRITE;
	}
	else if(pheader->p_flags == (PF_R | PF_W))
		flags |= MAP_GROWABLE;
	else {
		vid_printf("[LOADER] Unrecognized load segment (no=%d, type=%x, flags=%x)\n",
				loadSegNo,pheader->p_type,pheader->p_flags);
		return -ENOEXEC;
	}

	/* check if the sizes are valid */
	if(pheader->p_filesz > memsz) {
		vid_printf("[LOADER] Number of bytes in file (%zx) more than number of bytes in mem (%zx)\n",
				pheader->p_filesz,memsz);
		return -ENOEXEC;
	}

	/* regions without binary will not be demand-loaded */
	if(file == NULL)
		t->reserveFrames(BYTES_2_PAGES(memsz));

	/* add the region */
	if((res = vmm_map(t->getProc()->getPid(),pheader->p_vaddr,memsz,pheader->p_filesz,prot,flags,file,
			pheader->p_offset,&vm)) < 0) {
		vid_printf("[LOADER] Unable to add region: %s\n",strerror(-res));
		t->discardFrames();
		return res;
	}
	if(pheader->p_type == PT_TLS)
		t->setTLSRegion(vm);
	t->discardFrames();
	return 0;
}
