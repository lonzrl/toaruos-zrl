#pragma once

#include <kernel/types.h>

struct rsdp_descriptor {
	char     signature[8];
	uint8_t  checksum;
	char     oemid[6];
	uint8_t  revision;
	uint32_t rsdt_address;
} __attribute__((packed));

struct rsdp_descriptor_20 {
	struct rsdp_descriptor base;

	uint32_t length;
	uint64_t xsdt_address;
	uint8_t  ext_checksum;
	uint8_t  _reserved[3];
} __attribute((packed));

struct acpi_sdt_header {
	char     signature[4];
	uint32_t length;
	uint8_t  revision;
	uint8_t  checksum;
	char     oemid[6];
	char     oem_tableid[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed));

struct rsdt {
	struct acpi_sdt_header header;
	uint32_t pointers[];
};

struct madt {
	struct acpi_sdt_header header;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t entries[];
};

/* Fixed ACPI Description Table (FADT / FACP).
 * Only the fields relevant to power management are described here. */
struct fadt {
	struct acpi_sdt_header header;
	uint32_t firmware_ctrl;       /* FACS */
	uint32_t dsdt;                /* DSDT (physical address) */
	uint8_t  reserved1;
	uint8_t  preferred_pm_profile;
	uint16_t sci_interrupt;
	uint32_t smi_command;
	uint8_t  acpi_enable;
	uint8_t  acpi_disable;
	uint8_t  s4bios_req;
	uint8_t  pstate_control;
	uint32_t pm1a_event_block;
	uint32_t pm1b_event_block;
	uint32_t pm1a_control_block;  /* offset 64 */
	uint32_t pm1b_control_block;  /* offset 68 */
	uint32_t pm2_control_block;
	uint32_t pm_timer_block;
	uint32_t gpe0_block;
	uint32_t gpe1_block;
	uint8_t  pm1_event_length;
	uint8_t  pm1_control_length;
	uint8_t  pm2_control_length;
	uint8_t  pm_timer_length;
	uint8_t  gpe0_length;
	uint8_t  gpe1_length;
	uint8_t  gpe1_base;
	uint8_t  cstate_control;
	uint16_t worst_c2_latency;
	uint16_t worst_c3_latency;
	uint16_t flush_size;
	uint16_t flush_stride;
	uint8_t  duty_offset;
	uint8_t  duty_width;
	uint8_t  day_alarm;
	uint8_t  month_alarm;
	uint8_t  century;
	uint16_t boot_architecture_flags;
	uint8_t  reserved2;
	uint32_t flags;
} __attribute__((packed));

static inline int acpi_checksum(struct acpi_sdt_header * header) {
	uint8_t check = 0;
	for (size_t i = 0; i < header->length; ++i) {
		check += ((uint8_t *)header)[i];
	}
	return check == 0;
}

