#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _i2c.o

export-objs  := i2c.o cs8427.o tea6330t.o

snd-i2c-objs := i2c.o
snd-cs8427-objs := cs8427.o
snd-tea6330t-objs := tea6330t.o

ifeq ($(subst m,y,$(CONFIG_L3)),y)
  subdir-$(CONFIG_L3) += l3
endif
ifeq ($(filter $(subdir-y),l3),l3)
  subdir-m := l3
  obj-y += l3/l3.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-tea6330t.o snd-i2c.o
obj-$(CONFIG_SND_ICE1712) += snd-cs8427.o snd-i2c.o

include $(TOPDIR)/Rules.make
