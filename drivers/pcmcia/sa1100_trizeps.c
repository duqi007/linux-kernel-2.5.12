/*
 * drivers/pcmcia/sa1100_trizeps.c
 *
 * PCMCIA implementation routines for Trizeps
 *
 * Authors:
 * Andreas Hofer <ho@dsa-ac.de>,
 * Peter Lueg <pl@dsa-ac.de>,
 * Guennadi Liakhovetski <gl@dsa-ac.de>
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/hardware.h>   // included trizeps.h
#include <asm/system.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

#define NUMBER_OF_TRIZEPS_PCMCIA_SLOTS 1
/**
 *
 *
 ******************************************************/
static int trizeps_pcmcia_init(struct pcmcia_init *init)
{
	int res;

	/* Enable CF bus: */
	TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_nPCM_ENA_REG);

	/* All those are inputs */
	GPDR &= ~((GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_CD0))
		    | (GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_IRQ0)));

	/* Set transition detect */
//	set_irq_type(SA1100_GPIO_TO_IRQ(GPIO_TRIZEPS_PCMCIA_CD0), IRQT_BOTHEDGE);
	set_irq_type(TRIZEPS_IRQ_PCMCIA_IRQ0, IRQT_FALLING);

	/* Register SOCKET interrupts */
	/* WHY? */
	set_irq_type(TRIZEPS_IRQ_PCMCIA_CD0, IRQT_NOEDGE);
	res = request_irq( TRIZEPS_IRQ_PCMCIA_CD0, init->handler, SA_INTERRUPT, "PCMCIA_CD0", NULL );
	if( res < 0 ) goto irq_err;

	//MECR = 0x00060006; // Initialised on trizeps init

	// return=sa1100_pcmcia_socket_count (sa1100_generic.c)
	//        -> number of PCMCIA Slots
	// Slot 0 -> Trizeps PCMCIA
	// Slot 1 -> Trizeps ISA-Bus
	return NUMBER_OF_TRIZEPS_PCMCIA_SLOTS;

 irq_err:
	printk( KERN_ERR __FUNCTION__ ": PCMCIA Request for IRQ %u failed\n", TRIZEPS_IRQ_PCMCIA_CD0 );
	return -1;
}

/**
 *
 *
 ******************************************************/
static int trizeps_pcmcia_shutdown(void)
{
	printk(">>>>>PCMCIA TRIZEPS shutdown\n");
	/* disable IRQs */
	free_irq(TRIZEPS_IRQ_PCMCIA_CD0, NULL );

	/* Disable CF bus: */
	TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_nPCM_ENA_REG);

	return 0;
}

/**
 *

 ******************************************************/
static int trizeps_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
	unsigned long levels;

	if (state_array->size < NUMBER_OF_TRIZEPS_PCMCIA_SLOTS) return -1;

	memset(state_array->state, 0,
	       (state_array->size)*sizeof(struct pcmcia_state));

	levels = GPLR;

	state_array->state[0].detect = ((levels & GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_CD0)) == 0) ? 1 : 0;
	state_array->state[0].ready  = ((levels & GPIO_GPIO(TRIZEPS_GPIO_PCMCIA_IRQ0)) != 0) ? 1 : 0;
	state_array->state[0].bvd1   = ((TRIZEPS_BCR1 & TRIZEPS_PCM_BVD1) !=0 ) ? 1 : 0;
	state_array->state[0].bvd2   = ((TRIZEPS_BCR1 & TRIZEPS_PCM_BVD2) != 0) ? 1 : 0;
	state_array->state[0].wrprot = 0; // not write protected
	state_array->state[0].vs_3v  = ((TRIZEPS_BCR1 & TRIZEPS_nPCM_VS1) == 0) ? 1 : 0; //VS1=0 -> vs_3v=1
	state_array->state[0].vs_Xv  = ((TRIZEPS_BCR1 & TRIZEPS_nPCM_VS2) == 0) ? 1 : 0; //VS2=0 -> vs_Xv=1

	return 1;  // success
}

/**
 *
 *
 ******************************************************/
static int trizeps_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

	switch (info->sock) {
	case 0:
		info->irq=TRIZEPS_IRQ_PCMCIA_IRQ0;
		break;
	case 1:
		// MFTB2 use only one Slot
	default:
		return -1;
	}
	return 0;
}

/**
 *
 *
 ******************************************************/
static int trizeps_pcmcia_configure_socket(const struct pcmcia_configure *configure)
{
	unsigned long flags;

	if(configure->sock>1) return -1;

	local_irq_save(flags);

	switch (configure->vcc) {
	case 0:
		printk(">>> PCMCIA Power off\n");
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;

	case 33:
		// 3.3V Power on
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;
	case 50:
		// 5.0V Power on
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_PCM_V3_EN_REG);
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_PCM_V5_EN_REG);
		break;
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
		       configure->vcc);
		local_irq_restore(flags);
		return -1;
	}

	if (configure->reset)
		TRIZEPS_BCR_set(TRIZEPS_BCR1, TRIZEPS_nPCM_RESET_DISABLE);   // Reset
	else
		TRIZEPS_BCR_clear(TRIZEPS_BCR1, TRIZEPS_nPCM_RESET_DISABLE); // no Reset
	/*
	  printk(" vcc=%u vpp=%u -->reset=%i\n",
	  configure->vcc,
	  configure->vpp,
	  ((BCR_read(1) & nPCM_RESET_DISABLE)? 1:0));
	*/
	local_irq_restore(flags);

	if (configure->irq) {
		enable_irq(TRIZEPS_IRQ_PCMCIA_CD0);
		enable_irq(TRIZEPS_IRQ_PCMCIA_IRQ0);
	} else {
		disable_irq(TRIZEPS_IRQ_PCMCIA_IRQ0);
		disable_irq(TRIZEPS_IRQ_PCMCIA_CD0);
	}

	return 0;
}

static int trizeps_pcmcia_socket_init(int sock)
{
	return 0;
}

static int trizeps_pcmcia_socket_suspend(int sock)
{
	return 0;
}

/**
 * low-level PCMCIA interface
 *
 ******************************************************/
struct pcmcia_low_level trizeps_pcmcia_ops = {
	init:			trizeps_pcmcia_init,
	shutdown:		trizeps_pcmcia_shutdown,
	socket_state:		trizeps_pcmcia_socket_state,
	get_irq_info:		trizeps_pcmcia_get_irq_info,
	configure_socket:	trizeps_pcmcia_configure_socket,
	socket_init:		trizeps_pcmcia_socket_init,
	socket_suspend:		trizeps_pcmcia_socket_suspend,
};

int __init pcmcia_trizeps_init(void)
{
	if (machine_is_trizeps()) {
		return sa1100_register_pcmcia(&trizeps_pcmcia_ops);
	}
	return -ENODEV;
}

void __exit pcmcia_trizeps_exit(void)
{
	sa1100_unregister_pcmcia(&trizeps_pcmcia_ops);
}
