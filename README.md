# ⟨⟩ NanoKernel-X

**A toy OS that actually boots — 512-byte MBR, protected-mode kernel with
GDT/IDT/PIC/PIT, preemptive scheduler, ring-3 user task, int 0x80
syscalls — verified headless in QEMU on every push.**

[![CI](https://github.com/sudeanb/nanokernel-x/actions/workflows/ci.yml/badge.svg)](https://github.com/sudeanb/nanokernel-x/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

```
BIOS → MBR(0x7C00) → 0x8000 kernel (protected mode)
→ GDT/TSS · IDT · PIC remap · PIT 100 Hz · STI
→ IRQ0 preemption: A B C …
→ task U: iret (RPL 3) → ring 3 → int 0x80 → USER OK
```

## What's inside

| Component | Where | The interesting part |
|---|---|---|
| MBR bootloader | `boot/mbr.S` | int 13h disk load, GDT, CR0.PE, far jump — signature at offset 510 via `. = _start + 510` (not `.balign`, which rounds to 512 × n) |
| interrupt stubs | `kernel/entry.S` | **handlers return a resume-frame pointer** — the asm never branches on privilege; `iret`'s RPL check does the ring demotion |
| GDT + TSS | `kernel/kernel.c` | user segments flat; `tss.esp0` re-aimed at the user task's kernel stack on every dispatch |
| scheduler policy | `kernel/sched.c` | **pure C, no hardware** — unit-tested on the host |
| syscalls | `int 0x80` | write / gettick / exit; exit returns *another task's frame* — the whole context switch |
| ring-3 task | `apps/user.S` | own link script at 0x400000, embedded via `.incbin` |

## Quick start

```bash
git clone https://github.com/sudeanb/nanokernel-x.git
cd nanokernel-x
make host-test     # 12 unit tests, host C
make all           # os.img
# with qemu-system-i386:
make ci-boot       # boots headless, asserts serial output
```

## The frame-return contract

Every interrupt handler — IRQ0 scheduler, IRQ1 keyboard, int 0x80
syscalls — returns a pointer to the regs frame to resume:

```
eax ecx edx ebx (oesp) ebp esi edi | ds es fs gs | eip cs eflags | uesp uss
```

- task switching = returning **another task's frame**
- ring transition = the frame's CS carries RPL 3; `iret` demotes and pops
  `uesp/uss`
- `SYS_EXIT` marks the caller a zombie and returns some *other* runnable
  frame — the entire context switch is one return value

This kills the classic privilege-branch spaghetti in the asm stubs.

## Design decisions worth reading about

[docs/DESIGN.md](docs/DESIGN.md):

- the memory map and why the kernel links where the MBR loads it
- the `.balign 512` boot-signature bug and the `. = _start + 510` fix
- user programs link **separately at 0x400000** with flat segments and are
  embedded into the kernel image via `.incbin` — the embedded blob is
  copied to 0x400000 by `copy_user()` before any dispatch
- why the scheduler policy is hardware-free C

## Limitations (honest list)

- no paging — identity map, kernel and user share linear space
- user programs are embedded in the kernel image (no filesystem/ELF loader)
- single console; no priority scheduler yet

## License

[MIT](LICENSE)
