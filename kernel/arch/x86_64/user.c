/**
 * @file  kernel/arch/x86_64/user.c
 * @brief Various assembly snippets for jumping to usermode and back.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include <bits/errno.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/syscall.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/acpi.h>

/**
 * @brief Enter userspace.
 *
 * Called by process startup.
 * Does not return.
 *
 * @param entrypoint Address to "return" to in userspace.
 * @param argc       Number of arguments to provide to the new process.
 * @param argv       Argument array to pass to the new process; make sure this is user-accessible!
 * @param envp       Environment strings array
 * @param stack      Userspace stack address.
 */
void arch_enter_user(uintptr_t entrypoint, int argc, char * argv[], char * envp[], uintptr_t stack) {
	struct regs ret;
	ret.cs = 0x28 | 0x03;
	ret.ss = 0x20 | 0x03;
	ret.rip = entrypoint;
	ret.rflags = (1 << 21) | (1 << 9);
	ret.rsp = stack;

	ret.rsp = (ret.rsp & (uintptr_t)-16) - 8;

	update_process_times_on_exit();

	asm volatile (
		"pushq %0\n"
		"pushq %1\n"
		"pushq %2\n"
		"pushq %3\n"
		"pushq %4\n"
		"swapgs\n"
		"iretq"
	: : "m"(ret.ss), "m"(ret.rsp), "m"(ret.rflags), "m"(ret.cs), "m"(ret.rip),
	    "D"(argc), "S"(argv), "d"(envp));
}

static void _kill_it(void) {
	dprintf("core %d (pid=%d %s): invalid stack for signal return\n",
		this_core->cpu_id, this_core->current_process->id, this_core->current_process->name);
	task_exit(((128 + SIGSEGV) << 8) | SIGSEGV);
}

#define PUSH(stack, type, item) do { \
	stack -= sizeof(type); \
	if (!mmu_validate_user_pointer((void*)(uintptr_t)stack,sizeof(type),MMU_PTR_WRITE)) \
		_kill_it(); \
	*((volatile type *) stack) = item; \
} while (0)

#define POP(stack, type, item) do { \
	if (!mmu_validate_user_pointer((void*)(uintptr_t)stack,sizeof(type),0)) \
		_kill_it(); \
	item = *((volatile type *) stack); \
	stack += sizeof(type); \
} while (0)

int arch_return_from_signal_handler(struct regs *r) {

	POP(r->rsp, sigset_t, this_core->current_process->blocked_signals);
	long originalSignal;
	POP(r->rsp, long, originalSignal);

	POP(r->rsp, long, this_core->current_process->interrupted_system_call);

	for (int i = 0; i < 64; ++i) {
		POP(r->rsp, uint64_t, this_core->current_process->thread.fp_regs[63-i]);
	}

	arch_restore_floating((process_t*)this_core->current_process);

	struct regs out;
	POP(r->rsp, struct regs, out);

#define R(n) r-> n = out. n

	R(r15); R(r14); R(r13); R(r12);
	R(r11); R(r10); R(r9); R(r8);
	R(rbp); R(rdi); R(rsi); R(rdx); R(rcx); R(rbx); R(rax);

	R(rip);
	R(rsp);

	r->rflags = (out.rflags & 0xcd5) | (1 << 21) | (1 << 9) | ((r->rflags & (1 << 8)) ? (1 << 8) : 0);
	return originalSignal;
}


/**
 * @brief Enter a userspace signal handler.
 *
 * Similar to @c arch_enter_user but also setups up magic return addresses.
 *
 * Since signal handlers do to take complicated argument arrays, this only
 * supplies a @p signum argument.
 *
 * Does not return.
 *
 * @param entrypoint Userspace address of the signal handler, set by the process.
 * @param signum     Signal number that caused this entry.
 */
void arch_enter_signal_handler(struct signal_config * config, siginfo_t * cause, struct regs *r) {
	struct regs ret;
	ret.cs = 0x28 | 0x03;
	ret.ss = 0x20 | 0x03;
	ret.rip = config->handler;
	ret.rflags = (1 << 21) | (1 << 9);
	ret.rsp = ((r->rsp - 128) & (uintptr_t)-16) - 8; /* ensure considerable alignment */

	uintptr_t ucontext_addr = 0;
	uintptr_t sainfo_addr = 0;

	if (config->flags & SA_SIGINFO) {
		PUSH(ret.rsp, siginfo_t, *cause);
		sainfo_addr = ret.rsp;

		/* Bottom of ucontext_t */
		PUSH(ret.rsp, uintptr_t, 0); /* TODO uc_link */
		ucontext_addr = ret.rsp;
	}

	PUSH(ret.rsp, struct regs, *r);

	arch_save_floating((process_t*)this_core->current_process);
	for (int i = 0; i < 64; ++i) {
		PUSH(ret.rsp, uint64_t, this_core->current_process->thread.fp_regs[i]);
	}

	/* Common stuff */
	PUSH(ret.rsp, long, this_core->current_process->interrupted_system_call);
	this_core->current_process->interrupted_system_call = 0;

	PUSH(ret.rsp, long, cause->si_signo);
	PUSH(ret.rsp, sigset_t, this_core->current_process->blocked_signals);

	this_core->current_process->blocked_signals |= config->mask | (config->flags & SA_NODEFER ? 0 : (1UL << cause->si_signo));

	PUSH(ret.rsp, uintptr_t, 0x516);

	update_process_times_on_exit();

	asm volatile(
		"pushq %0\n"
		"pushq %1\n"
		"pushq %2\n"
		"pushq %3\n"
		"pushq %4\n"
		"swapgs\n"
		"iretq"
	: : "m"(ret.ss), "m"(ret.rsp), "m"(ret.rflags), "m"(ret.cs), "m"(ret.rip),
	    "D"(cause->si_signo),
	    "S"(sainfo_addr),
	    "d"(ucontext_addr));
	__builtin_unreachable();
}

/**
 * @brief Return from fork or clone.
 *
 * This is what we inject as the stored rip for a new thread,
 * so that it immediately returns from the system call.
 *
 * This is never called as a function, its address is stored
 * in the thread context of a new @c process_t.
 */
__attribute__((naked))
void arch_resume_user(void) {
	asm volatile (
		"pop %r15\n"
		"pop %r14\n"
		"pop %r13\n"
		"pop %r12\n"
		"pop %r11\n"
		"pop %r10\n"
		"pop %r9\n"
		"pop %r8\n"
		"pop %rbp\n"
		"pop %rdi\n"
		"pop %rsi\n"
		"pop %rdx\n"
		"pop %rcx\n"
		"pop %rbx\n"
		"pop %rax\n"
		"add $16, %rsp\n"
		"swapgs\n"
		"iretq\n"
	);
	__builtin_unreachable();
}

/**
 * @brief Save FPU registers for this thread.
 */
void arch_restore_floating(process_t * proc) {
	asm volatile ("fxrstor (%0)" :: "r"(&proc->thread.fp_regs));
}

/**
 * @brief Restore FPU registers for this thread.
 */
void arch_save_floating(process_t * proc) {
	asm volatile ("fxsave (%0)" :: "r"(&proc->thread.fp_regs));
}

/**
 * @brief Called in a loop by kernel idle tasks.
 *
 * Turns on and waits for interrupts.
 * There is room for improvement here with other power states,
 * but HLT is "good enough" for us.
 */
void arch_pause(void) {
	asm volatile (
		"sti\n"
		"hlt\n"
		"cli\n"
	);
}

extern void lapic_send_ipi(int i, uint32_t val);

/**
 * @brief Prepare for a fatal event by stopping all other cores.
 *
 * Sends an IPI to all other CPUs to tell them to immediately stop.
 * This causes an NMI (isr2), which disables interrupts and loops
 * on a hlt instruction.
 *
 * Ensures that we can then print tracebacks and do other complicated
 * things without having to mess with locks, and without other
 * processors causing further damage in the case of a fatal error.
 */
void arch_fatal_prepare(void) {
	for (int i = 0; i < processor_count; ++i) {
		if (i == this_core->cpu_id) continue;
		lapic_send_ipi(processor_local_data[i].lapic_id, 0x447D);
	}
}

/**
 * @brief Halt all processors, including this one.
 * @see arch_fatal_prepare
 */
void arch_fatal(void) {
	arch_fatal_prepare();
	while (1) {
		asm volatile (
			"cli\n"
			"hlt\n"
		);
	}
}

/**
 * @brief Reboot the computer.
 *
 * This tries to do a "keyboard reset". We clear out the IDT
 * so that we can maybe triple fault, and then we try to use
 * the keyboard reset vector... if that doesn't work,
 * then returning from this and letting anything else happen
 * almost certainly will.
 */
long arch_reboot(void) {
	/* load a null page as an IDT */
	uintptr_t frame = mmu_allocate_a_frame();
	uintptr_t * idt = mmu_map_from_physical(frame << 12);
	memset(idt, 0, 0x1000);
	asm volatile (
		"lidt (%0)"
		: : "r"(idt)
	);
	uint8_t out = 0x02;
	while ((out & 0x02) != 0) {
		out = inportb(0x64);
	}
	outportb(0x64, 0xFE); /* Reset */
	return 0;
}

/**
 * @brief Locate the RSDP by scanning the BIOS/EBDA region.
 */
static uintptr_t acpi_find_rsdp(void) {
	/* Search the BIOS read-only memory region 0xE0000 - 0xFFFFF */
	for (uintptr_t scan = 0x000E0000; scan < 0x00100000; scan += 16) {
		char * p = mmu_map_from_physical(scan);
		if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' &&
		    p[3] == 'P' && p[4] == 'T' && p[5] == 'R') {
			return scan;
		}
	}
	return 0;
}

/**
 * @brief Parse the _S5 package from a DSDT to find the S5 sleep type.
 *
 * Returns the PM1x SLP_TYP value for the S5 (soft off) state, or -1 if it
 * could not be determined.
 */
static int acpi_find_s5_slptyp(uint8_t * dsdt, uint32_t length) {
	/* Look for the "_S5_" name (root-relative NameString: 0x5F 'S' '5') */
	for (uint32_t i = 0; i + 4 < length; ++i) {
		if (dsdt[i] == '_' && dsdt[i+1] == 'S' && dsdt[i+2] == '5') {
			uint32_t p = i + 3;
			/* Skip to the PackageOp (0x12) */
			while (p + 1 < length && dsdt[p] != 0x12) p++;
			if (p + 1 >= length) return -1;
			p++; /* now at PkgLength */
			/* PkgLength may have continuation bits; decode */
			uint8_t pkglen = dsdt[p++];
			if (pkglen & 0x80) {
				/* 2-byte form */
				pkglen = (pkglen & 0x0F) | (dsdt[p++] << 4);
			}
			(void)pkglen;
			/* Number of elements (ByteData) */
			if (p >= length) return -1;
			if (dsdt[p] == 0x0A) {
				p += 2; /* ByteConst */
			} else if (dsdt[p] == 0x0B) {
				p += 3; /* WordConst */
			} else {
				p += 1; /* Zero / One / direct byte */
			}
			/* First element = SLP_TYPa */
			if (p >= length) return -1;
			if (dsdt[p] == 0x0A) {
				return dsdt[p+1]; /* ByteConst */
			} else if (dsdt[p] == 0x00) {
				return 0; /* Zero */
			} else if (dsdt[p] == 0x01) {
				return 1; /* One */
			} else if (dsdt[p] == 0x0B) {
				return dsdt[p+1]; /* WordConst low byte */
			}
			return -1;
		}
	}
	return -1;
}

/**
 * @brief Power off the system via ACPI or QEMU debug exit.
 *
 * Tries, in order:
 * 1. ACPI FADT PM1a control block with the S5 sleep type (real hardware)
 * 2. QEMU isa-debug-exit device (port 0x604)
 * 3. Legacy ACPI PM1a_CNT (port 0xB004, common on BIOS/QEMU)
 * 4. Fallback to keyboard reset (reboot)
 */
long arch_poweroff(void) {
	/* Method 1: Real ACPI power off via FADT */
	uintptr_t rsdp_addr = acpi_find_rsdp();
	if (rsdp_addr) {
		struct rsdp_descriptor * rsdp = (void*)mmu_map_from_physical(rsdp_addr);
		/* RSDP v1 checksum covers the first 20 bytes */
		uint8_t rsdp_sum = 0;
		uint8_t * rsdp_bytes = (uint8_t *)rsdp;
		for (int i = 0; i < 20; ++i) rsdp_sum += rsdp_bytes[i];
		if (rsdp_sum == 0) {
			struct rsdt * rsdt = (void*)mmu_map_from_physical(rsdp->rsdt_address);
			if (rsdt && acpi_checksum(&rsdt->header)) {
				uint32_t entries = (rsdt->header.length - sizeof(struct acpi_sdt_header)) / 4;
				for (uint32_t i = 0; i < entries; ++i) {
					uint8_t * tbl = mmu_map_from_physical(rsdt->pointers[i]);
					if (tbl[0] == 'F' && tbl[1] == 'A' && tbl[2] == 'C' && tbl[3] == 'P') {
						struct fadt * fadt = (void*)tbl;
						/* Locate the DSDT to read the _S5 sleep type */
						int slp_typ = -1;
						if (fadt->dsdt) {
							uint8_t * dsdt = mmu_map_from_physical(fadt->dsdt);
							if (dsdt && acpi_checksum((struct acpi_sdt_header *)dsdt)) {
								slp_typ = acpi_find_s5_slptyp(dsdt,
									((struct acpi_sdt_header *)dsdt)->length);
							}
						}
						if (slp_typ < 0) slp_typ = 0; /* default S5 SLP_TYP */

						if (fadt->pm1a_control_block) {
							uint16_t val = (slp_typ << 10) | (1 << 13); /* SLP_TYP + SLP_EN */
							outports(fadt->pm1a_control_block, val);
						}
						if (fadt->pm1b_control_block) {
							uint16_t val = (slp_typ << 10) | (1 << 13);
							outports(fadt->pm1b_control_block, val);
						}
						/* Give the hardware a moment; if it didn't work we fall through */
						for (volatile int d = 0; d < 1000000; ++d) {}
					}
				}
			}
		}
	}

	/* Method 2: QEMU isa-debug-exit (port 0x604) */
	outports(0x604, 0x2000);

	/* Method 3: Legacy ACPI PM1 control (port 0xB004) - SLP_TYP=0, SLP_EN=1 */
	outports(0xB004, 0x2000);

	/* Method 4: If nothing worked, at least reboot */
	return arch_reboot();
}

/* Syscall parameter accessors */
void arch_syscall_return(struct regs * r, long retval) { r->rax = retval; }
long arch_syscall_number(struct regs * r) { return (unsigned long)r->rax; }
long arch_syscall_arg0(struct regs * r) { return r->rdi; }
long arch_syscall_arg1(struct regs * r) { return r->rsi; }
long arch_syscall_arg2(struct regs * r) { return r->rdx; }
long arch_syscall_arg3(struct regs * r) { return r->r10; }
long arch_syscall_arg4(struct regs * r) { return r->r8; }
long arch_syscall_arg5(struct regs * r) { return r->r9; }
long arch_stack_pointer(struct regs * r) { return r->rsp; }
long arch_user_ip(struct regs * r) { return r->rip; }
