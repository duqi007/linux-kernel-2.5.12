/*
 *  linux/arch/arm/mm/proc-arm720.S: MMU functions for ARM720
 *
 *  Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *                     Rob Scott (rscott@mtrob.fdns.net)
 *  Copyright (C) 2000 ARM Limited, Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the ARM720T.  The ARM720T has a writethrough IDC
 * cache, so we don't need to clean it.
 *
 *  Changelog:
 *   05-09-2000 SJH	Created by moving 720 specific functions
 *			out of 'proc-arm6,7.S' per RMK discussion
 *   07-25-2000 SJH	Added idle function.
 *   08-25-2000	DBS	Updated for integration of ARM Ltd version.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>

/*
 * Function: arm720_cache_clean_invalidate_all (void)
 *	   : arm720_cache_clean_invalidate_page (unsigned long address, int size,
 *                                    int flags)
 *
 * Params  : address	Area start address
 *	   : size	size of area
 *	   : flags	b0 = I cache as well
 *
 * Purpose : Flush all cache lines
 */
ENTRY(cpu_arm720_cache_clean_invalidate_all)
ENTRY(cpu_arm720_cache_clean_invalidate_range)
ENTRY(cpu_arm720_icache_invalidate_range)
ENTRY(cpu_arm720_icache_invalidate_page)
ENTRY(cpu_arm720_dcache_invalidate_range)
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ flush cache
		mov	pc, lr

/*
 * These just expect cache lines to be cleaned.  Since we have a writethrough
 * cache, we never have any dirty cachelines to worry about.
 */
ENTRY(cpu_arm720_dcache_clean_range)
ENTRY(cpu_arm720_dcache_clean_page)
ENTRY(cpu_arm720_dcache_clean_entry)
		mov	pc, lr

/*
 * Function: arm720_check_bugs (void)
 *	   : arm720_proc_init (void)
 *	   : arm720_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm720_check_bugs)
		mrs	ip, cpsr
		bic	ip, ip, #PSR_F_BIT
		msr	cpsr, ip
		mov	pc, lr

ENTRY(cpu_arm720_proc_init)
		mov	pc, lr

ENTRY(cpu_arm720_proc_fin)
		stmfd	sp!, {lr}
		mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, ip
		mrc	p15, 0, r0, c1, c0, 0
		bic	r0, r0, #0x1000			@ ...i............
		bic	r0, r0, #0x000e			@ ............wca.
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
		ldmfd	sp!, {pc}

/*
 * Function: arm720_proc_do_idle(void)
 * Params  : r0 = unused
 * Purpose : put the processer in proper idle mode
 */
ENTRY(cpu_arm720_do_idle)
		mov	pc, lr

/*
 * Function: arm720_set_pgd(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old process' state and restoring
 *	     the new.
 */
ENTRY(cpu_arm720_set_pgd)
		mov	r1, #0
		mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
		mcr	p15, 0, r0, c2, c0, 0		@ update page table ptr
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_set_pmd ()
 *
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 *
 * Purpose : Set a PMD and flush it out of any WB cache
 */
ENTRY(cpu_arm720_set_pmd)
		tst	r1, #3
		orrne	r1, r1, #16			@ Updatable bit is
		str	r1, [r0]			@ always set on ARM720
		mov	pc, lr

/*
 * Function: arm720_set_pte(pte_t *ptep, pte_t pte)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out of any WB cache
 */
		.align	5
ENTRY(cpu_arm720_set_pte)
		tst	r0, #2048
		streq	r0, [r0, -r0]			@ BUG_ON
		str	r1, [r0], #-2048		@ linux version

		eor	r1, r1, #LPTE_PRESENT | LPTE_YOUNG | LPTE_WRITE | LPTE_DIRTY

		bic	r2, r1, #0xff0
		bic	r2, r2, #3
		orr	r2, r2, #HPTE_TYPE_SMALL

		tst	r1, #LPTE_USER | LPTE_EXEC	@ User or Exec?
		orrne	r2, r2, #HPTE_AP_READ

		tst	r1, #LPTE_WRITE | LPTE_DIRTY	@ Write and Dirty?
		orreq	r2, r2, #HPTE_AP_WRITE

		tst	r1, #LPTE_PRESENT | LPTE_YOUNG	@ Present and Young
		movne	r2, #0

		str	r2, [r0]			@ hardware version
		mov	pc, lr

/*
 * Function: arm720_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm720_reset)
		mov	ip, #0
		mcr	p15, 0, ip, c7, c7, 0		@ invalidate cache
		mcr	p15, 0, ip, c8, c7, 0		@ flush TLB (v4)
		mrc	p15, 0, ip, c1, c0, 0		@ get ctrl register
		bic	ip, ip, #0x000f			@ ............wcam
		bic	ip, ip, #0x2100			@ ..v....s........
		mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
		mov	pc, r0

	
cpu_armvlsi_name:
		.asciz	"ARM"
cpu_arm720_name:
		.asciz	"ARM720T"
		.align

		.section ".text.init", #alloc, #execinstr

__arm720_setup:	mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ invalidate caches
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mcr	p15, 0, r4, c2, c0		@ load page table pointer
		mov	r0, #0x1f			@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0		@ load domain access register

		mrc	p15, 0, r0, c1, c0		@ get control register
		bic	r0, r0, #0x0e00			@ ..V. ..RS BLDP WCAM
		orr	r0, r0, #0x2100			@ .... .... .111 .... (old)
		orr	r0, r0, #0x003d			@ ..1. ..01 ..11 1101 (new)
		mov	pc, lr				@ __ret (head-armv.S)

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm720_processor_functions, #object
ENTRY(arm720_processor_functions)
		.word	v4t_late_abort
		.word	cpu_arm720_check_bugs
		.word	cpu_arm720_proc_init
		.word	cpu_arm720_proc_fin
		.word	cpu_arm720_reset
		.word	cpu_arm720_do_idle

		/* cache */
		.word	cpu_arm720_cache_clean_invalidate_all
		.word	cpu_arm720_cache_clean_invalidate_range

		/* dcache */
		.word	cpu_arm720_dcache_invalidate_range
		.word	cpu_arm720_dcache_clean_range
		.word	cpu_arm720_dcache_clean_page
		.word	cpu_arm720_dcache_clean_entry

		/* icache */
		.word	cpu_arm720_icache_invalidate_range
		.word	cpu_arm720_icache_invalidate_page

		/* pgtable */
		.word	cpu_arm720_set_pgd
		.word	cpu_arm720_set_pmd
		.word	cpu_arm720_set_pte

		.size	arm720_processor_functions, . - arm720_processor_functions

		.type	cpu_arm720_info, #object
cpu_arm720_info:
		.long	cpu_armvlsi_name
		.long	cpu_arm720_name
		.size	cpu_arm720_info, . - cpu_arm720_info
	
		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name
		.align

/*
 * See linux/include/asm-arm/procinfo.h for a definition of this structure.
 */
	
		.section ".proc.info", #alloc, #execinstr

		.type	__arm720_proc_info, #object
__arm720_proc_info:
		.long	0x41807200				@ cpu_val
		.long	0xffffff00				@ cpu_mask
		.long	0x00000c1e				@ section_mmu_flags
		b	__arm720_setup				@ cpu_flush
		.long	cpu_arch_name				@ arch_name
		.long	cpu_elf_name				@ elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB | HWCAP_26BIT	@ elf_hwcap
		.long	cpu_arm720_info				@ info
		.long	arm720_processor_functions
		.long	v4_tlb_fns
		.long	v4_user_fns
		.size	__arm720_proc_info, . - __arm720_proc_info
