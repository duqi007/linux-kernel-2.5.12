/*
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

/*
 * ALI M14xx chipset EIDE controller
 *
 * Works for ALI M1439/1443/1445/1487/1489 chipsets.
 *
 * Adapted from code developed by derekn@vw.ece.cmu.edu.  -ml
 * Derek's notes follow:
 *
 * I think the code should be pretty understandable,
 * but I'll be happy to (try to) answer questions.
 *
 * The critical part is in the setupDrive function.  The initRegisters
 * function doesn't seem to be necessary, but the DOS driver does it, so
 * I threw it in.
 *
 * I've only tested this on my system, which only has one disk.  I posted
 * it to comp.sys.linux.hardware, so maybe some other people will try it
 * out.
 *
 * Derek Noonburg  (derekn@ece.cmu.edu)
 * 95-sep-26
 *
 * Update 96-jul-13:
 *
 * I've since upgraded to two disks and a CD-ROM, with no trouble, and
 * I've also heard from several others who have used it successfully.
 * This driver appears to work with both the 1443/1445 and the 1487/1489
 * chipsets.  I've added support for PIO mode 4 for the 1487.  This
 * seems to work just fine on the 1443 also, although I'm not sure it's
 * advertised as supporting mode 4.  (I've been running a WDC AC21200 in
 * mode 4 for a while now with no trouble.)  -Derek
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ata-timing.h"

/* port addresses for auto-detection */
#define ALI_NUM_PORTS 4
static int ports[ALI_NUM_PORTS] __initdata = {0x074, 0x0f4, 0x034, 0x0e4};

/* register initialization data */
typedef struct { byte reg, data; } RegInitializer;

static RegInitializer initData[] __initdata = {
	{0x01, 0x0f}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
	{0x05, 0x00}, {0x06, 0x00}, {0x07, 0x2b}, {0x0a, 0x0f},
	{0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x00}, {0x2a, 0x00}, {0x2f, 0x00}, {0x2b, 0x00},
	{0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00}, {0x34, 0xff},
	{0x35, 0x03}, {0x00, 0x00}
};

/* timing parameter registers for each drive */
static struct { byte reg1, reg2, reg3, reg4; } regTab[4] = {
	{0x03, 0x26, 0x04, 0x27},     /* drive 0 */
	{0x05, 0x28, 0x06, 0x29},     /* drive 1 */
	{0x2b, 0x30, 0x2c, 0x31},     /* drive 2 */
	{0x2d, 0x32, 0x2e, 0x33},     /* drive 3 */
};

static int basePort;	/* base port address */
static int regPort;	/* port for register number */
static int dataPort;	/* port for register data */
static byte regOn;	/* output to base port to access registers */
static byte regOff;	/* output to base port to close registers */

/*------------------------------------------------------------------------*/

/*
 * Read a controller register.
 */
static inline byte inReg (byte reg)
{
	outb_p(reg, regPort);
	return inb(dataPort);
}

/*
 * Write a controller register.
 */
static void outReg (byte data, byte reg)
{
	outb_p(reg, regPort);
	outb_p(data, dataPort);
}

/*
 * Set PIO mode for the specified drive.
 * This function computes timing parameters
 * and sets controller registers accordingly.
 */
static void ali14xx_tune_drive (ide_drive_t *drive, byte pio)
{
	int driveNum;
	int time1, time2;
	byte param1, param2, param3, param4;
	unsigned long flags;
	struct ata_timing *t;

	if (pio == 255)
		pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		pio = XFER_PIO_0 + min_t(byte, pio, 4);

	t = ata_timing_data(pio);

	/* calculate timing, according to PIO mode */
	time1 = t->cycle;
	time2 = t->active;
	param3 = param1 = (time2 * system_bus_speed + 999) / 1000;
	param4 = param2 = (time1 * system_bus_speed + 999) / 1000 - param1;
	if (pio < XFER_PIO_3) {
		param3 += 8;
		param4 += 8;
	}
	printk("%s: PIO mode%d, t1=%dns, t2=%dns, cycles = %d+%d, %d+%d\n",
		drive->name, pio - XFER_PIO_0, time1, time2, param1, param2, param3, param4);

	/* stuff timing parameters into controller registers */
	driveNum = (drive->channel->index << 1) + drive->select.b.unit;
	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	outb_p(regOn, basePort);
	outReg(param1, regTab[driveNum].reg1);
	outReg(param2, regTab[driveNum].reg2);
	outReg(param3, regTab[driveNum].reg3);
	outReg(param4, regTab[driveNum].reg4);
	outb_p(regOff, basePort);
	restore_flags(flags);	/* all CPUs */
}

/*
 * Auto-detect the IDE controller port.
 */
static int __init findPort (void)
{
	int i;
	byte t;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	for (i = 0; i < ALI_NUM_PORTS; ++i) {
		basePort = ports[i];
		regOff = inb(basePort);
		for (regOn = 0x30; regOn <= 0x33; ++regOn) {
			outb_p(regOn, basePort);
			if (inb(basePort) == regOn) {
				regPort = basePort + 4;
				dataPort = basePort + 8;
				t = inReg(0) & 0xf0;
				outb_p(regOff, basePort);
				__restore_flags(flags);	/* local CPU only */
				if (t != 0x50)
					return 0;
				return 1;  /* success */
			}
		}
		outb_p(regOff, basePort);
	}
	__restore_flags(flags);	/* local CPU only */
	return 0;
}

/*
 * Initialize controller registers with default values.
 */
static int __init initRegisters (void) {
	RegInitializer *p;
	byte t;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	outb_p(regOn, basePort);
	for (p = initData; p->reg != 0; ++p)
		outReg(p->data, p->reg);
	outb_p(0x01, regPort);
	t = inb(regPort) & 0x01;
	outb_p(regOff, basePort);
	__restore_flags(flags);	/* local CPU only */
	return t;
}

void __init init_ali14xx (void)
{
	/* auto-detect IDE controller port */
	if (!findPort()) {
		printk("\nali14xx: not found");
		return;
	}

	printk("\nali14xx: base= 0x%03x, regOn = 0x%02x", basePort, regOn);
	ide_hwifs[0].chipset = ide_ali14xx;
	ide_hwifs[1].chipset = ide_ali14xx;
	ide_hwifs[0].tuneproc = &ali14xx_tune_drive;
	ide_hwifs[1].tuneproc = &ali14xx_tune_drive;
	ide_hwifs[0].unit = ATA_PRIMARY;
	ide_hwifs[1].unit = ATA_SECONDARY;

	/* initialize controller registers */
	if (!initRegisters()) {
		printk("\nali14xx: Chip initialization failed");
		return;
	}
}