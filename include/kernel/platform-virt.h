#ifndef KERNEL_PLATFORM
#define KERNEL_PLATFORM

/* memory map */
#define VIRT_DEBUG 0x0ull
#define VIRT_MROM 0x1000ull

#define VIRT_TEST 0x100000ull
#define VIRT_TEST_LEN 0x1000ull

#define VIRT_RTC 0x101000ull
#define VIRT_RTC_LEN 0x1000ull

#define VIRT_CLINT 0x2000000ull
#define VIRT_CLINT_LEN 0x10000ull

#define VIRT_PCIE_PIO 0x3000000ull

#define VIRT_PLIC 0xc000000ull
#define VIRT_PLIC_LEN 0x600000ull

#define VIRT_UART0 0x10000000ull
#define VIRT_UART0_LEN 0x100ull

#define VIRT_VIRTIO 0x10001000ull
#define VIRT_VIRTIO_LEN 0x8000ull

#define VIRT_FLASH 0x20000000ull
#define VIRT_PCIE_ECAM 0x30000000ull
#define VIRT_PCIE_MMIO 0x40000000ull

#define VIRT_DRAM 0x80000000ull

/* irq lines */
#define VIRT_PLIC_UART0 0xa

#endif

