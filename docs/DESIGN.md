# NanoKernel-X — Design Document

A toy operating system for x86 (32-bit protected mode) that boots from a
512-byte MBR, runs preemptive tasks, and demotes to ring 3 for a user
program that talks back through syscalls. Verified booting headless in
QEMU on every push.

## 1. Memory map

```
0x0000_0000 ── real-mode IVT ( BIOS era, unused after protected mode )
0x0000_7C00 ── MBR load address (boot sector)
0x0000_8000 ── kernel load + link address (9 sectors from disk)
0x0009_0000 ── boot/kernel stack (grows down)
0x0009_8000 ── TSS esp0 kernel stack top (ring-3 interrupt frames)
0x0040_0000 ── user program (flat user segments, base 0)
0x000B_8000 ── VGA text buffer (80×25)
```

The kernel is linked at 0x8000 to match the MBR's load address — no
relocation needed after the disk read.

## 2. Boot flow

```
BIOS
 → MBR at 0x7C00 (real mode, cli, segments zeroed, stack 0x7C00)
 → int 13h AH=02: read 9 sectors (LBA 1..9) → 0x8000
 → lgdt (null / code 0x08 / data 0x10, base 0 limit 4G)
 → CR0.PE = 1 → far jmp 0x08 (32-bit)
→ kernel_entry at 0x8000
   esp = 0x90000, .bss zeroed, kmain()
```

The MBR fits in 512 bytes including the 0xAA55 signature at offset 510
(placed with `. = _start + 510`, not `.balign` — balign would round to a
sector boundary and push the signature into the next sector).

## 3. GDT and rings

| selector | segment | access |
|---|---|---|
| 0x08 | kernel code | ring 0, exec/read |
| 0x10 | kernel data | ring 0, r/w |
| 0x18 | user code | ring 3, exec/read |
| 0x20 | user data | ring 3, r/w |
| 0x28 | TSS (64-byte, 32-bit) | system |

All segments are flat (base 0, limit 4 GiB) except simplicity itself —
the user program links at 0x400000 and uses flat addressing too.

The TSS exists for exactly one thing: `esp0`. Every time the scheduler
dispatches the user task, `tss.esp0` is re-aimed at that task's kernel
stack top, so an interrupt or syscall from ring 3 lands on the right
kernel stack.

## 4. Interrupts

- **PIC remapped** to vectors 0x20–0x2F (IRQ0 = PIT @ 100 Hz, IRQ1 = PS/2)
- **IDT**: 256 gates, flags 0x8E (ring 0, interrupt gate), kernel selector
- handlers follow the **frame-return contract**:

```
asm stub: pusha, push ds/es/fs/gs, mov esp→eax, call handler
handler:  returns the pointer to the regs frame to resume
asm:      mov eax→esp, pop gs/fs/es/ds, popa, iret
```

The regs frame layout (low → high) matches the push order exactly:

```
eax ecx edx ebx oesp ebp esi edi | ds es fs gs | eip cs eflags | uesp uss
```

`uesp/uss` are used **only** by ring-3 entry frames; for same-privilege
returns iret ignores them.

**Ring transitions are free**: the asm never inspects privilege. The
frame's CS carries an RPL — when it is 3, iret demotes to ring 3 and pops
`uesp/uss`; when 0, it resumes the kernel frame. One mechanism, two worlds.

## 5. Scheduler

`kernel/sched.c` is pure C with zero hardware access:

```c
int sched_pick_next(int current);   // round-robin over runnable tasks
```

The IRQ0 path (`irq0_handler_c`):

1. `g_ticks++`, EOI
2. save the interrupted frame pointer into the current task
3. `sched_pick_next` → next runnable (or idle: resume same frame)
4. first dispatch of the user task → build a ring-3 entry frame
5. otherwise return the saved frame (or build one for a never-run kernel task)

Demo tasks A/B/C print their letter every ~300k delay-loop iterations,
preempted by IRQ0 regardless of their loop state.

## 6. Syscalls

`int 0x80` from ring 3: the CPU switches to `tss.esp0` (the user task's
kernel stack) and pushes the hardware frame; the stub saves user registers
into a regs frame and calls the dispatcher:

| eax | call | effect |
|---|---|---|
| 1 | `write(ebx=buf, ecx=len)` | bytes to serial + VGA |
| 2 | `gettick(edx=out)` | user memory ← tick count |
| 3 | `exit` | task → zombie, reschedule |

`SYS_EXIT` returning another task's frame is the whole context switch.

## 7. Build and CI

```
clang -target i386-pc-none   # local compile check (objects only)
gcc -m32                     # CI build (ubuntu: gcc-multilib)
objcopy -O binary            # ELF → flat kernel.bin
qemu-system-i386 -drive os.img -serial file:serial.out -display none
grep BOOT OK / USER OK / SCHED OK serial.out
```

CI (ubuntu-latest) installs `gcc-multilib nasm qemu-system-x86`, builds
`os.img`, runs 12 host unit tests, then boots headless and asserts the
serial output. The boot assertion is the repo's integration test.

## 8. Roadmap

- paging (kernel page tables, user address space separation)
- ELF user loader + a real tiny filesystem
- priority scheduler + sleep/wake on timer
- serial shell reading the keyboard queue
