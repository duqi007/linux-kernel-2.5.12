# Makefile for the AVM ISDN device drivers

# The target object and module list name.

O_TARGET	:= vmlinux-obj.o

# Objects that export symbols.

export-objs	:= b1dma.o b1pcmcia.o b1.o

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_AVMB1_B1ISA)	+= b1isa.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCI)	+= b1pci.o b1.o b1dma.o
obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCMCIA)	+= b1pcmcia.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_AVM_CS)	+= avm_cs.o
obj-$(CONFIG_ISDN_DRV_AVMB1_T1ISA)	+= t1isa.o b1.o
obj-$(CONFIG_ISDN_DRV_AVMB1_T1PCI)	+= t1pci.o b1.o b1dma.o
obj-$(CONFIG_ISDN_DRV_AVMB1_C4)		+= c4.o b1.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
