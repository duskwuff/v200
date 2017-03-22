#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_keycode.h>

#include "m68k.h"

#define RAM_SIZE    (256 * 1024)
#define FLASH_SIZE  (4 * 1024 * 1024)

#define RAM_BASE    0x000000
#define FLASH_BASE  0x200000

#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   128

#define SCREEN_PADDING  8
#define SCREEN_SCALE    2

/* 12 MHz = 12k cycles / 1 ms */
#define CYCLES_PER_TICK 12000

/* 40 Hz = 25 ms / frame */
#define FRAME_TICKS     25

#define FRAME_CYCLES    (FRAME_TICKS * CYCLES_PER_TICK)

uint8_t io[32];
void *ti_ram = NULL, *ti_flash = NULL;

uint8_t keyboard_state[81] = {0};
uint8_t keyboard_touched = 0;

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

uint8_t io_getkbd(void)
{
    uint16_t mask = (io[0x18] << 8) | io[0x19];
    uint8_t result = 0;
    for (int row = 0; row < 10; row++) {
        if (!(mask & (1 << row))) {
            for (int col = 0; col < 8; col++) {
                if (keyboard_state[row * 8 + col])
                    result |= 1 << (7 - col);
            }
        }
    }
    return ~result;
}

uint8_t io_read8(uint32_t addr)
{
    addr &= 0x1f;

    uint8_t val = io[addr];
    switch (addr) {
        case 0x00:
            val |= 4;
            break;
        case 0x1b:
            val = io_getkbd();
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

int sdl_to_ti_kbd(SDL_Keycode key)
{
    switch (key) {
        case SDLK_DOWN:     return 0;
        case SDLK_RIGHT:    return 1;
        case SDLK_UP:       return 2;
        case SDLK_LEFT:     return 3;
                            // hand = 4???
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:   return 5; // shift
        case SDLK_LALT:
        case SDLK_RALT:     return 6; // diamond
        case SDLK_LCTRL:
        case SDLK_RCTRL:    return 7; // 2nd

        case SDLK_3:        return 8;
        case SDLK_2:        return 9;
        case SDLK_1:        return 10;
        case SDLK_F8:       return 11;
        case SDLK_w:        return 12;
        case SDLK_s:        return 13;
        case SDLK_z:        return 14;
                            // no key @ 15

        case SDLK_6:        return 16;
        case SDLK_5:        return 17;
        case SDLK_4:        return 18;
        case SDLK_F3:       return 19;
        case SDLK_e:        return 20;
        case SDLK_d:        return 21;
        case SDLK_x:        return 22;
                            // no key @ 23

        case SDLK_9:        return 24;
        case SDLK_8:        return 25;
        case SDLK_7:        return 26;
        case SDLK_F7:       return 27;
        case SDLK_r:        return 28;
        case SDLK_f:        return 29;
        case SDLK_c:        return 30;
        case SDLK_BACKSLASH: return 31; // store

        case SDLK_COMMA:    return 32;
        case SDLK_RIGHTBRACKET: return 33; // paren right
        case SDLK_LEFTBRACKET:  return 34; // paren left
        case SDLK_F2:       return 35;
        case SDLK_t:        return 36;
        case SDLK_g:        return 37;
        case SDLK_v:        return 38;
        case SDLK_SPACE:    return 39;

                            // tan = 40
                            // cos = 41
                            // sin = 42
        case SDLK_F6:       return 43;
        case SDLK_y:        return 44;
        case SDLK_h:        return 45;
        case SDLK_b:        return 46;
        case SDLK_KP_DIVIDE: return 47;

        case SDLK_p:        return 48;
        case SDLK_KP_ENTER: return 49;
                            // ln = 50
        case SDLK_F1:       return 51;
        case SDLK_u:        return 52;
        case SDLK_j:        return 53;
        case SDLK_n:        return 54;
                            // ^ = 55

        case SDLK_KP_MULTIPLY: return 56;
        case SDLK_INSERT:   return 57; // apps
        case SDLK_DELETE:   return 58; // clear
        case SDLK_F5:       return 59;
        case SDLK_i:        return 60;
        case SDLK_k:        return 61;
        case SDLK_m:        return 62;
        case SDLK_EQUALS:   return 63;

                            // no key @ 64
        case SDLK_ESCAPE:   return 65;
                            // mode = 66
        case SDLK_KP_PLUS:  return 67;
        case SDLK_o:        return 68;
        case SDLK_l:        return 69;
        case SDLK_SLASH:    return 70; // theta
        case SDLK_BACKSPACE: return 71;

                            // negate = 72
        case SDLK_PERIOD:   return 73;
        case SDLK_0:        return 74;
        case SDLK_F4:       return 75;
        case SDLK_q:        return 76;
        case SDLK_a:        return 77;
        case SDLK_RETURN:   return 78;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: return 79;

        default:            return -1;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
                "Usage (for now):\n"
                "  v200 <os.v2u>\n"
               );
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

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
            "v200",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            SCREEN_WIDTH * SCREEN_SCALE + SCREEN_PADDING * 2,
            SCREEN_HEIGHT * SCREEN_SCALE + SCREEN_PADDING * 2,
            SDL_WINDOW_SHOWN
            );
    SDL_Surface *window_surface = SDL_GetWindowSurface(window);

    SDL_Rect dstrect = {
        .x = SCREEN_PADDING,
        .y = SCREEN_PADDING,
        .w = SCREEN_WIDTH * SCREEN_SCALE,
        .h = SCREEN_HEIGHT * SCREEN_SCALE,
    };

    SDL_Surface *screen_surface = SDL_CreateRGBSurface(
            0,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            32,
            0, 0, 0, 0
            );

    uint32_t white = SDL_MapRGBA(screen_surface->format, 255, 255, 255, 255);
    uint32_t black = SDL_MapRGBA(screen_surface->format, 0,   0,   0,   255);
    uint32_t gray  = SDL_MapRGBA(screen_surface->format, 128, 128, 128, 255);

    uint32_t last_tick = SDL_GetTicks();

    for (;;) {
        uint32_t next_tick = last_tick + FRAME_TICKS;

        int n = m68k_execute(FRAME_CYCLES);
        if (n == 0)
            break; // ???

        SDL_LockSurface(screen_surface);
        {

            uint8_t  *src = ti_ram + 0x4c00;
            uint32_t *dst = screen_surface->pixels;

            for (int i = 0; i < SCREEN_HEIGHT; i++) {
                for (int j = 0; j < SCREEN_WIDTH; j += 8) {
                    uint8_t b = *src++;
                    for (int k = 0; k < 8; k++) {
                        *dst++ = (b & 0x80) ? black : white;
                        b <<= 1;
                    }
                }
            }
        }
        SDL_UnlockSurface(screen_surface);

        SDL_FillRect(window_surface, NULL, white);
        SDL_BlitScaled(screen_surface, NULL, window_surface, &dstrect);
        SDL_UpdateWindowSurface(window);

        uint32_t now_tick = SDL_GetTicks();
        do {
            uint32_t wait_ticks = next_tick - now_tick;
            if (wait_ticks < 5) wait_ticks = 5;

            SDL_Event ev;
            if (SDL_WaitEventTimeout(&ev, wait_ticks)) {
                int key;
                switch (ev.type) {
                    case SDL_QUIT:
                        return 0;
                    case SDL_KEYDOWN:
                    case SDL_KEYUP:
                        key = sdl_to_ti_kbd(ev.key.keysym.sym);
                        if (key >= 0) {
                            keyboard_state[key] = (ev.key.state == SDL_PRESSED);
                        }
                        break;
                }
            }

            now_tick = SDL_GetTicks();
        } while (now_tick < next_tick);

        last_tick = now_tick;

        // FIXME: This is a hack. Need to implement real timers.
        static int i = 0;
        if (i++ > 30) {
            m68k_set_irq(1);
        }
    }

    return 0;
}
