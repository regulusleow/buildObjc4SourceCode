/*
 * Copyright (c) 2016 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/*-
 * Portions Copyright (c) 1992, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *  @(#)null_subr.c 8.7 (Berkeley) 5/14/95
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/vnode.h>

#include "nullfs.h"

/*
 * Null layer cache:
 * Each cache entry holds a reference to the lower vnode
 * along with a pointer to the alias vnode.  When an
 * entry is added the lower vnode is VREF'd.  When the
 * alias is removed the lower vnode is vrele'd.
 */

#define NULL_HASH_SIZE (desiredvnodes / 10)

/* osx doesn't really have the functionality freebsd uses here..gonna try this
 * hacked hash...*/
#define NULL_NHASH(vp) (&null_node_hashtbl[((((uintptr_t)vp) >> vnsz2log) + (uintptr_t)vnode_mount(vp)) & null_hash_mask])

static LIST_HEAD(null_node_hashhead, null_node) * null_node_hashtbl;
static LCK_GRP_DECLARE(null_hashlck_grp, "com.apple.filesystems.nullfs");
static LCK_MTX_DECLARE(null_hashmtx, &null_hashlck_grp);
static u_long null_hash_mask;

/* os x doesn't have hashes built into vnode. gonna try doing what freebsd does
 *  anyway
 *  Don't want to create a dependency on vnode_internal.h and the real struct
 *  vnode.
 *  9 is an eyeball of the log 2 size of vnode */
static int vnsz2log = 9;

static int null_hashins(struct mount *, struct null_node *, struct vnode **);

void
nullfs_init_lck(lck_mtx_t * lck)
{
	lck_mtx_init(lck, &null_hashlck_grp, LCK_ATTR_NULL);
}

void
nullfs_destroy_lck(lck_mtx_t * lck)
{
	lck_mtx_destroy(lck, &null_hashlck_grp);
}

/*
 * Initialise cache headers
 */
int
nullfs_init(__unused struct vfsconf * vfsp)
{
	NULLFSDEBUG("%s\n", __FUNCTION__);
	null_node_hashtbl = hashinit(NULL_HASH_SIZE, M_TEMP, &null_hash_mask);
	NULLFSDEBUG("%s finished\n", __FUNCTION__);
	return 0;
}

int
nullfs_uninit(void)
{
	/* This gets called when the fs is uninstalled, there wasn't an exact
	 * equivalent in vfsops */
	hashdestroy(null_node_hashtbl, M_TEMP, null_hash_mask);
	return 0;
}

/*
 * Find the nullfs vnode mapped to lowervp. Return it in *vpp with an iocount if found.
 * Return 0 on success. On failure *vpp will be null and a non-zero error code will be returned.
 */
int
null_hashget(struct mount * mp, struct vnode * lowervp, struct vnode ** vpp)
{
	struct null_node_hashhead * hd = NULL;
	struct null_node * a = NULL;
	struct vnode * vp = NULL;
	uint32_t vp_vid = 0;
	int error = ENOENT;

	/*
	 * Find hash base, and then search the (two-way) linked
	 * list looking for a null_node structure which is referencing
	 * the lower vnode. We only give up our reference at reclaim so
	 * just check whether the lowervp has gotten pulled from under us
	 */
	hd = NULL_NHASH(lowervp);
	// In the future we should consider using a per bucket lock
	lck_mtx_lock(&null_hashmtx);
	LIST_FOREACH(a, hd, null_hash)
	{
		if (a->null_lowervp == lowervp && vnode_mount(NULLTOV(a)) == mp) {
			vp = NULLTOV(a);
			if (a->null_lowervid != vnode_vid(lowervp)) {
				/*lowervp has reved */
				error = EIO;
				vp = NULL;
			} else {
				vp_vid = a->null_myvid;
			}
			// In the case of a succesful look-up we should consider moving the object to the top of the head
			break;
		}
	}
	if (vp != NULL) {
		vnode_hold(vp);
	}
	lck_mtx_unlock(&null_hashmtx);
	if (vp != NULL) {
		error = vnode_getwithvid(vp, vp_vid);
		if (error == 0) {
			*vpp = vp;
		}
		vnode_drop(vp);
	}
	return error;
}

/*
 * Act like null_hashget, but add passed null_node to hash if no existing
 * node found.
 */
static int
null_hashins(struct mount * mp, struct null_node * xp, struct vnode ** vpp)
{
	struct null_node_hashhead * hd = NULL;
	struct null_node * oxp = NULL;
	struct vnode * ovp = NULL;
	uint32_t oxp_vid = 0;
	int error = 0;

	hd = NULL_NHASH(xp->null_lowervp);
	lck_mtx_lock(&null_hashmtx);
	LIST_FOREACH(oxp, hd, null_hash)
	{
		if (oxp->null_lowervp == xp->null_lowervp && vnode_mount(NULLTOV(oxp)) == mp) {
			/*
			 * See null_hashget for a description of this
			 * operation.
			 */
			ovp = NULLTOV(oxp);
			if (oxp->null_lowervid != vnode_vid(oxp->null_lowervp)) {
				/*vp doesn't exist so return null (not sure we are actually gonna catch
				 *  recycle right now
				 *  This is an exceptional case right now, it suggests the vnode we are
				 *  trying to add has been recycled
				 *  don't add it.*/
				error = EIO;
				ovp = NULL;
			} else {
				oxp_vid = oxp->null_myvid;
			}
			goto end;
		}
	}
	/* if it wasn't in the hash map then the vnode pointed to by xp already has a
	 * iocount so don't bother */
	LIST_INSERT_HEAD(hd, xp, null_hash);
	xp->null_flags |= NULL_FLAG_HASHED;
end:
	if (ovp != NULL) {
		vnode_hold(ovp);
	}
	lck_mtx_unlock(&null_hashmtx);
	if (ovp != NULL) {
		/* if we found something in the hash map then grab an iocount */
		error = vnode_getwithvid(ovp, oxp_vid);
		if (error == 0) {
			*vpp = ovp;
		}
		vnode_drop(ovp);
	}
	return error;
}

/*
 * Remove node from hash.
 */
void
null_hashrem(struct null_node * xp)
{
	lck_mtx_lock(&null_hashmtx);
	LIST_REMOVE(xp, null_hash);
	lck_mtx_unlock(&null_hashmtx);
}

static struct null_node *
null_nodecreate(struct vnode * lowervp)
{
	struct null_node * xp;

	xp = kalloc_type(struct null_node, Z_WAITOK | Z_ZERO | Z_NOFAIL);
	if (lowervp) {
		xp->null_lowervp  = lowervp;
		xp->null_lowervid = vnode_vid(lowervp);
	}
	return xp;
}

/* assumption is that vnode has iocount on it after vnode create */
int
null_getnewvnode(
	struct mount * mp, struct vnode * lowervp, struct vnode * dvp, struct vnode ** vpp, struct componentname * cnp, int root)
{
	struct vnode_fsparam vnfs_param;
	int error             = 0;
	enum vtype type       = VDIR;
	struct null_node * xp = null_nodecreate(lowervp);

	if (xp == NULL) {
		return ENOMEM;
	}

	if (lowervp) {
		type = vnode_vtype(lowervp);
	}

	vnfs_param.vnfs_mp         = mp;
	vnfs_param.vnfs_vtype      = type;
	vnfs_param.vnfs_str        = "nullfs";
	vnfs_param.vnfs_dvp        = dvp;
	vnfs_param.vnfs_fsnode     = (void *)xp;
	vnfs_param.vnfs_vops       = nullfs_vnodeop_p;
	vnfs_param.vnfs_markroot   = root;
	vnfs_param.vnfs_marksystem = 0;
	vnfs_param.vnfs_rdev       = 0;
	vnfs_param.vnfs_filesize   = 0; // set this to 0 since we should only be shadowing non-regular files
	vnfs_param.vnfs_cnp        = cnp;
	vnfs_param.vnfs_flags      = VNFS_ADDFSREF;

	error = vnode_create_ext(VNCREATE_FLAVOR, VCREATESIZE, &vnfs_param, vpp, VNODE_CREATE_DEFAULT);
	if (error == 0) {
		xp->null_vnode = *vpp;
		xp->null_myvid = vnode_vid(*vpp);
		vnode_settag(*vpp, VT_NULL);
	} else {
		kfree_type(struct null_node, xp);
	}
	return error;
}

/*
 * Make a new or get existing nullfs node.
 * Vp is the alias vnode, lowervp is the lower vnode.
 *
 * lowervp is assumed to have an iocount on it from the caller
 */
int
null_nodeget(
	struct mount * mp, struct vnode * lowervp, struct vnode * dvp, struct vnode ** vpp, struct componentname * cnp, int root)
{
	struct vnode * vp;
	int error;

	/* Lookup the hash firstly. */
	error = null_hashget(mp, lowervp, vpp);
	/* ENOENT means it wasn't found, EIO is a failure we should bail from, 0 is it
	 * was found */
	if (error != ENOENT) {
		/* null_hashget checked the vid, so if we got something here its legit to
		 * the best of our knowledge*/
		/* if we found something then there is an iocount on vpp,
		 *  if we didn't find something then vpp shouldn't be used by the caller */
		return error;
	}

	/*
	 * We do not serialize vnode creation, instead we will check for
	 * duplicates later, when adding new vnode to hash.
	 */
	error = vnode_ref(lowervp); // take a ref on lowervp so we let the system know we care about it
	if (error) {
		// Failed to get a reference on the lower vp so bail. Lowervp may be gone already.
		return error;
	}

	error = null_getnewvnode(mp, lowervp, dvp, &vp, cnp, root);

	if (error) {
		vnode_rele(lowervp);
		return error;
	}

	/*
	 * Atomically insert our new node into the hash or vget existing
	 * if someone else has beaten us to it.
	 */
	error = null_hashins(mp, VTONULL(vp), vpp);
	if (error || *vpp != NULL) {
		/* recycle will call reclaim which will get rid of the internals */
		vnode_recycle(vp);
		vnode_put(vp);
		/* if we found vpp, then null_hashins put an iocount on it */
		return error;
	}

	/* vp has an iocount from null_getnewvnode */
	*vpp = vp;

	return 0;
}
