#
# S/390 network devices
#

O_TARGET := s390-net.o

export-objs := iucv.o fsm.o

ctc-objs := ctcmain.o ctctty.o

obj-y += iucv.o fsm.o
obj-$(CONFIG_CTC) += ctc.o
obj-$(CONFIG_IUCV) += netiucv.o

include $(TOPDIR)/Rules.make
