CC := gcc
CXX := g++

CFLAGS := -g -Wall -Werror -I.
LDFLAGS := -g -pthread

# Minion core
OBJS := \
	main.o \
	vm/vm_api.o \
	vm/vm.o \
	vm/vm_biostables.o \
	vm/vm_cpus.o \
	vm/vm_cpus_util.o \
	vm/vm_memory.o \
	vm/loader.o \

# Busses and Devices
OBJS += \
	hw/devices.o \
	hw/disks/disks.o \
	hw/isa/isa.o \
	hw/isa/cmos.o \
	hw/isa/i8042.o \
	hw/isa/i8250.o \
	hw/pci/pci.o \
	hw/pci/pci_hostbridge.o \
	hw/pci/serial.o \
	hw/pci/virtio_blk.o

# CAOS
OBJS += \
	caos/caos_linux.o \
	caos/log.o

CFLAGS += -Icaos/include/

ALL_DEPS := $(patsubst %.o,%.d,$(OBJS))

all: minion

clean:
	@echo "Cleaning"
	@rm -f minion $(OBJS) $(ALL_DEPS)

minion: $(OBJS)
	@echo "LINK $@"
	@$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o:%.c
	@echo "CC $<"
	@$(CC) -MM -MF $(subst .o,.d,$@) -MP -MT "$@ $(subst .o,.d,$@)" $(CFLAGS) $<
	@$(CC) $(CFLAGS) -c -o $@ $<

#%.o:%.cpp
#	@$(CXX) -MM -MF $(subst .o,.d,$@) -MP -MT "$@ $(subst .o,.d,$@)" $(CFLAGS) $<
#	$(CXX) $(CFLAGS) -c -o $@ $<

ifneq ("$(MAKECMDGOALS)","clean")
-include $(ALL_DEPS)
endif
