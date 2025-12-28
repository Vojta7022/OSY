.PHONY: c compile r run rd run-debug rd2 run-debug2 \
	du debug-user dk debug-kernel cl clean cla cleanall hw10 hw11 hw12

QEMU=qemu-system-i386
# use this variable to pass custom flags to QEMU
QEMU_FLAGS ?=

c compile :
	$(MAKE) -C user
	$(MAKE) -C kern/build

r run:
	$(QEMU) -serial stdio -kernel kern/build/hypervisor -initrd user/hello $(QEMU_FLAGS)

rd run-debug:
	$(QEMU) -serial stdio -kernel kern/build/hypervisor -initrd user/hello -s -S $(QEMU_FLAGS)

rd2 run-debug2:
	$(QEMU) -nographic    -kernel kern/build/hypervisor -initrd user/hello -s -S $(QEMU_FLAGS)

du debug-user:
	gdb -ex 'target remote :1234' -ex 'break main' -ex 'continue' user/hello

dk debug-kernel:
	gdb -ex 'target remote :1234' -ex 'break bootstrap' -ex 'continue' kern/build/hypervisor

cl clean:
	make -C kern/build clean
	make -C user clean

cla cleanall: clean
	make -C kern/build cleanall

hw10:
	zip osy-hw10.zip kern/src/ec_syscall.cc

hw11:
	zip osy-hw11.zip kern/src/ec_syscall.cc $(or $(wildcard user/mem_alloc.cc),user/mem_alloc.c)

hw12:
	zip osy-hw12.zip kern/src/ec_syscall.cc


ifeq ($(origin WSLENV),environment)
ifneq ($(shell which qemu-system-i386.exe),)
# To run qemu installed natively in Windows (under WSL2)
# NOTE: Nova directory must be located in a path accessible from Windows (so not under e.g. ~/... in WSL)
QEMU=qemu-system-i386.exe

# TODO: add debug target when running on Windows (needs different GDB remote target, and probably also a firewall exclusion)
endif
endif
