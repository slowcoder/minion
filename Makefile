CC := gcc
CXX := g++

CFLAGS := -g -Wall -I.
LDFLAGS := -g -pthread

OBJS := \
	main.o \
	vm.o \
	vm_loader.o \
	vm_cpuutils.o \
	hw/devices.o \
	hw/io.o \
	hw/pci.o \
	hw/cmos.o \
	hw/i8250.o \
	hw/i8042.o

OBJS += \
	caos/caos_linux.o \
	caos/log.o

CFLAGS += -Iinclude/

ALL_DEPS := $(patsubst %.o,%.d,$(OBJS))

all: shard

clean:
	@echo "Cleaning"
	@rm -f shard $(OBJS) $(ALL_DEPS)

shard: $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o:%.c
	@$(CC) -MM -MF $(subst .o,.d,$@) -MP -MT "$@ $(subst .o,.d,$@)" $(CFLAGS) $<
	$(CC) $(CFLAGS) -c -o $@ $<

%.o:%.cpp
	@$(CXX) -MM -MF $(subst .o,.d,$@) -MP -MT "$@ $(subst .o,.d,$@)" $(CFLAGS) $<
	$(CXX) $(CFLAGS) -c -o $@ $<

ifneq ("$(MAKECMDGOALS)","clean")
-include $(ALL_DEPS)
endif
