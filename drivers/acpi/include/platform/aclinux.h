/******************************************************************************
 *
 * Name: aclinux.h - OS specific defines, etc.
 *       $Revision: 19 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ACLINUX_H__
#define __ACLINUX_H__

#define ACPI_OS_NAME                "Linux"

#define ACPI_USE_SYSTEM_CLIBRARY

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/div64.h>

#define strtoul simple_strtoul

#ifdef _IA64
#define ACPI_FLUSH_CPU_CACHE()
#else
#define ACPI_FLUSH_CPU_CACHE()	wbinvd()
#endif

#else /* !__KERNEL__ */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#endif /* __KERNEL__ */

/* Linux uses GCC */

#include "acgcc.h"

#undef DEBUGGER_THREADING
#define DEBUGGER_THREADING          DEBUGGER_SINGLE_THREADED


#endif /* __ACLINUX_H__ */
