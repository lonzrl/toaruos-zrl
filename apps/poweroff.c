/**
 * @brief Power off the system.
 *
 * In QEMU with -no-reboot, rebooting will cause
 * QEMU to exit, effectively shutting down the VM.
 * For real hardware, this triggers a reboot as
 * the kernel does not have ACPI shutdown support.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/reboot.h>

int main(int argc, char ** argv) {
	fprintf(stderr, "\xe7\xb3\xbb\xe7\xbb\x9f\xe6\xad\xa3\xe5\x9c\xa8\xe5\x85\xb3\xe6\x9c\xba...\n");
	if (reboot(0) < 0) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
	}
	return 1;
}
