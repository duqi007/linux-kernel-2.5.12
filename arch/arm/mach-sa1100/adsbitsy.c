/*
 * linux/arch/arm/mach-sa1100/adsbitsy.c
 *
 * Author: Woojung Huh
 *
 * Pieces specific to the ADS Bitsy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"
#include "sa1111.h"

static int __init adsbitsy_init(void)
{
	int ret;

	if (!machine_is_adsbitsy())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	/*
	 * Reset SA1111
	 */
	GPCR |= GPIO_GPIO26;
	udelay(1000);
	GPSR |= GPIO_GPIO26;

	/*
	 * Probe for SA1111.
	 */
	ret = sa1111_init(NULL, 0x18000000, IRQ_GPIO0);
	if (ret < 0)
		return ret;

	/*
	 * Enable PWM control for LCD
	 */
	SKPCR |= SKPCR_PWMCLKEN;
	SKPWM0 = 0x7F;				// VEE
	SKPEN0 = 1;
	SKPWM1 = 0x01;				// Backlight
	SKPEN1 = 1;

	return 0;
}

__initcall(adsbitsy_init);

static void __init adsbitsy_init_irq(void)
{
	/* First the standard SA1100 IRQs */
	sa1100_init_irq();
}


/*
 * Initialization fixup
 */

static void __init
fixup_adsbitsy(struct machine_desc *desc, struct param_struct *params,
		     char **cmdline, struct meminfo *mi)
{
	SET_BANK( 0, 0xc0000000, 32*1024*1024 );
	mi->nr_banks = 1;

	ROOT_DEV = mk_kdev(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
}

static struct map_desc adsbitsy_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xf4000000, 0x18000000, 0x00800000, DOMAIN_IO, 0, 1, 0, 0 }, /* SA1111 */
  LAST_DESC
};

static int adsbitsy_uart_open(struct uart_port *port, struct uart_info *info)
{
	if (port->mapbase == _Ser1UTCR0) {
		Ser1SDCR0 |= SDCR0_UART;
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO15;
	} else if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = Ser2HSCR0 = 0;
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO17;
	} else if (port->mapbase == _Ser2UTCR0) {
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO19;
	}
	return 0;
}

static struct sa1100_port_fns adsbitsy_port_fns __initdata = {
	open:	adsbitsy_uart_open,
};

static void __init adsbitsy_map_io(void)
{
	sa1100_map_io();
	iotable_init(adsbitsy_io_desc);

	sa1100_register_uart_fns(&adsbitsy_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
	GPDR |= GPIO_GPIO15 | GPIO_GPIO17 | GPIO_GPIO19;
	GPDR &= ~(GPIO_GPIO14 | GPIO_GPIO16 | GPIO_GPIO18);
}

MACHINE_START(ADSBITSY, "ADS Bitsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_adsbitsy)
	MAPIO(adsbitsy_map_io)
	INITIRQ(adsbitsy_init_irq)
MACHINE_END
