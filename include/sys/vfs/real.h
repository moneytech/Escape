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

#ifndef VFSREAL_H_
#define VFSREAL_H_

#include <sys/common.h>
#include <esc/fsinterface.h>

/**
 * Inits vfs-real
 */
void vfsr_init(void);

/**
 * Removes the given thread (closes fs-communication-file)
 *
 * @param tid the thread-id
 */
void vfsr_removeThread(tTid tid);

/**
 * Opens the given path with given flags for given thread
 *
 * @param tid the thread-id
 * @param flags read / write
 * @param path the path
 * @return 0 on success or the error-code
 */
s32 vfsr_openFile(tTid tid,u16 flags,const char *path);

/**
 * Opens the given inode+devno with given flags for given thread
 *
 * @param tid the thread-id
 * @param flags read / write
 * @param ino the inode-number
 * @param dev the dev-number
 * @return 0 on success or the error-code
 */
s32 vfsr_openInode(tTid tid,u16 flags,tInodeNo ino,tDevNo dev);

/**
 * Retrieves information about the given (real!) path
 *
 * @param tid the thread-id
 * @param path the path in the real filesystem
 * @param info should be filled
 * @return 0 on success
 */
s32 vfsr_stat(tTid tid,const char *path,sFileInfo *info);

/**
 * Retrieves information about the given inode on given device
 *
 * @param tid the thread-id
 * @param ino the inode-number
 * @param devNo the device-number
 * @param info should be filled
 * @return 0 on success
 */
s32 vfsr_istat(tTid tid,tInodeNo ino,tDevNo devNo,sFileInfo *info);

/**
 * Reads from the given inode at <offset> <count> bytes into the given buffer
 *
 * @param tid the thread-id
 * @param inodeNo the inode
 * @param devNo the device-number
 * @param buffer the buffer to fill
 * @param offset the offset in the data
 * @param count the number of bytes to copy
 * @return the number of read bytes
 */
s32 vfsr_readFile(tTid tid,tInodeNo inodeNo,tDevNo devNo,u8 *buffer,u32 offset,u32 count);

/**
 * Writes to the given inode at <offset> <count> bytes from the given buffer
 *
 * @param tid the thread-id
 * @param inodeNo the inode
 * @param devNo the device-number
 * @param buffer the buffer
 * @param offset the offset in the inode
 * @param count the number of bytes to copy
 * @return the number of written bytes
 */
s32 vfsr_writeFile(tTid tid,tInodeNo inodeNo,tDevNo devNo,const u8 *buffer,u32 offset,u32 count);

/**
 * Creates a hardlink at <newPath> which points to <oldPath>
 *
 * @param tid the thread-id
 * @param oldPath the link-target
 * @param newPath the link-path
 * @return 0 on success
 */
s32 vfsr_link(tTid tid,const char *oldPath,const char *newPath);

/**
 * Unlinks the given path. That means, the directory-entry will be removed and if there are no
 * more references to the inode, it will be removed.
 *
 * @param tid the thread-id
 * @param path the path
 * @return 0 on success
 */
s32 vfsr_unlink(tTid tid,const char *path);

/**
 * Creates the given directory. Expects that all except the last path-component exist.
 *
 * @param tid the thread-id
 * @param path the path
 * @return 0 on success
 */
s32 vfsr_mkdir(tTid tid,const char *path);

/**
 * Removes the given directory. Expects that the directory is empty (except '.' and '..')
 *
 * @param tid the thread-id
 * @param path the path
 * @return 0 on success
 */
s32 vfsr_rmdir(tTid tid,const char *path);

/**
 * Mounts <device> at <path> with fs <type>
 *
 * @param tid the thread-id
 * @param device the device-path
 * @param path the path to mount at
 * @param type the fs-type
 * @return 0 on success
 */
s32 vfsr_mount(tTid tid,const char *device,const char *path,u16 type);

/**
 * Unmounts the device mounted at <path>
 *
 * @param tid the thread-id
 * @param path the path
 * @return 0 on success
 */
s32 vfsr_unmount(tTid tid,const char *path);

/**
 * Writes all dirty objects of the filesystem to disk
 *
 * @param tid the thread-id
 * @return 0 on success
 */
s32 vfsr_sync(tTid tid);

/**
 * Closes the given inode
 *
 * @param tid the thread-id
 * @param inodeNo the inode
 * @param devNo the device-number
 */
void vfsr_closeFile(tTid tid,tInodeNo inodeNo,tDevNo devNo);

#endif /* VFSREAL_H_ */
