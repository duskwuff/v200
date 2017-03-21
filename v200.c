#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "m68k.h"

#define RAM_SIZE    (256 * 1024)
#define FLASH_SIZE  (4 * 1024 * 1024)

#define RAM_BASE    0x000000
#define FLASH_BASE  0x200000

uint64_t cycles = 0;

uint8_t io[32];
void *ti_ram = NULL, *ti_flash = NULL;

//////////////////////////////////////////////////////////////////////////////

enum mem_bank {
    BANK_RAM,
    BANK_FLASH,
    BANK_IO,
    BANK_WTF
};

uint8_t flash_phase = 0x50;
int flash_write = 0;
int flash_ff = 0;

void flash_write16(uint16_t value, uint32_t addr)
{
    addr = (addr - FLASH_BASE) & (FLASH_SIZE - 1);

    if (flash_write > 0) {
        uint16_t *rom = ti_flash + addr;
        *rom &= value;
        flash_write = 0;
        flash_ff = 1;
    } else switch(value & 0xff) {
        case 0x10:
            if (flash_phase == 0x50) flash_write = 1;
            break;
        case 0x20:
            if (flash_phase == 0x50) flash_phase = 0x20;
            break;
        case 0x50:
            flash_phase = 0x50;
            break;
        case 0x90:
            flash_phase = 0x90;
            break;
        case 0xd0:
            if (flash_phase == 0x20) {
                memset(ti_flash + (addr & 0xff0000), 0xff, 65536);
                flash_phase = 0xd0;
                flash_ff = 1;
            }
            break;
        case 0xff:
            if (flash_phase == 0x50) {
                flash_ff = 0;
            }
            break;
    }
}

enum mem_bank mem_bank_for_addr(unsigned int addr)
{
    if (addr < 0x200000) return BANK_RAM;
    if (addr < 0x600000) return BANK_FLASH;
    if (addr < 0x800000) return BANK_IO;
    return BANK_WTF;
}

uint8_t read8(void *buf, int offset)
{
    uint8_t *p = buf + offset;
    return *p;
}

uint16_t read16(void *buf, int offset)
{
    uint16_t *p = buf + offset;
    return ntohs(*p);
}

void write8(void *buf, int offset, uint8_t value)
{
    uint8_t *p = buf + offset;
    *p = value;
}

void write16(void *buf, int offset, uint16_t value)
{
    uint16_t *p = buf + offset;
    *p = htons(value);
}

uint8_t io_read8(uint32_t addr)
{
    addr &= 0x1f;

    uint8_t val = io[addr];
    switch (addr) {
        case 0:
            val |= 4;
            break;
    }
    return val;
}

void io_write8(uint32_t addr, uint8_t val)
{
    addr &= 0x1f;
    io[addr] = val;
}

unsigned int m68k_read_memory_8(unsigned int addr)
{
    switch (mem_bank_for_addr(addr)) {
        case BANK_RAM:
            return read8(ti_ram, (addr - RAM_BASE) % RAM_SIZE);
        case BANK_FLASH:
            if (flash_ff)
                return 0xff;
            else
                return read8(ti_flash, (addr - FLASH_BASE) % FLASH_SIZE);
        case BANK_IO:
            return io_read8(addr);
        case BANK_WTF:
            return 0;
    }
}

unsigned int m68k_read_memory_16(unsigned int addr)
{
    switch (mem_bank_for_addr(addr)) {
        case BANK_RAM:
            return read16(ti_ram, (addr - RAM_BASE) % RAM_SIZE);
        case BANK_FLASH:
            if (flash_ff)
                return 0xffff;
            else
                return read16(ti_flash, (addr - FLASH_BASE) % FLASH_SIZE);
        case BANK_IO:
            return (io_read8(addr) << 16) | io_read8(addr + 1);
        case BANK_WTF:
            printf("Unhandled weird read @ %08x\n", addr);
            return 0;
    }
}

void m68k_write_memory_8(unsigned int addr, unsigned int value)
{
    switch(mem_bank_for_addr(addr)) {
        case BANK_RAM:
            write8(ti_ram, (addr - RAM_BASE) % RAM_SIZE, value);
            break;
        case BANK_FLASH:
            printf("FLASH BYTE WRITE: %02x @ %04x (?!)\n", value, addr);
            break;
        case BANK_IO:
            io_write8(addr, value);
            break;
        case BANK_WTF:
            printf("Unhandled weird write: %02x -> %08x\n", value, addr);
            break;
    }
}

void m68k_write_memory_16(unsigned int addr, unsigned int value)
{
    switch(mem_bank_for_addr(addr)) {
        case BANK_RAM:
            write16(ti_ram, (addr - RAM_BASE) % RAM_SIZE, value);
            break;
        case BANK_FLASH:
            flash_write16(value, addr);
            break;
        case BANK_IO:
            io_write8(addr + 0, value >> 8);
            io_write8(addr + 1, value & 0xff);
            break;
        case BANK_WTF:
            printf("Unhandled weird write @ %04x -> %08x\n", value, addr);
            break;
    }
}

unsigned int m68k_read_memory_32(unsigned int addr)
{
    return (m68k_read_memory_16(addr) << 16) | m68k_read_memory_16(addr + 2);
}

void m68k_write_memory_32(unsigned int addr, unsigned int value)
{
    m68k_write_memory_16(addr + 0, (value >> 16) & 0xffff);
    m68k_write_memory_16(addr + 2, (value >>  0) & 0xffff);
}

unsigned int m68k_read_disassembler_16(unsigned int addr)
{
    return m68k_read_memory_16(addr);
}

unsigned int m68k_read_disassembler_32(unsigned int addr)
{
    return m68k_read_memory_32(addr);
}

//////////////////////////////////////////////////////////////////////////////

void dump_screen(void)
{
    FILE *fh = fopen("screen.pbm", "w");
    if (!fh) {
        perror("dump_screen");
        return;
    }
    fprintf(fh, "P4\n240 128\n");
    fwrite(ti_ram + 0x4c00, 1, 240 * 128 / 8, fh);
    fclose(fh);
}

void dump_memory(void)
{
    FILE *fh = fopen("memory.bin", "w");
    if (!fh) {
        perror("dump_memory");
        return;
    }
    fwrite(ti_ram, 1, RAM_SIZE, fh);
    fclose(fh);
}

void dump_flash(void)
{
    FILE *fh = fopen("flash.bin", "w");
    if (!fh) {
        perror("dump_flash");
        return;
    }
    fwrite(ti_flash, 1, FLASH_SIZE, fh);
    fclose(fh);
}

void cpu_whereami(void)
{
    printf("D0 = %08x | D1 = %08x | D2 = %08x | D3 = %08x\n",
            m68k_get_reg(NULL, M68K_REG_D0),
            m68k_get_reg(NULL, M68K_REG_D1),
            m68k_get_reg(NULL, M68K_REG_D2),
            m68k_get_reg(NULL, M68K_REG_D3));
    printf("D4 = %08x | D5 = %08x | D6 = %08x | D7 = %08x\n",
            m68k_get_reg(NULL, M68K_REG_D4),
            m68k_get_reg(NULL, M68K_REG_D5),
            m68k_get_reg(NULL, M68K_REG_D6),
            m68k_get_reg(NULL, M68K_REG_D7));
    printf("A0 = %08x | A1 = %08x | A2 = %08x | A3 = %08x\n",
            m68k_get_reg(NULL, M68K_REG_A0),
            m68k_get_reg(NULL, M68K_REG_A1),
            m68k_get_reg(NULL, M68K_REG_A2),
            m68k_get_reg(NULL, M68K_REG_A3));
    printf("A4 = %08x | A5 = %08x | A6 = %08x | A7 = %08x\n",
            m68k_get_reg(NULL, M68K_REG_A4),
            m68k_get_reg(NULL, M68K_REG_A5),
            m68k_get_reg(NULL, M68K_REG_A6),
            m68k_get_reg(NULL, M68K_REG_A7));
    printf("PC = %08x | SR = %08x\n",
            m68k_get_reg(NULL, M68K_REG_PC),
            m68k_get_reg(NULL, M68K_REG_SR));
}

//////////////////////////////////////////////////////////////////////////////

void read_rom(const char *path)
{
    FILE *fh = fopen(path, "r");
    if (!fh) {
        perror(path);
        exit(1);
    }

    uint8_t header[78];
    if (fread(header, sizeof(header), 1, fh) != 1) {
        fprintf(stderr, "Couldn't read v2u header\n");
        exit(1);
    }

    if (memcmp(&header[0], "**TIFL**", 8)) {
        fprintf(stderr, "Invalid flash header\n");
        exit(1);
    }

    uint32_t image_len = *((uint32_t *) &header[74]);
    if ((image_len & 0xff000000) || (image_len + 0x12000 > FLASH_SIZE)) {
        fprintf(stderr, "Unreasonable flash size (got %04x)\n", image_len);
        exit(1);
    }

    memset(ti_flash, 0xff, FLASH_SIZE);

    if (fread(ti_flash + 0x12000, image_len, 1, fh) != 1) {
        fprintf(stderr, "Couldn't read flash image\n");
        exit(1);
    }

    // Copy boot code
    memcpy(ti_flash, ti_flash + 0x12088, 256);

    // FIXME: Set up hardware param block @ FLASH+0x100
    // The calculator seems to boot without, but it's probably not happy
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
                "Usage (for now):\n"
                "  v200 <os.v2u> <cycle count>\n"
               );
        return 1;
    }

    int64_t stop_after_cycles = strtoll(argv[2], NULL, 10);
    if (stop_after_cycles <= 0) {
        fprintf(stderr, "Unreasonable looking cycle count\n");
        return 1;
    }

    ti_ram = malloc(RAM_SIZE);
    ti_flash = malloc(FLASH_SIZE);

    read_rom(argv[1]);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    m68k_set_reg(M68K_REG_SP, m68k_read_memory_32(FLASH_BASE + 0));
    m68k_set_reg(M68K_REG_PC, m68k_read_memory_32(FLASH_BASE + 4));

    for (;;) {
        int n = m68k_execute(10000);
        if (n == 0)
            break;
        cycles += n;
        if (cycles > stop_after_cycles)
            break;
    }

    cpu_whereami();
    dump_screen();

    return 0;
}
