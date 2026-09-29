# NanoKernel-X build
# CI (ubuntu): gcc-multilib + objcopy + qemu-system-x86
# local: `make compile-check` (clang cross, objects only) + `make host-test`

CC      ?= gcc
AS      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
QEMU    ?= qemu-system-i386

CFLAGS  = -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -nostdlib -O2 -Wall -Wextra -mno-80387 -Iinclude
ASFLAGS = --32

all: os.img

kernel/entry.o: kernel/entry.S
	$(AS) $(ASFLAGS) -c $< -o $@

kernel/kernel.o: kernel/kernel.c include/nk.h
	$(CC) $(CFLAGS) -c $< -o $@

apps/user.o: apps/user.S apps/user.ld
	$(AS) $(ASFLAGS) -c apps/user.S -o apps/user_raw.o
	$(LD) -m elf_i386 -T apps/user.ld -o apps/user.elf apps/user_raw.o
	$(OBJCOPY) -O binary apps/user.elf apps/user.bin
	@printf '.section .rodata\n.globl user_bin_start\nuser_bin_start:\n.incbin "apps/user.bin"\n.globl user_bin_end\nuser_bin_end:\n' > apps/user_blob.s
	$(AS) $(ASFLAGS) -c apps/user_blob.s -o $@

kernel/entry.o: boot/mbr.S

kernel.elf: kernel/entry.o kernel/kernel.o apps/user.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ kernel/entry.o kernel/kernel.o apps/user.o

mbr.bin: boot/mbr.S boot/mbr.ld
	$(AS) --32 -c boot/mbr.S -o mbr.o
	$(LD) -m elf_i386 -T boot/mbr.ld -o mbr.elf mbr.o
	$(OBJCOPY) -O binary mbr.elf $@

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

os.img: kernel.bin mbr.bin
	dd if=/dev/zero of=os.img bs=512 count=64 2>/dev/null
	dd if=mbr.bin of=os.img bs=512 seek=0 conv=notrunc
	dd if=kernel.bin of=os.img bs=512 seek=1 conv=notrunc
	@echo "os.img ready"

compile-check:
	clang -target i386-pc-none -ffreestanding -fno-pic -fno-stack-protector -mno-80387 -O2 -Wall -Wextra -Iinclude -c kernel/kernel.c -o /dev/null
	@echo "compile-check done (objects only; link+boot in CI)"

host-test: test/sched_test
	./test/sched_test

test/sched_test: test/sched_test.c kernel/sched.c include/nk.h
	cc -O1 -Iinclude -o $@ test/sched_test.c kernel/sched.c

test: host-test

ci-boot: os.img
	timeout 15 $(QEMU) -m 32M -drive format=raw,file=os.img \
	  -serial file:serial.out -display none -no-reboot -no-shutdown \
	  -debugcon file:debugcon.out -global isa-debugcon.iobase=0x402 \
	  -d int,cpu_reset -D qemu.debug || true
	@echo "=== serial.out ==="
	@cat serial.out 2>/dev/null || echo "(yok)"
	@echo "=== serial.out hex ==="
	@od -c serial.out 2>/dev/null | head -10 || true
	@echo "=== qemu trace son 60 satır ==="
	@grep -E "v=|RESET" qemu.debug 2>/dev/null | tail -60 || echo "(trace yok)"
	@echo "=== seabios debugcon (ilk 40) ==="
	@head -40 debugcon.out 2>/dev/null || true
	@echo "=== nm kernel.elf ==="
	@nm -n kernel.elf 2>/dev/null | head -40
	grep -q "NANOKERNEL-X BOOT OK" serial.out
	grep -q "SCHED OK" serial.out
	grep -q "USER OK" serial.out
	@echo "CI BOOT TEST PASSED"

clean:
	rm -rf *.o kernel/*.o apps/*.o *.elf *.bin *.img serial.out test/sched_test apps/user_raw.o apps/user.elf apps/user_blob.s

.PHONY: all compile-check host-test test ci-boot clean
