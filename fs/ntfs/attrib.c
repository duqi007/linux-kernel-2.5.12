/**
 * attrib.c - NTFS attribute operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (C) 2002 Richard Russon.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ntfs.h"

/* Temporary helper functions -- might become macros */

/**
 * rl_mm - run_list memmove
 *
 * It is up to the caller to serialize access to the run list @base.
 */
static inline void rl_mm(run_list_element *base, int dst, int src, int size)
{
	if ((dst != src) && (size > 0))
		memmove (base + dst, base + src, size * sizeof (*base));
}

/**
 * rl_mc - run_list memory copy
 *
 * It is up to the caller to serialize access to the run lists @dstbase and
 * @srcbase.
 */
static inline void rl_mc(run_list_element *dstbase, int dst,
		run_list_element *srcbase, int src, int size)
{
	if (size > 0)
		memcpy (dstbase+dst, srcbase+src, size * sizeof (*dstbase));
}

/**
 * ntfs_rl_realloc - Reallocate memory for run_lists
 * @orig:  The original memory allocation
 * @old:   The number of run_lists in the original
 * @new:   The number of run_lists we need space for
 *
 * As the run_lists grow, more memory will be required.  To prevent the
 * kernel having to allocate and reallocate large numbers of small bits of
 * memory, this function returns and entire page of memory.
 *
 * It is up to the caller to serialize access to the run list @orig.
 *
 * N.B.  If the new allocation doesn't require a different number of pages in
 *       memory, the function will return the original pointer.
 *
 * Return: Pointer  The newly allocated, or recycled,  memory.
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 */
static inline run_list_element *ntfs_rl_realloc(run_list_element *orig,
		int old, int new)
{
	run_list_element *nrl;

	old = PAGE_ALIGN (old * sizeof (*orig));
	new = PAGE_ALIGN (new * sizeof (*orig));
	if (old == new)
		return orig;

	nrl = ntfs_malloc_nofs (new);
	if (!nrl)
		return ERR_PTR (-ENOMEM);

	if (orig) {
		memcpy (nrl, orig, min (old, new));
		ntfs_free (orig);
	}
	return nrl;
}

/**
 * ntfs_rl_merge - Join together two run_lists
 * @one:  The first run_list and destination
 * @two:  The second run_list
 *
 * If possible merge together two run_lists.  For this, their VCNs and LCNs
 * must be adjacent.
 *
 * It is up to the caller to serialize access to the run lists @one and @two.
 *
 * Return: TRUE   Success, the run_lists were merged
 *         FALSE  Failure, the run_lists were not merged
 */
static inline BOOL ntfs_rl_merge(run_list_element *one, run_list_element *two)
{
	BUG_ON (!one || !two);

	if ((one->lcn < 0) || (two->lcn < 0))     /* Are we merging holes? */
		return FALSE;
	if ((one->lcn + one->length) != two->lcn) /* Are the runs contiguous? */
		return FALSE;
	if ((one->vcn + one->length) != two->vcn) /* Are the runs misaligned? */
		return FALSE;

	one->length += two->length;
	return TRUE;
}

/**
 * ntfs_rl_append - Append a run_list after the given element
 * @orig:   The original run_list to be worked on.
 * @osize:  The number of elements in @orig (including end marker).
 * @new:    The run_list to be inserted.
 * @nsize:  The number of elements in @new (excluding end marker).
 * @loc:    Append the new run_list after this element in @orig.
 *
 * Append a run_list after element @loc in @orig.  Merge the right end of
 * the new run_list, if necessary.  Adjust the size of the hole before the
 * appended run_list.
 *
 * It is up to the caller to serialize access to the run lists @orig and @new.
 *
 * Return: Pointer, The new, combined, run_list
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 */
static inline run_list_element *ntfs_rl_append(run_list_element *orig,
		int osize, run_list_element *new, int nsize, int loc)
{
	run_list_element *res;
	BOOL right;

	BUG_ON (!orig || !new);

	/* First, merge the right hand end, if necessary. */
	right = ntfs_rl_merge (new + nsize - 1, orig + loc + 1);

	/* Space required: Orig size + New size, less one if we merged. */
	res = ntfs_rl_realloc (orig, osize, osize + nsize - right);
	if (IS_ERR (res))
		return res;

	/* Move the tail of Orig out of the way, then copy in New. */
	rl_mm (res, loc + 1 + nsize, loc + 1 + right, osize - loc - 1 - right);
	rl_mc (res, loc + 1, new, 0, nsize);

	/* Adjust the size of the preceding hole. */
	res[loc].length = res[loc+1].vcn - res[loc].vcn;

	/* We may have changed the length of the file, so fix the end marker */
	if (res[loc+nsize+1].lcn == LCN_ENOENT)
		res[loc+nsize+1].vcn = res[loc+nsize].vcn + res[loc+nsize].length;

	return res;
}

/**
 * ntfs_rl_insert - Insert a run_list into another
 * @orig:   The original run_list to be worked on.
 * @osize:  The number of elements in @orig (including end marker).
 * @new:    The run_list to be inserted.
 * @nsize:  The number of elements in @new (excluding end marker).
 * @loc:    Insert the new run_list before this element in @orig.
 *
 * Insert a run_list before element @loc in @orig.  Merge the left end of
 * the new run_list, if necessary.  Adjust the size of the hole after the
 * inserted run_list.
 *
 * It is up to the caller to serialize access to the run lists @orig and @new.
 *
 * Return: Pointer, The new, combined, run_list
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 */
static inline run_list_element *ntfs_rl_insert(run_list_element *orig,
		int osize, run_list_element *new, int nsize, int loc)
{
	run_list_element *res;
	BOOL left = FALSE;
	BOOL disc = FALSE;	/* Discontinuity */
	BOOL hole = FALSE;	/* Following a hole */

	BUG_ON (!orig || !new);

	/* disc => Discontinuity between the end of Orig and the start of New.
	 *         This means we might need to insert a hole.
	 * hole => Orig ends with a hole or an unmapped region which we can
	 *         extend to match the discontinuity. */
	if (loc == 0) {
		disc = (new[0].vcn > 0);
	} else {
		left = ntfs_rl_merge (orig + loc - 1, new);

		disc = (new[0].vcn > (orig[loc-1].vcn + orig[loc-1].length));
		if (disc)
			hole = (orig[loc-1].lcn == LCN_HOLE);
	}

	/* Space required: Orig size + New size, less one if we merged,
	 * plus one if there was a discontinuity, less one for a trailing hole */
	res = ntfs_rl_realloc (orig, osize, osize + nsize - left + disc - hole);
	if (IS_ERR (res))
		return res;

	/* Move the tail of Orig out of the way, then copy in New. */
	rl_mm (res, loc + nsize - left + disc - hole, loc, osize - loc);
	rl_mc (res, loc + disc - hole, new, left, nsize - left);

	/* Adjust the VCN of the last run ... */
	if (res[loc+nsize-left+disc-hole].lcn <= LCN_HOLE) {
		res[loc+nsize-left+disc-hole].vcn =
			res[loc+nsize-left+disc-hole-1].vcn +
			res[loc+nsize-left+disc-hole-1].length;
	}
	/* ... and the length. */
	if ((res[loc+nsize-left+disc-hole].lcn == LCN_HOLE) ||
	    (res[loc+nsize-left+disc-hole].lcn == LCN_RL_NOT_MAPPED)) {
		res[loc+nsize-left+disc-hole].length =
			res[loc+nsize-left+disc-hole+1].vcn -
			res[loc+nsize-left+disc-hole].vcn;
	}

	/* Writing beyond the end of the file and there's a discontinuity. */
	if (disc) {
		if (hole) {
			res[loc-1].length = res[loc].vcn - res[loc-1].vcn;
		} else {
			if (loc > 0) {
				res[loc].vcn = res[loc-1].vcn +
					res[loc-1].length;
				res[loc].length = res[loc+1].vcn - res[loc].vcn;
			} else {
				res[loc].vcn = 0;
				res[loc].length = res[loc+1].vcn;
			}
			res[loc].lcn = LCN_RL_NOT_MAPPED;
		}

		if (res[loc+nsize-left+disc].lcn == LCN_ENOENT)
			res[loc+nsize-left+disc].vcn = res[loc+nsize-left+disc-1].vcn +
				res[loc+nsize-left+disc-1].length;
	}

	return res;
}

/**
 * ntfs_rl_replace - Overwrite a run_list element with another run_list
 * @orig:   The original run_list to be worked on.
 * @osize:  The number of elements in @orig (including end marker).
 * @new:    The run_list to be inserted.
 * @nsize:  The number of elements in @new (excluding end marker).
 * @loc:    Index of run_list @orig to overwrite with @new.
 *
 * Replace the run_list at @loc with @new.  Merge the left and right ends of
 * the inserted run_list, if necessary.
 *
 * It is up to the caller to serialize access to the run lists @orig and @new.
 *
 * Return: Pointer, The new, combined, run_list
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 */
static inline run_list_element *ntfs_rl_replace(run_list_element *orig,
		int osize, run_list_element *new, int nsize, int loc)
{
	run_list_element *res;
	BOOL left = FALSE;
	BOOL right;

	BUG_ON (!orig || !new);

	/* First, merge the left and right ends, if necessary. */
	right = ntfs_rl_merge (new + nsize - 1, orig + loc + 1);
	if (loc > 0)
		left = ntfs_rl_merge (orig + loc - 1, new);

	/* Allocate some space.  We'll need less if the left, right
	 * or both ends were merged. */
	res = ntfs_rl_realloc (orig, osize, osize + nsize - left - right);
	if (IS_ERR (res))
		return res;

	/* Move the tail of Orig out of the way, then copy in New. */
	rl_mm (res, loc + nsize - left, loc + right + 1,
		osize - loc - right - 1);
	rl_mc (res, loc, new, left, nsize - left);

	/* We may have changed the length of the file, so fix the end marker */
	if (res[loc+nsize-left].lcn == LCN_ENOENT)
		res[loc+nsize-left].vcn = res[loc+nsize-left-1].vcn +
					  res[loc+nsize-left-1].length;
	return res;
}

/**
 * ntfs_rl_split - Insert a run_list into the centre of a hole
 * @orig:   The original run_list to be worked on.
 * @osize:  The number of elements in @orig (including end marker).
 * @new:    The run_list to be inserted.
 * @nsize:  The number of elements in @new (excluding end marker).
 * @loc:    Index of run_list in @orig to split with @new.
 *
 * Split the run_list at @loc into two and insert @new.  No merging of
 * run_lists is necessary.  Adjust the size of the holes either side.
 *
 * It is up to the caller to serialize access to the run lists @orig and @new.
 *
 * Return: Pointer, The new, combined, run_list
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 */
static inline run_list_element *ntfs_rl_split(run_list_element *orig, int osize,
		run_list_element *new, int nsize, int loc)
{
	run_list_element *res;

	BUG_ON (!orig || !new);

	/* Space required: Orig size + New size + One new hole. */
	res = ntfs_rl_realloc (orig, osize, osize + nsize + 1);
	if (IS_ERR (res))
		return res;

	/* Move the tail of Orig out of the way, then copy in New. */
	rl_mm (res, loc + 1 + nsize, loc, osize - loc);
	rl_mc (res, loc + 1, new, 0, nsize);

	/* Adjust the size of the holes either size of New. */
	res[loc].length         = res[loc+1].vcn       - res[loc].vcn;
	res[loc+nsize+1].vcn    = res[loc+nsize].vcn   + res[loc+nsize].length;
	res[loc+nsize+1].length = res[loc+nsize+2].vcn - res[loc+nsize+1].vcn;

	return res;
}

/**
 * merge_run_lists - merge two run_lists into one
 * @drl:  The original run_list.
 * @srl:  The new run_list to be merge into @drl.
 *
 * First we sanity check the two run_lists to make sure that they are sensible
 * and can be merged.  The @srl run_list must be either after the @drl run_list
 * or completely within a hole in @drl.
 *
 * It is up to the caller to serialize access to the run lists @drl and @srl.
 *
 * Merging of run lists is necessary in two cases:
 *   1. When attribute lists are used and a further extent is being mapped.
 *   2. When new clusters are allocated to fill a hole or extend a file.
 *
 * There are four possible ways @srl can be merged.  It can be inserted at
 * the beginning of a hole; split the hole in two; appended at the end of
 * a hole; replace the whole hole.  It can also be appended to the end of
 * the run_list, which is just a variant of the insert case.
 *
 * N.B.  Either, or both, of the input pointers may be freed if the function
 *       is successful.  Only the returned pointer may be used.
 *
 *       If the function fails, neither of the input run_lists may be safe.
 *
 * Return: Pointer, The resultant merged run_list.
 *
 * Errors: -ENOMEM, Not enough memory to allocate run list array.
 *         -EINVAL, Invalid parameters were passed in.
 *         -ERANGE, The run_lists overlap and cannot be merged.
 */
run_list_element *merge_run_lists(run_list_element *drl, run_list_element *srl)
{
	run_list_element *nrl;	/* New run list. */
	int di, si;		/* Current index into @[ds]rl. */
	int sstart;		/* First index with lcn > LCN_RL_NOT_MAPPED. */
	int dins;		/* Index into @drl at which to insert @srl. */
	int dend, send;		/* Last index into @[ds]rl. */
	int dfinal, sfinal;	/* The last index into @[ds]rl with
				   lcn >= LCN_HOLE. */
	int marker = 0;

#if 1
	ntfs_debug ("dst:");
	ntfs_debug_dump_runlist (drl);
	ntfs_debug ("src:");
	ntfs_debug_dump_runlist (srl);
#endif

 	/* Check for silly calling... */
	if (unlikely (!srl))
		return drl;
	if (unlikely (IS_ERR (srl) || IS_ERR (drl)))
		return ERR_PTR (-EINVAL);

	/* Check for the case where the first mapping is being done now. */
	if (unlikely (!drl)) {
		nrl = srl;

		/* Complete the source run list if necessary. */
		if (unlikely (srl[0].vcn)) {
			/* Scan to the end of the source run list. */
			for (send = 0; likely (srl[send].length); send++)
				;
			nrl = ntfs_rl_realloc (srl, send, send + 1);
			if (!nrl)
				return ERR_PTR (-ENOMEM);

			rl_mm (nrl, 1, 0, send);
			nrl[0].vcn = 0;			/* Add start element. */
			nrl[0].lcn = LCN_RL_NOT_MAPPED;
			nrl[0].length = nrl[1].vcn;
		}
		goto finished;
	}

	si = di = 0;

	/* Skip the unmapped start element(s) in each run_list if present. */
	while (srl[si].length && srl[si].lcn < (LCN)LCN_HOLE)
		si++;

	/* Can't have an entirely unmapped srl run_list. */
	BUG_ON (!srl[si].length);

	/* Record the starting points. */
	sstart = si;

	/*
	 * Skip forward in @drl until we reach the position where @srl needs to
	 * be inserted. If we reach the end of @drl, @srl just needs to be
	 * appended to @drl.
	 */
	for (; drl[di].length; di++) {
		if ((drl[di].vcn + drl[di].length) > srl[sstart].vcn)
			break;
	}
	dins = di;

	/* Sanity check for illegal overlaps. */
	if ((drl[di].vcn == srl[si].vcn) &&
	    (drl[di].lcn >= 0) &&
	    (srl[si].lcn >= 0)) {
		ntfs_error (NULL, "Run lists overlap. Cannot merge! Returning "
				"ERANGE.");
		nrl = ERR_PTR (-ERANGE);
		goto exit;
	}

	/* Scan to the end of both run lists in order to know their sizes. */
	for (send = si; srl[send].length; send++)
		;
	for (dend = di; drl[dend].length; dend++)
		;

	if (srl[send].lcn == LCN_ENOENT) {
		marker = send;
	}

	/* Scan to the last element with lcn >= LCN_HOLE. */
	for (sfinal = send; sfinal >= 0 && srl[sfinal].lcn < LCN_HOLE; sfinal--)
		;
	for (dfinal = dend; dfinal >= 0 && drl[dfinal].lcn < LCN_HOLE; dfinal--)
		;

	{
	BOOL start;
	BOOL finish;
	int ds = dend   + 1;		/* Number of elements in drl & srl */
	int ss = sfinal - sstart + 1;

	start  = ((drl[dins].lcn <  LCN_RL_NOT_MAPPED) ||    /* End of file   */
		  (drl[dins].vcn == srl[sstart].vcn));	     /* Start of hole */
	finish = ((drl[dins].lcn >= LCN_RL_NOT_MAPPED) &&    /* End of file   */
		 ((drl[dins].vcn + drl[dins].length) <=      /* End of hole   */
		  (srl[send-1].vcn + srl[send-1].length)));

	/* Or we'll lose an end marker */
	if (start && finish && (drl[dins].length == 0))
		ss++;
	if (marker && (drl[dins].vcn + drl[dins].length > srl[send-1].vcn))
		finish = FALSE;

#if 0
	ntfs_debug("dfinal = %i, dend = %i", dfinal, dend);
	ntfs_debug("sstart = %i, sfinal = %i, send = %i", sstart, sfinal, send);
	ntfs_debug("start = %i, finish = %i", start, finish);
	ntfs_debug("ds = %i, ss = %i, dins = %i", ds, ss, dins);
#endif
	if (start)
		if (finish)
			nrl = ntfs_rl_replace (drl, ds, srl + sstart, ss, dins);
		else
			nrl = ntfs_rl_insert  (drl, ds, srl + sstart, ss, dins);
	else
		if (finish)
			nrl = ntfs_rl_append  (drl, ds, srl + sstart, ss, dins);
		else
			nrl = ntfs_rl_split   (drl, ds, srl + sstart, ss, dins);

	if (marker && !IS_ERR(nrl)) {
		for (ds = 0; nrl[ds].length; ds++)
			;
		nrl = ntfs_rl_insert(nrl, ds + 1, srl + marker, 1, ds);
	}
	}

	if (likely (!IS_ERR (nrl))) {
		/* The merge was completed successfully. */
finished:
		if (nrl != srl)
			ntfs_free (srl);
		/*ntfs_debug ("Done.");*/
		/*ntfs_debug ("Merged run list:");*/

#if 1
		ntfs_debug ("res:");
		ntfs_debug_dump_runlist (nrl);
#endif
	} else {
		ntfs_error (NULL, "Merge failed, returning error code %ld.",
				-PTR_ERR (nrl));
	}
exit:
	return nrl;
}

/**
 * decompress_mapping_pairs - convert mapping pairs array to run list
 * @vol:	ntfs volume on which the attribute resides
 * @attr:	attribute record whose mapping pairs array to decompress
 * @run_list:	optional run list in which to insert @attr's run list
 *
 * Decompress the attribute @attr's mapping pairs array into a run_list and
 * return the run list or -errno on error. If @run_list is not NULL then
 * the mapping pairs array of @attr is decompressed and the run list inserted
 * into the appropriate place in @run_list. If this is the case and the
 * function returns success, the original pointer passed into @run_list is no
 * longer valid.
 *
 * It is up to the caller to serialize access to the run list @old_rl.
 *
 * Check the return value for error with IS_ERR(ret_val). If this is FALSE,
 * the function was successful, the return value is the new run list, and if
 * an existing run list pointer was passed in, this is no longer valid.
 * If IS_ERR(ret_val) returns true, there was an error, the return value is not
 * a run_list pointer and the existing run list pointer if one was passed in
 * has not been touched. In this case use PTR_ERR(ret_val) to obtain the error
 * code. Following error codes are defined:
 * 	-ENOMEM		Not enough memory to allocate run list array.
 * 	-EIO		Corrupt run list.
 * 	-EINVAL		Invalid parameters were passed in.
 * 	-ERANGE		The two run lists overlap.
 *
 * FIXME: For now we take the conceptionally simplest approach of creating the
 * new run list disregarding the already existing one and then splicing the
 * two into one if that is possible (we check for overlap and discard the new
 * run list if overlap present and return error).
 */
run_list_element *decompress_mapping_pairs(const ntfs_volume *vol,
		const ATTR_RECORD *attr, run_list_element *old_rl)
{
	VCN vcn;		/* Current vcn. */
	LCN lcn; 		/* Current lcn. */
	s64 deltaxcn;		/* Change in [vl]cn. */
	run_list_element *rl = NULL;	/* The output run_list. */
	run_list_element *rl2;	/* Temporary run_list. */
	u8 *buf;		/* Current position in mapping pairs array. */
	u8 *attr_end;		/* End of attribute. */
	int rlsize;		/* Size of run_list buffer. */
	int rlpos;		/* Current run_list position. */
	u8 b;			/* Current byte offset in buf. */

#ifdef DEBUG
	/* Make sure attr exists and is non-resident. */
	if (!attr || !attr->non_resident ||
			sle64_to_cpu(attr->_ANR(lowest_vcn)) < (VCN)0) {
		ntfs_error(vol->sb, "Invalid arguments.");
		return ERR_PTR(-EINVAL);
	}
#endif
	/* Start at vcn = lowest_vcn and lcn 0. */
	vcn = sle64_to_cpu(attr->_ANR(lowest_vcn));
	lcn = 0;
	/* Get start of the mapping pairs array. */
	buf = (u8*)attr + le16_to_cpu(attr->_ANR(mapping_pairs_offset));
	attr_end = (u8*)attr + le32_to_cpu(attr->length);
	if (unlikely(buf < (u8*)attr || buf > attr_end)) {
		ntfs_error(vol->sb, "Corrupt attribute.");
		return ERR_PTR(-EIO);
	}
	/* Current position in run_list array. */
	rlpos = 0;
	/* Allocate first page. */
	rl = ntfs_malloc_nofs(PAGE_SIZE);
	if (unlikely(!rl))
		return ERR_PTR(-ENOMEM);
	/* Current run_list buffer size in bytes. */
	rlsize = PAGE_SIZE;
	/* Insert unmapped starting element if necessary. */
	if (vcn) {
		rl->vcn = (VCN)0;
		rl->lcn = (LCN)LCN_RL_NOT_MAPPED;
		rl->length = vcn;
		rlpos++;
	}
	while (buf < attr_end && *buf) {
		/*
		 * Allocate more memory if needed, including space for the
		 * not-mapped and terminator elements. ntfs_malloc_nofs()
		 * operates on whole pages only.
		 */
		if (((rlpos + 3) * sizeof(*old_rl)) > rlsize) {
			rl2 = ntfs_malloc_nofs(rlsize + (int)PAGE_SIZE);
			if (unlikely(!rl2)) {
				ntfs_free(rl);
				return ERR_PTR(-ENOMEM);
			}
			memmove(rl2, rl, rlsize);
			ntfs_free(rl);
			rl = rl2;
			rlsize += PAGE_SIZE;
		}
		/* Enter the current vcn into the current run_list element. */
		(rl + rlpos)->vcn = vcn;
		/*
		 * Get the change in vcn, i.e. the run length in clusters.
		 * Doing it this way ensures that we signextend negative values.
		 * A negative run length doesn't make any sense, but hey, I
		 * didn't make up the NTFS specs and Windows NT4 treats the run
		 * length as a signed value so that's how it is...
		 */
		b = *buf & 0xf;
		if (b) {
			if (unlikely(buf + b > attr_end))
				goto io_error;
			for (deltaxcn = (s8)buf[b--]; b; b--)
				deltaxcn = (deltaxcn << 8) + buf[b];
		} else { /* The length entry is compulsory. */
			ntfs_error(vol->sb, "Missing length entry in mapping "
					"pairs array.");
			deltaxcn = (s64)-1;
		}
		/*
		 * Assume a negative length to indicate data corruption and
		 * hence clean-up and return NULL.
		 */
		if (unlikely(deltaxcn < 0)) {
			ntfs_error(vol->sb, "Invalid length in mapping pairs "
					"array.");
			goto err_out;
		}
		/*
		 * Enter the current run length into the current run_list
		 * element.
		 */
		(rl + rlpos)->length = deltaxcn;
		/* Increment the current vcn by the current run length. */
		vcn += deltaxcn;
		/*
		 * There might be no lcn change at all, as is the case for
		 * sparse clusters on NTFS 3.0+, in which case we set the lcn
		 * to LCN_HOLE.
		 */
		if (!(*buf & 0xf0))
			(rl + rlpos)->lcn = (LCN)LCN_HOLE;
		else {
			/* Get the lcn change which really can be negative. */
			u8 b2 = *buf & 0xf;
			b = b2 + ((*buf >> 4) & 0xf);
			if (buf + b > attr_end)
				goto io_error;
			for (deltaxcn = (s8)buf[b--]; b > b2; b--)
				deltaxcn = (deltaxcn << 8) + buf[b];
			/* Change the current lcn to it's new value. */
			lcn += deltaxcn;
#ifdef DEBUG
			/*
			 * On NTFS 1.2-, apparently can have lcn == -1 to
			 * indicate a hole. But we haven't verified ourselves
			 * whether it is really the lcn or the deltaxcn that is
			 * -1. So if either is found give us a message so we
			 * can investigate it further!
			 */
			if (vol->major_ver < 3) {
				if (unlikely(deltaxcn == (LCN)-1))
					ntfs_error(vol->sb, "lcn delta == -1");
				if (unlikely(lcn == (LCN)-1))
					ntfs_error(vol->sb, "lcn == -1");
			}
#endif
			/* Check lcn is not below -1. */
			if (unlikely(lcn < (LCN)-1)) {
				ntfs_error(vol->sb, "Invalid LCN < -1 in "
						"mapping pairs array.");
				goto err_out;
			}
			/* Enter the current lcn into the run_list element. */
			(rl + rlpos)->lcn = lcn;
		}
		/* Get to the next run_list element. */
		rlpos++;
		/* Increment the buffer position to the next mapping pair. */
		buf += (*buf & 0xf) + ((*buf >> 4) & 0xf) + 1;
	}
	if (unlikely(buf >= attr_end))
		goto io_error;
	/*
	 * If there is a highest_vcn specified, it must be equal to the final
	 * vcn in the run list - 1, or something has gone badly wrong.
	 */
	deltaxcn = sle64_to_cpu(attr->_ANR(highest_vcn));
	if (unlikely(deltaxcn && vcn - 1 != deltaxcn)) {
mpa_err:
		ntfs_error(vol->sb, "Corrupt mapping pairs array in "
				"non-resident attribute.");
		goto err_out;
	}
	/* Setup not mapped run_list element if this is the base extent. */
	if (!attr->_ANR(lowest_vcn)) {
		VCN max_cluster;

		max_cluster = (sle64_to_cpu(attr->_ANR(allocated_size)) +
				vol->cluster_size - 1) >>
				vol->cluster_size_bits;
		/*
		 * If there is a difference between the highest_vcn and the
		 * highest cluster, the run list is either corrupt or, more
		 * likely, there are more extents following this one.
		 */
		if (deltaxcn < --max_cluster) {
			//RAR ntfs_debug("More extents to follow; deltaxcn = 0x%Lx, "
					//RAR "max_cluster = 0x%Lx",
					//RAR (long long)deltaxcn,
					//RAR (long long)max_cluster);
			(rl + rlpos)->vcn = vcn;
			vcn += (rl + rlpos)->length = max_cluster - deltaxcn;
			(rl + rlpos)->lcn = (LCN)LCN_RL_NOT_MAPPED;
			rlpos++;
		} else if (unlikely(deltaxcn > max_cluster)) {
			ntfs_error(vol->sb, "Corrupt attribute. deltaxcn = "
					"0x%Lx, max_cluster = 0x%Lx",
					(long long)deltaxcn,
					(long long)max_cluster);
			goto mpa_err;
		}
		(rl + rlpos)->lcn = (LCN)LCN_ENOENT;
	} else /* Not the base extent. There may be more extents to follow. */
		(rl + rlpos)->lcn = (LCN)LCN_RL_NOT_MAPPED;

	/* Setup terminating run_list element. */
	(rl + rlpos)->vcn = vcn;
	(rl + rlpos)->length = (s64)0;
	//RAR ntfs_debug("Mapping pairs array successfully decompressed.");
	//RAR ntfs_debug_dump_runlist(rl);
	/* If no existing run list was specified, we are done. */
	if (!old_rl)
		return rl;
	/* Now combine the new and old run lists checking for overlaps. */
	rl2 = merge_run_lists(old_rl, rl);
	if (likely(!IS_ERR(rl2)))
		return rl2;
	ntfs_free(rl);
	ntfs_error(vol->sb, "Failed to merge run lists.");
	return rl2;
io_error:
	ntfs_error(vol->sb, "Corrupt attribute.");
err_out:
	ntfs_free(rl);
	return ERR_PTR(-EIO);
}

/**
 * map_run_list - map (a part of) a run list of an ntfs inode
 * @ni:		ntfs inode for which to map (part of) a run list 
 * @vcn:	map run list part containing this vcn
 *
 * Map the part of a run list containing the @vcn of an the ntfs inode @ni.
 *
 * Return 0 on success and -errno on error.
 */
int map_run_list(ntfs_inode *ni, VCN vcn)
{
	attr_search_context *ctx;
	MFT_RECORD *mrec;
	const uchar_t *name;
	u32 name_len;
	ATTR_TYPES at;
	int err = 0;
	
	ntfs_debug("Mapping run list part containing vcn 0x%Lx.",
			(long long)vcn);

	/* Map, pin and lock the mft record for reading. */
	mrec = map_mft_record(READ, ni);
	if (IS_ERR(mrec))
		return PTR_ERR(mrec);

	ctx = get_attr_search_ctx(ni, mrec);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	/* The attribute type is determined from the inode type. */
	if (S_ISDIR(VFS_I(ni)->i_mode)) {
		at = AT_INDEX_ALLOCATION;
		name = I30;
		name_len = 4;
	} else {
		at = AT_DATA;
		name = NULL;
		name_len = 0;
	}

	/* Find the attribute in the mft record. */
	if (!lookup_attr(at, name, name_len, CASE_SENSITIVE, vcn, NULL, 0,
			ctx)) {
		put_attr_search_ctx(ctx);
		err = -ENOENT;
		goto unm_err_out;
	}

	/* Lock the run list. */
	down_write(&ni->run_list.lock);

	/* Make sure someone else didn't do the work while we were spinning. */
	if (likely(vcn_to_lcn(ni->run_list.rl, vcn) <= LCN_RL_NOT_MAPPED)) {
		run_list_element *rl;

		/* Decode the run list. */
		rl = decompress_mapping_pairs(ni->vol, ctx->attr,
				ni->run_list.rl);

		/* Flag any errors or set the run list if successful. */
		if (unlikely(IS_ERR(rl)))
			err = PTR_ERR(rl);
		else
			ni->run_list.rl = rl;
	}

	/* Unlock the run list. */
	up_write(&ni->run_list.lock);
	
	put_attr_search_ctx(ctx);

	/* Unlock, unpin and release the mft record. */
	unmap_mft_record(READ, ni);

	/* If an error occured, return it. */
	ntfs_debug("Done.");
	return err;

unm_err_out:
	unmap_mft_record(READ, ni);
	return err;
}

/**
 * vcn_to_lcn - convert a vcn into a lcn given a run list
 * @rl:		run list to use for conversion
 * @vcn:	vcn to convert
 *
 * Convert the virtual cluster number @vcn of an attribute into a logical
 * cluster number (lcn) of a device using the run list @rl to map vcns to their
 * corresponding lcns.
 *
 * It is up to the caller to serialize access to the run list @rl.
 *
 * Since lcns must be >= 0, we use negative return values with special meaning:
 *
 * Return value			Meaning / Description
 * ==================================================
 *  -1 = LCN_HOLE		Hole / not allocated on disk.
 *  -2 = LCN_RL_NOT_MAPPED	This is part of the run list which has not been
 *				inserted into the run list yet.
 *  -3 = LCN_ENOENT		There is no such vcn in the attribute.
 *  -4 = LCN_EINVAL		Input parameter error (if debug enabled).
 */
LCN vcn_to_lcn(const run_list_element *rl, const VCN vcn)
{
	int i;

#ifdef DEBUG
	if (vcn < (VCN)0)
		return (LCN)LCN_EINVAL;
#endif
	/*
	 * If rl is NULL, assume that we have found an unmapped run list. The
	 * caller can then attempt to map it and fail appropriately if
	 * necessary.
	 */
	if (unlikely(!rl))
		return (LCN)LCN_RL_NOT_MAPPED;

	/* Catch out of lower bounds vcn. */
	if (unlikely(vcn < rl[0].vcn))
		return (LCN)LCN_ENOENT;

	for (i = 0; likely(rl[i].length); i++) {
		if (unlikely(vcn < rl[i+1].vcn)) {
			if (likely(rl[i].lcn >= (LCN)0))
				return rl[i].lcn + (vcn - rl[i].vcn);
			return rl[i].lcn;
		}
	}
	/*
	 * The terminator element is setup to the correct value, i.e. one of
	 * LCN_HOLE, LCN_RL_NOT_MAPPED, or LCN_ENOENT.
	 */
	if (likely(rl[i].lcn < (LCN)0))
		return rl[i].lcn;
	/* Just in case... We could replace this with BUG() some day. */
	return (LCN)LCN_ENOENT;
}

/**
 * find_attr - find (next) attribute in mft record
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * You shouldn't need to call this function directly. Use lookup_attr() instead.
 *
 * find_attr() takes a search context @ctx as parameter and searches the mft
 * record specified by @ctx->mrec, beginning at @ctx->attr, for an attribute of
 * @type, optionally @name and @val. If found, find_attr() returns TRUE and
 * @ctx->attr will point to the found attribute. If not found, find_attr()
 * returns FALSE and @ctx->attr is undefined (i.e. do not rely on it not
 * changing).
 *
 * If @ctx->is_first is TRUE, the search begins with @ctx->attr itself. If it
 * is FALSE, the search begins after @ctx->attr.
 *
 * If @ic is IGNORE_CASE, the @name comparisson is not case sensitive and
 * @ctx->ntfs_ino must be set to the ntfs inode to which the mft record
 * @ctx->mrec belongs. This is so we can get at the ntfs volume and hence at
 * the upcase table. If @ic is CASE_SENSITIVE, the comparison is case
 * sensitive. When @name is present, @name_len is the @name length in Unicode
 * characters.
 *
 * If @name is not present (NULL), we assume that the unnamed attribute is
 * being searched for.
 *
 * Finally, the resident attribute value @val is looked for, if present. If @val
 * is not present (NULL), @val_len is ignored.
 *
 * find_attr() only searches the specified mft record and it ignores the
 * presence of an attribute list attribute (unless it is the one being searched
 * for, obviously). If you need to take attribute lists into consideration, use
 * lookup_attr() instead (see below). This also means that you cannot use
 * find_attr() to search for extent records of non-resident attributes, as
 * extents with lowest_vcn != 0 are usually described by the attribute list
 * attribute only. - Note that it is possible that the first extent is only in
 * the attribute list while the last extent is in the base mft record, so don't
 * rely on being able to find the first extent in the base mft record.
 *
 * Warning: Never use @val when looking for attribute types which can be
 *	    non-resident as this most likely will result in a crash!
 */
BOOL find_attr(const ATTR_TYPES type, const uchar_t *name, const u32 name_len,
		const IGNORE_CASE_BOOL ic, const u8 *val, const u32 val_len,
		attr_search_context *ctx)
{
	ATTR_RECORD *a;
	ntfs_volume *vol;
	uchar_t *upcase;
	u32 upcase_len;

	if (ic == IGNORE_CASE) {
		vol = ctx->ntfs_ino->vol;
		upcase = vol->upcase;
		upcase_len = vol->upcase_len;
	} else {
		vol = NULL;
		upcase = NULL;
		upcase_len = 0;
	}
	/*
	 * Iterate over attributes in mft record starting at @ctx->attr, or the
	 * attribute following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		a = ctx->attr;
		ctx->is_first = FALSE;
	} else
		a = (ATTR_RECORD*)((u8*)ctx->attr +
				le32_to_cpu(ctx->attr->length));
	for (;;	a = (ATTR_RECORD*)((u8*)a + le32_to_cpu(a->length))) {
		if ((u8*)a < (u8*)ctx->mrec || (u8*)a > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_allocated))
			break;
		ctx->attr = a;
		/* We catch $END with this more general check, too... */
		if (le32_to_cpu(a->type) > le32_to_cpu(type))
			return FALSE;
		if (unlikely(!a->length))
			break;
		if (a->type != type)
			continue;
		/* 
		 * If @name is present, compare the two names. If @name is
		 * missing, assume we want an unnamed attribute.
		 */
		if (!name) {
			/* The search failed if the found attribute is named. */
			if (a->name_length)
				return FALSE;
		} else if (!ntfs_are_names_equal(name, name_len,
			    (uchar_t*)((u8*)a + le16_to_cpu(a->name_offset)),
			    a->name_length, ic, upcase, upcase_len)) {
			register int rc;
			
			rc = ntfs_collate_names(name, name_len,
					(uchar_t*)((u8*)a +
						le16_to_cpu(a->name_offset)),
					a->name_length, 1, IGNORE_CASE,
					upcase, upcase_len);
			/*
			 * If @name collates before a->name, there is no
			 * matching attribute.
			 */
			if (rc == -1)
				return FALSE;
			/* If the strings are not equal, continue search. */
			if (rc)
	 			continue;
			rc = ntfs_collate_names(name, name_len,
					(uchar_t*)((u8*)a +
						le16_to_cpu(a->name_offset)),
					a->name_length, 1, CASE_SENSITIVE,
					upcase, upcase_len);
			if (rc == -1)
				return FALSE;
			if (rc)
				continue;
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed. If no @val specified, we have found the attribute
		 * and are done.
		 */
		if (!val)
			return TRUE;
		/* @val is present; compare values. */
		else {
			register int rc;
			
			rc = memcmp(val, (u8*)a +le16_to_cpu(a->_ARA(value_offset)),
				min(val_len, le32_to_cpu(a->_ARA(value_length))));
			/*
			 * If @val collates before the current attribute's
			 * value, there is no matching attribute.
			 */
			if (!rc) {
				register u32 avl;
				avl = le32_to_cpu(a->_ARA(value_length));
				if (val_len == avl)
					return TRUE;
				if (val_len < avl)
					return FALSE;
			} else if (rc < 0)
				return FALSE;
		}
	}
	ntfs_error(NULL, "Inode is corrupt. Run chkdsk.");
	return FALSE;
}

/**
 * load_attribute_list - load an attribute list into memory
 * @vol:		ntfs volume from which to read
 * @run_list:		run list of the attribute list
 * @al:			destination buffer
 * @size:		size of the destination buffer in bytes
 * @initialized_size:	initialized size of the attribute list
 *
 * Walk the run list @run_list and load all clusters from it copying them into
 * the linear buffer @al. The maximum number of bytes copied to @al is @size
 * bytes. Note, @size does not need to be a multiple of the cluster size. If
 * @initialized_size is less than @size, the region in @al between
 * @initialized_size and @size will be zeroed and not read from disk.
 *
 * Return 0 on success or -errno on error.
 */
int load_attribute_list(ntfs_volume *vol, run_list *run_list, u8 *al,
		const s64 size, const s64 initialized_size)
{
	LCN lcn;
	u8 *al_end = al + initialized_size;
	run_list_element *rl;
	struct buffer_head *bh;
	struct super_block *sb = vol->sb;
	unsigned long block_size = sb->s_blocksize;
	unsigned long block, max_block;
	int err = 0;
	unsigned char block_size_bits = sb->s_blocksize_bits;

	ntfs_debug("Entering.");
#ifdef DEBUG
	if (!vol || !run_list || !al || size <= 0 || initialized_size < 0 ||
			initialized_size > size)
		return -EINVAL;
#endif
	if (!initialized_size) {
		memset(al, 0, size);
		return 0;
	}
	down_read(&run_list->lock);
	rl = run_list->rl;
	/* Read all clusters specified by the run list one run at a time. */
	while (rl->length) {
		lcn = vcn_to_lcn(rl, rl->vcn);
		ntfs_debug("Reading vcn = 0x%Lx, lcn = 0x%Lx.",
				(long long)rl->vcn, (long long)lcn);
		/* The attribute list cannot be sparse. */
		if (lcn < 0) {
			ntfs_error(sb, "vcn_to_lcn() failed. Cannot read "
					"attribute list.");
			goto err_out;
		}
		block = lcn << vol->cluster_size_bits >> block_size_bits;
		/* Read the run from device in chunks of block_size bytes. */
		max_block = block + (rl->length << vol->cluster_size_bits >>
				block_size_bits);
		ntfs_debug("max_block = 0x%lx.", max_block);
		do {
			ntfs_debug("Reading block = 0x%lx.", block);
			bh = sb_bread(sb, block);
			if (!bh) {
				ntfs_error(sb, "sb_bread() failed. Cannot "
						"read attribute list.");
				goto err_out;
			}
			if (al + block_size > al_end)
				goto do_partial;
			memcpy(al, bh->b_data, block_size);
			brelse(bh);
			al += block_size;
		} while (++block < max_block);
		rl++;
	}
	if (initialized_size < size) {
initialize:
		memset(al + initialized_size, 0, size - initialized_size);
	}
done:
	up_read(&run_list->lock);
	return err;
do_partial:
	if (al < al_end) {
		/* Partial block. */
		memcpy(al, bh->b_data, al_end - al);
		brelse(bh);
		/*
		 * Skip sanity checking if initialized_size < size as it is
		 * too much trouble.
		 */
		if (initialized_size < size)
			goto initialize;
		/* If the final lcn is partial all is fine. */
		if (((s64)(block - (lcn << vol->cluster_size_bits >>
				block_size_bits)) << block_size_bits >>
				vol->cluster_size_bits) == rl->length - 1) {
			if (!rl[1].length || (rl[1].lcn == LCN_RL_NOT_MAPPED
					&& !rl[2].length))
				goto done;
		}
	} else
		brelse(bh);
	/* Real overflow! */
	ntfs_error(sb, "Attribute list buffer overflow. Read attribute list "
			"is truncated.");
err_out:
	err = -EIO;
	goto done;
}

/**
 * find_external_attr - find an attribute in the attribute list of an ntfs inode
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @lowest_vcn:	lowest vcn to find (optional, non-resident attributes only)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * You shouldn't need to call this function directly. Use lookup_attr() instead.
 *
 * Find an attribute by searching the attribute list for the corresponding
 * attribute list entry. Having found the entry, map the mft record for read
 * if the attribute is in a different mft record/inode, find_attr the attribute
 * in there and return it.
 *
 * On first search @ctx->ntfs_ino must be the base mft record and @ctx must
 * have been obtained from a call to get_attr_search_ctx(). On subsequent calls
 * @ctx->ntfs_ino can be any extent inode, too (@ctx->base_ntfs_ino is then the
 * base inode).
 *
 * After finishing with the attribute/mft record you need to call
 * release_attr_search_ctx() to cleanup the search context (unmapping any
 * mapped inodes, etc).
 *
 * Return TRUE if the search was successful and FALSE if not. When TRUE,
 * @ctx->attr is the found attribute and it is in mft record @ctx->mrec. When
 * FALSE, @ctx->attr is the attribute which collates just after the attribute
 * being searched for in the base ntfs inode, i.e. if one wants to add the
 * attribute to the mft record this is the correct place to insert it into
 * and if there is not enough space, the attribute should be placed in an
 * extent mft record.
 */
static BOOL find_external_attr(const ATTR_TYPES type, const uchar_t *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		attr_search_context *ctx)
{
	ntfs_inode *base_ni, *ni;
	ntfs_volume *vol;
	ATTR_LIST_ENTRY *al_entry, *next_al_entry;
	u8 *al_start, *al_end;
	ATTR_RECORD *a;
	uchar_t *al_name;
	u32 al_name_len;

	ni = ctx->ntfs_ino;
	base_ni = ctx->base_ntfs_ino;
	ntfs_debug("Entering for inode 0x%Lx, type 0x%x.",
			(unsigned long long)ni->mft_no, type);
	if (!base_ni) {
		/* First call happens with the base mft record. */
		base_ni = ctx->base_ntfs_ino = ctx->ntfs_ino;
		ctx->base_mrec = ctx->mrec;
	}
	if (ni == base_ni)
		ctx->base_attr = ctx->attr;
	vol = base_ni->vol;
	al_start = base_ni->attr_list;
	al_end = al_start + base_ni->attr_list_size;
	if (!ctx->al_entry)
		ctx->al_entry = (ATTR_LIST_ENTRY*)al_start;
	/*
	 * Iterate over entries in attribute list starting at @ctx->al_entry,
	 * or the entry following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		al_entry = ctx->al_entry;
		ctx->is_first = FALSE;
	} else
		al_entry = (ATTR_LIST_ENTRY*)((u8*)ctx->al_entry +
				le16_to_cpu(ctx->al_entry->length));
	for (;; al_entry = next_al_entry) {
		/* Out of bounds check. */
		if ((u8*)al_entry < base_ni->attr_list ||
				(u8*)al_entry > al_end)
			break;	/* Inode is corrupt. */
		ctx->al_entry = al_entry;
		/* Catch the end of the attribute list. */
		if ((u8*)al_entry == al_end)
			goto not_found;
		if (!al_entry->length)
			break;
		if ((u8*)al_entry + 6 > al_end || (u8*)al_entry +
				le16_to_cpu(al_entry->length) > al_end)
			break;
		next_al_entry = (ATTR_LIST_ENTRY*)((u8*)al_entry +
				le16_to_cpu(al_entry->length));
		if (le32_to_cpu(al_entry->type) > le32_to_cpu(type))
			goto not_found;
		if (type != al_entry->type)
			continue;
		/*
		 * If @name is present, compare the two names. If @name is
		 * missing, assume we want an unnamed attribute.
		 */
		al_name_len = al_entry->name_length;
		al_name = (uchar_t*)((u8*)al_entry + al_entry->name_offset);
		if (!name) {
			if (al_name_len)
				goto not_found;
		} else if (!ntfs_are_names_equal(al_name, al_name_len, name,
				name_len, ic, vol->upcase, vol->upcase_len)) {
			register int rc;

			rc = ntfs_collate_names(name, name_len, al_name,
					al_name_len, 1, IGNORE_CASE,
					vol->upcase, vol->upcase_len);
			/*
			 * If @name collates before al_name, there is no
			 * matching attribute.
			 */
			if (rc == -1)
				goto not_found;
			/* If the strings are not equal, continue search. */
			if (rc)
				continue;
			/*
			 * FIXME: Reverse engineering showed 0, IGNORE_CASE but
			 * that is inconsistent with find_attr(). The subsequent
			 * rc checks were also different. Perhaps I made a
			 * mistake in one of the two. Need to recheck which is
			 * correct or at least see what is going on... (AIA)
			 */
			rc = ntfs_collate_names(name, name_len, al_name,
					al_name_len, 1, CASE_SENSITIVE,
					vol->upcase, vol->upcase_len);
			if (rc == -1)
				goto not_found;
			if (rc)
				continue;
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed. Now check @lowest_vcn. Continue search if the
		 * next attribute list entry still fits @lowest_vcn. Otherwise
		 * we have reached the right one or the search has failed.
		 */
		if (lowest_vcn && (u8*)next_al_entry >= al_start	    &&
				(u8*)next_al_entry + 6 < al_end		    &&
				(u8*)next_al_entry + le16_to_cpu(
					next_al_entry->length) <= al_end    &&	
				sle64_to_cpu(next_al_entry->lowest_vcn) <=
					sle64_to_cpu(lowest_vcn)	    &&
				next_al_entry->type == al_entry->type	    &&
				next_al_entry->name_length == al_name_len   &&
				ntfs_are_names_equal((uchar_t*)((u8*)
					next_al_entry +
					next_al_entry->name_offset),
					next_al_entry->name_length,
					al_name, al_name_len, CASE_SENSITIVE,
					vol->upcase, vol->upcase_len))
			continue;
		if (MREF_LE(al_entry->mft_reference) == ni->mft_no) {
			if (MSEQNO_LE(al_entry->mft_reference) != ni->seq_no) {
				ntfs_error(vol->sb, "Found stale mft "
						"reference in attribute list!");
				break;
			}
		} else { /* Mft references do not match. */
			/* If there is a mapped record unmap it first. */
			if (ni != base_ni)
				unmap_extent_mft_record(ni);
			/* Do we want the base record back? */
			if (MREF_LE(al_entry->mft_reference) ==
					base_ni->mft_no) {
				ni = ctx->ntfs_ino = base_ni;
				ctx->mrec = ctx->base_mrec;
			} else {
				/* We want an extent record. */
				ctx->mrec = map_extent_mft_record(base_ni,
						al_entry->mft_reference, &ni);
				ctx->ntfs_ino = ni;
				if (IS_ERR(ctx->mrec)) {
					ntfs_error(vol->sb, "Failed to map mft "
							"record, error code "
							"%ld.",
							-PTR_ERR(ctx->mrec));
					break;
				}
			}
			ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
					le16_to_cpu(ctx->mrec->attrs_offset));
		}
		/*
		 * ctx->vfs_ino, ctx->mrec, and ctx->attr now point to the
		 * mft record containing the attribute represented by the
		 * current al_entry.
		 */
		/*
		 * We could call into find_attr() to find the right attribute
		 * in this mft record but this would be less efficient and not
		 * quite accurate as find_attr() ignores the attribute instance
		 * numbers for example which become important when one plays
		 * with attribute lists. Also, because a proper match has been
		 * found in the attribute list entry above, the comparison can
		 * now be optimized. So it is worth re-implementing a
		 * simplified find_attr() here.
		 */
		a = ctx->attr;
		/*
		 * Use a manual loop so we can still use break and continue
		 * with the same meanings as above.
		 */
do_next_attr_loop:
		if ((u8*)a < (u8*)ctx->mrec || (u8*)a > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_allocated))
			break;
		if (a->type == AT_END)
			continue;
		if (!a->length)
			break;
		if (al_entry->instance != a->instance)
			goto do_next_attr;
		if (al_entry->type != a->type)
			continue;
		if (name) {
			if (a->name_length != al_name_len)
				continue;
			if (!ntfs_are_names_equal((uchar_t*)((u8*)a +
					le16_to_cpu(a->name_offset)),
					a->name_length, al_name, al_name_len,
					CASE_SENSITIVE, vol->upcase,
					vol->upcase_len))
				continue;
		}
		ctx->attr = a;
		/*
		 * If no @val specified or @val specified and it matches, we
		 * have found it!
		 */
		if (!val || (!a->non_resident && le32_to_cpu(a->_ARA(value_length))
				== val_len && !memcmp((u8*)a +
				le16_to_cpu(a->_ARA(value_offset)), val, val_len))) {
			ntfs_debug("Done, found.");
			return TRUE;
		}
do_next_attr:
		/* Proceed to the next attribute in the current mft record. */
		a = (ATTR_RECORD*)((u8*)a + le32_to_cpu(a->length));
		goto do_next_attr_loop;
	}
	ntfs_error(base_ni->vol->sb, "Inode contains corrupt attribute list "
			"attribute.\n");
	if (ni != base_ni) {
		unmap_extent_mft_record(ni);
		ctx->ntfs_ino = base_ni;
		ctx->mrec = ctx->base_mrec;
		ctx->attr = ctx->base_attr;
	}
	/*
	 * FIXME: We absolutely have to return ERROR status instead of just
	 * false or we will blow up or even worse cause corruption when we add
	 * write support and we reach this code path!
	 */
	printk(KERN_CRIT "NTFS: FIXME: Hit unfinished error code path!!!\n");
	return FALSE;
not_found:
	/*
	 * Seek to the end of the base mft record, i.e. when we return false,
	 * ctx->mrec and ctx->attr indicate where the attribute should be
	 * inserted into the attribute record.
	 * And of course ctx->al_entry points to the end of the attribute
	 * list inside NTFS_I(ctx->base_vfs_ino)->attr_list.
	 *
	 * FIXME: Do we really want to do this here? Think about it... (AIA)
	 */
	reinit_attr_search_ctx(ctx);
	find_attr(type, name, name_len, ic, val, val_len, ctx);
	ntfs_debug("Done, not found.");
	return FALSE;
}

/**
 * lookup_attr - find an attribute in an ntfs inode
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @lowest_vcn:	lowest vcn to find (optional, non-resident attributes only)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * Find an attribute in an ntfs inode. On first search @ctx->ntfs_ino must
 * be the base mft record and @ctx must have been obtained from a call to
 * get_attr_search_ctx().
 *
 * This function transparently handles attribute lists and @ctx is used to
 * continue searches where they were left off at.
 *
 * After finishing with the attribute/mft record you need to call
 * release_attr_search_ctx() to cleanup the search context (unmapping any
 * mapped inodes, etc).
 *
 * Return TRUE if the search was successful and FALSE if not. When TRUE,
 * @ctx->attr is the found attribute and it is in mft record @ctx->mrec. When
 * FALSE, @ctx->attr is the attribute which collates just after the attribute
 * being searched for, i.e. if one wants to add the attribute to the mft
 * record this is the correct place to insert it into.
 */
BOOL lookup_attr(const ATTR_TYPES type, const uchar_t *name, const u32 name_len,
		const IGNORE_CASE_BOOL ic, const VCN lowest_vcn, const u8 *val,
		const u32 val_len, attr_search_context *ctx)
{
	ntfs_inode *base_ni;

	ntfs_debug("Entering.");
	if (ctx->base_ntfs_ino)
		base_ni = ctx->base_ntfs_ino;
	else
		base_ni = ctx->ntfs_ino;
	/* Sanity check, just for debugging really. */
	BUG_ON(!base_ni);
	if (!NInoAttrList(base_ni))
		return find_attr(type, name, name_len, ic, val, val_len, ctx);
	return find_external_attr(type, name, name_len, ic, lowest_vcn, val,
			val_len, ctx);
}

/**
 * init_attr_search_ctx - initialize an attribute search context
 * @ctx:	attribute search context to initialize
 * @ni:		ntfs inode with which to initialize the search context
 * @mrec:	mft record with which to initialize the search context
 *
 * Initialize the attribute search context @ctx with @ni and @mrec.
 */
static inline void init_attr_search_ctx(attr_search_context *ctx,
		ntfs_inode *ni, MFT_RECORD *mrec)
{
	ctx->mrec = mrec;
	/* Sanity checks are performed elsewhere. */
	ctx->attr = (ATTR_RECORD*)((u8*)mrec + le16_to_cpu(mrec->attrs_offset));
	ctx->is_first = TRUE;
	ctx->ntfs_ino = ni;
	ctx->al_entry = NULL;
	ctx->base_ntfs_ino = NULL;
	ctx->base_mrec = NULL;
	ctx->base_attr = NULL;
}

/**
 * reinit_attr_search_ctx - reinitialize an attribute search context
 * @ctx:	attribute search context to reinitialize
 *
 * Reinitialize the attribute search context @ctx, unmapping an associated
 * extent mft record if present, and initialize the search context again.
 *
 * This is used when a search for a new attribute is being started to reset
 * the search context to the beginning.
 */
void reinit_attr_search_ctx(attr_search_context *ctx)
{
	if (likely(!ctx->base_ntfs_ino)) {
		/* No attribute list. */
		ctx->is_first = TRUE;
		/* Sanity checks are performed elsewhere. */
		ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
				le16_to_cpu(ctx->mrec->attrs_offset));
		return;
	} /* Attribute list. */
	if (ctx->ntfs_ino != ctx->base_ntfs_ino)
		unmap_mft_record(READ, ctx->ntfs_ino);
	init_attr_search_ctx(ctx, ctx->base_ntfs_ino, ctx->base_mrec);
	return;
}

/**
 * get_attr_search_ctx - allocate and initialize a new attribute search context
 * @ni:		ntfs inode with which to initialize the search context
 * @mrec:	mft record with which to initialize the search context
 *
 * Allocate a new attribute search context, initialize it with @ni and @mrec,
 * and return it. Return NULL if allocation failed.
 */
attr_search_context *get_attr_search_ctx(ntfs_inode *ni, MFT_RECORD *mrec)
{
	attr_search_context *ctx;

	ctx = kmem_cache_alloc(ntfs_attr_ctx_cache, SLAB_NOFS);
	if (ctx)
		init_attr_search_ctx(ctx, ni, mrec);
	return ctx;
}

/**
 * put_attr_search_ctx - release an attribute search context
 * @ctx:	attribute search context to free
 *
 * Release the attribute search context @ctx, unmapping an associated extent
 * mft record if present.
 */
void put_attr_search_ctx(attr_search_context *ctx)
{
	if (ctx->base_ntfs_ino && ctx->ntfs_ino != ctx->base_ntfs_ino)
		unmap_mft_record(READ, ctx->ntfs_ino);
	kmem_cache_free(ntfs_attr_ctx_cache, ctx);
	return;
}

