// Copyright 2024 Synaptics Incorporated
// All rights reserved.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

//#define __debug__
#define BOOTCTRL_MAGIC 0x42424100
#define BOOTCTRL_SUFFIX_A "_a"
#define BOOTCTRL_SUFFIX_B "_b"
#define BOOT_CONTROL_VERSION 1
#define MISC_BOOT_CONTROL_VERSION 2
#define COMMAND_LINE_SIZE 2048

static const char *misc_device = "/dev/disk/by-partlabel/misc";
static const size_t SYNA_SPACE_OFFSET_IN_MISC = 4 * 1024;
static const int kActiveSlotPriority = 8;
static const int kOtherSlotPriority = 3;

typedef struct slot_metadata {
	uint8_t priority : 4;
	uint8_t tries_remaining : 3;
	uint8_t successful_boot : 1;
} slot_metadata_t;

typedef struct boot_ctrl {
	uint32_t magic;			// Magic for identification - '\0ABB' (Boot Contrl Magic)
	uint8_t version; 		// boot ctrl version
	slot_metadata_t slot_info[2]; 	// Information about each slot
	uint8_t slot_a_verity_corrupted:4;
	uint8_t slot_b_verity_corrupted:4;
} boot_ctrl_t;

typedef struct misc_slot_metadata {
	uint8_t priority;
	uint8_t tries_remaining;
	// 1 if this slot has booted successfully, 0 otherwise.
	uint8_t successful_boot;
	// 1 if this slot is corrupted from a dm-verity corruption, 0 otherwise
	uint8_t verity_corrupted;
	uint8_t reserved;
} misc_slot_metadata_t;

typedef struct misc_boot_ctrl {
	uint32_t magic;
	uint8_t version;
	uint8_t recovery_tries_remaining;
	misc_slot_metadata_t slot_info[2];
	uint8_t reserved[16];
} misc_boot_ctrl_t;

void print_log(void *p)
{
	misc_boot_ctrl_t *bootInfo = p;

	printf("bootinfo: magic=0x%x, version=%d, recovery_tries_remaining=%d\n",
			bootInfo->magic, bootInfo->version, bootInfo->recovery_tries_remaining);
	printf("slot0.priority=%d, slot0.tries=%d, slot0.successful=%d, slot0.verity=%d\n",
			bootInfo->slot_info[0].priority, bootInfo->slot_info[0].tries_remaining,
			bootInfo->slot_info[0].successful_boot, bootInfo->slot_info[0].verity_corrupted);
	printf("slot1.priority=%d, slot1.tries=%d, slot1.successful=%d, slot1.verity=%d\n",
			bootInfo->slot_info[1].priority, bootInfo->slot_info[1].tries_remaining,
			bootInfo->slot_info[1].successful_boot, bootInfo->slot_info[1].verity_corrupted);

}

int write_misc_partition(void *p, int size)
{
	int ret, fd;

	fd = open(misc_device, O_RDWR);
	if (fd < 0) {
		printf("failed to open misc device: %d\n", fd);
		return fd;
	}

	ret = lseek(fd, SYNA_SPACE_OFFSET_IN_MISC, SEEK_SET);
	if (ret < 0) {
		printf("failed to lseek misc device: %d\n", ret);
		close(fd);
		return ret;
	}

	ret = write(fd, p, size);
	if (ret < 0) {
		printf("failed to write misc device: %d\n", ret);
		close(fd);
		return ret;
	}

	close(fd);

#ifdef __debug__
	print_log(p);
#endif

	return 0;
}

int read_misc_partition(void *p, int size)
{
	int ret, fd;

	fd = open(misc_device, O_RDWR);
	if (fd < 0) {
		printf("failed to open misc device: %d\n", fd);
		return fd;
	}

	ret = lseek(fd, SYNA_SPACE_OFFSET_IN_MISC, SEEK_SET);
	if (ret < 0) {
		printf("failed to lseek misc device: %d\n", ret);
		close(fd);
		return ret;
	}

	ret = read(fd, p, size);
	if (ret < 0) {
		printf("failed to read misc device: %d\n", ret);
		close(fd);
		return ret;
	}

	close(fd);

#ifdef __debug__
	print_log(p);
#endif

	return 0;
}

int init_misc_info()
{
	misc_boot_ctrl_t bootInfo;

	bootInfo.magic = BOOTCTRL_MAGIC;
	bootInfo.version = MISC_BOOT_CONTROL_VERSION;
	bootInfo.slot_info[0].priority = kActiveSlotPriority;
	bootInfo.slot_info[0].tries_remaining = 2;
	bootInfo.slot_info[0].successful_boot = 0;
	bootInfo.slot_info[0].verity_corrupted = 0;
	bootInfo.slot_info[1].priority = kOtherSlotPriority;
	bootInfo.slot_info[1].tries_remaining = 2;
	bootInfo.slot_info[1].successful_boot = 0;
	bootInfo.slot_info[1].verity_corrupted = 0;
	bootInfo.recovery_tries_remaining = 0;

	return write_misc_partition(&bootInfo, sizeof(misc_boot_ctrl_t));
}

int init()
{
	int ret;
	misc_boot_ctrl_t bootInfo;

	ret = read_misc_partition(&bootInfo, sizeof(misc_boot_ctrl_t));
	if (ret < 0) {
		printf("failed to get active boot slot: %d\n", ret);
		return ret;
	}

	if (bootInfo.magic != BOOTCTRL_MAGIC || bootInfo.version != MISC_BOOT_CONTROL_VERSION) {
		printf("misc boot info corrupted, init bootInfo", ret);
		ret = init_misc_info();
		if (ret < 0) {
			printf("failed to init misc info: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int get_active_boot_slot()
{
	int ret, val;
	misc_boot_ctrl_t bootInfo;

	ret = read_misc_partition(&bootInfo, sizeof(misc_boot_ctrl_t));
	if (ret < 0) {
		printf("failed to get active boot slot: %d\n", ret);
		return ret;
	}

	if (bootInfo.magic != BOOTCTRL_MAGIC || bootInfo.version != MISC_BOOT_CONTROL_VERSION) {
		printf("misc boot info corrupted");
		return -1;
	}

	val = (bootInfo.slot_info[0].priority >= bootInfo.slot_info[1].priority) ? 0 : 1;

	printf("the active boot slot is: %d\n", val);

	return val;
}

int set_active_boot_slot(int slot)
{
	int ret;
	misc_boot_ctrl_t bootInfo;

	if (slot > 1) {
		printf("error slot can be only 0 or 1\n");
		return -1;
	}

	ret = read_misc_partition(&bootInfo, sizeof(misc_boot_ctrl_t));
	if (ret < 0) {
		printf("failed to get active boot slot: %d\n", ret);
		return ret;
	}

	if (bootInfo.magic != BOOTCTRL_MAGIC || bootInfo.version != MISC_BOOT_CONTROL_VERSION) {
		printf("misc boot info corrupted");
		return -1;
	}

	bootInfo.slot_info[slot].priority = kActiveSlotPriority;
	bootInfo.slot_info[slot].tries_remaining = 2;
	bootInfo.slot_info[slot].successful_boot = 0;
	bootInfo.slot_info[slot].verity_corrupted = 0;
	bootInfo.slot_info[1 - slot].priority = kOtherSlotPriority;
	bootInfo.slot_info[1 - slot].verity_corrupted = 0;

	printf("set active boot slot: %d\n", slot);
	return write_misc_partition(&bootInfo, sizeof(misc_boot_ctrl_t));
}

static void usage(int status)
{
	fprintf(status ? stdout: stderr,
			"bootctl command: \n"
			"get-active-boot-slot			- Prints the SLOT to load on next boot. \n"
			"set-active-boot-slot SLOT		- On next boot, load and execute SLOT.\n"
			"\n"
			);

	exit(status);
}
int main(int argc, char *argv[])
{
	init();

	if (argc < 2)
		usage(1);

	if (strcmp(argv[1], "get-active-boot-slot") == 0) {
		return get_active_boot_slot();
	} else if (strcmp(argv[1], "set-active-boot-slot") == 0) {
		if (argc < 3)
			usage(1);
		return set_active_boot_slot(strtoul(argv[2], NULL, 10));
	} else {
		printf("please input paramenter\n");
	}

	return 0;
}
