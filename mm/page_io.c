/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 *  Always use brw_page, life becomes simpler. 12 May 1998 Eric Biederman
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/locks.h>
#include <linux/swapctl.h>

#include <asm/pgtable.h>

/*
 * Reads or writes a swap page.
 * wait=1: start I/O and wait for completion. wait=0: start asynchronous I/O.
 *
 * Important prevention of race condition: the caller *must* atomically 
 * create a unique swap cache entry for this swap page before calling
 * rw_swap_page, and must lock that page.  By ensuring that there is a
 * single page of memory reserved for the swap entry, the normal VM page
 * lock on that page also doubles as a lock on swap entries.  Having only
 * one lock to deal with per swap entry (rather than locking swap and memory
 * independently) also makes it easier to make certain swapping operations
 * atomic, which is particularly important when we are trying to ensure 
 * that shared pages stay shared while being swapped.
 */

static int rw_swap_page_base(int rw, swp_entry_t entry, struct page *page)
{
	unsigned long offset;
	sector_t zones[PAGE_SIZE/512];
	int zones_used;
	int block_size;
	struct inode *swapf = 0;
	struct block_device *bdev;

	if (rw == READ) {
		ClearPageUptodate(page);
		kstat.pswpin++;
	} else
		kstat.pswpout++;

	get_swaphandle_info(entry, &offset, &swapf);
	bdev = swapf->i_bdev;
	if (bdev) {
		zones[0] = offset;
		zones_used = 1;
		block_size = PAGE_SIZE;
	} else {
		int i, j;
		unsigned int block = offset
			<< (PAGE_SHIFT - swapf->i_sb->s_blocksize_bits);

		block_size = swapf->i_sb->s_blocksize;
		for (i=0, j=0; j< PAGE_SIZE ; i++, j += block_size)
			if (!(zones[i] = bmap(swapf,block++))) {
				printk("rw_swap_page: bad swap file\n");
				return 0;
			}
		zones_used = i;
		bdev = swapf->i_sb->s_bdev;
	}

 	/* block_size == PAGE_SIZE/zones_used */
 	brw_page(rw, page, bdev, zones, block_size);

 	/* Note! For consistency we do all of the logic,
 	 * decrementing the page count, and unlocking the page in the
 	 * swap lock map - in the IO completion handler.
 	 */
	return 1;
}

/*
 * A simple wrapper so the base function doesn't need to enforce
 * that all swap pages go through the swap cache! We verify that:
 *  - the page is locked
 *  - it's marked as being swap-cache
 *  - it's associated with the swap inode
 */
void rw_swap_page(int rw, struct page *page)
{
	swp_entry_t entry;

	entry.val = page->index;

	if (!PageLocked(page))
		PAGE_BUG(page);
	if (!PageSwapCache(page))
		PAGE_BUG(page);
	if (!rw_swap_page_base(rw, entry, page))
		unlock_page(page);
}

/*
 * The swap lock map insists that pages be in the page cache!
 * Therefore we can't use it.  Later when we can remove the need for the
 * lock map and we can reduce the number of functions exported.
 */
void rw_swap_page_nolock(int rw, swp_entry_t entry, char *buf)
{
	struct page *page = virt_to_page(buf);
	
	if (!PageLocked(page))
		PAGE_BUG(page);
	if (page->mapping)
		PAGE_BUG(page);
	/* needs sync_page to wait I/O completation */
	page->mapping = &swapper_space;
	if (!rw_swap_page_base(rw, entry, page))
		unlock_page(page);
	if (rw == WRITE)
		wait_on_page_writeback(page);
	else
		wait_on_page_locked(page);
	page->mapping = NULL;
}