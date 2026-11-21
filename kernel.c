#include "font8x16.h"

/* =========================================================================
 * 1. DEFINIÇÕES DO FRAMEBUFFER LINEAR VBE & HARDWARE
 * ========================================================================= */
struct __attribute__((packed)) vbe_mode_info {
    unsigned short mode_attributes;     /* 00 */
    unsigned char  win_a_attributes;    /* 02 */
    unsigned char  win_b_attributes;    /* 03 */
    unsigned short win_granularity;     /* 04 */
    unsigned short win_size;            /* 06 */
    unsigned short win_a_segment;       /* 08 */
    unsigned short win_b_segment;       /* 0A */
    unsigned int   win_func_ptr;        /* 0C */
    unsigned short bytes_per_scanline;  /* 10 */
    unsigned short x_resolution;        /* 12 */
    unsigned short y_resolution;        /* 14 */
    unsigned char  x_char_size;         /* 16 */
    unsigned char  y_char_size;         /* 17 */
    unsigned char  number_of_planes;    /* 18 */
    unsigned char  bits_per_pixel;      /* 19 */
    unsigned char  number_of_banks;     /* 1A */
    unsigned char  memory_model;        /* 1B */
    unsigned char  bank_size;           /* 1C */
    unsigned char  number_of_image_pages; /* 1D */
    unsigned char  reserved0;           /* 1E */
    unsigned char  red_mask_size;       /* 1F */
    unsigned char  red_field_position;  /* 20 */
    unsigned char  green_mask_size;     /* 21 */
    unsigned char  green_field_position;/* 22 */
    unsigned char  blue_mask_size;      /* 23 */
    unsigned char  blue_field_position; /* 24 */
    unsigned char  rsvd_mask_size;      /* 25 */
    unsigned char  rsvd_field_position; /* 26 */
    unsigned char  direct_color_mode_info; /* 27 */
    unsigned int   physbase;            /* 28 */
};

#define VBE_INFO_ADDR 0x0500

/* Backbuffer em RAM rápida (4MB alocados em 0x200000) para eliminar 100% do lag de FPS! */
#define BACKBUFFER_ADDR 0x00200000
static unsigned int* backbuffer = (unsigned int*)BACKBUFFER_ADDR;

static volatile unsigned char* fb_bytes = (volatile unsigned char*)0xFD000000;
static int screen_w = 1024;
static int screen_h = 768;
static int screen_bpp = 32;
static int bytes_per_line = 4096;

/* Modo de desenho: 0 = escreve direto na VRAM + backbuffer (Console), 1 = escreve no Backbuffer (Compositor GUI) */
static int g_draw_to_backbuffer_only = 0;

/* Cores base para o console */
#define COLOR_TERM_BG       0x00000000
#define COLOR_TERM_FG       0x0000FF7F
#define COLOR_TERM_PROMPT   0x0033CCFF
#define COLOR_TERM_WHITE    0x00EEEEEE
#define COLOR_TERM_YELLOW   0x00FFD166
#define COLOR_TERM_RED      0x00FF5555

/* =========================================================================
 * 2. ESTADO DO TERMINAL, HISTÓRICO, NOTAS E MOUSE
 * ========================================================================= */
static int font_scale = 1;

#define MAX_TERM_ROWS 60
#define MAX_TERM_COLS 130

static char term_buffer[MAX_TERM_ROWS][MAX_TERM_COLS];
static unsigned int term_fg_buffer[MAX_TERM_ROWS][MAX_TERM_COLS];

static int term_cols = 128;
static int term_rows = 48;
static int term_cur_x = 0;
static int term_cur_y = 0;
static int gui_active = 0;

#define CMD_BUFFER_SIZE 128
static char cmd_buffer[CMD_BUFFER_SIZE];
static int cmd_index = 0;

/* Histórico de comandos do terminal (Setas Cima / Baixo) */
#define MAX_HISTORY 16
static char cmd_history[MAX_HISTORY][CMD_BUFFER_SIZE];
static int history_count = 0;
static int history_view_idx = -1;

/* Estado do mouse e modificadores de teclado */
static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_left = 0;
static int mouse_right = 0;
static unsigned int mouse_bg_backup[24 * 24];
static int mouse_backup_x = -1;
static int mouse_backup_y = -1;
static int shift_pressed = 0;
static int extended_scancode = 0;
static unsigned int kernel_frames_rendered = 1;

/* =========================================================================
 * 3. PORTAS DE I/O E UTILITÁRIOS DE STRING / NÚMEROS
 * ========================================================================= */
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int strlen_k(const char* s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

void strcpy_k(char* dst, const char* src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void strcat_k(char* dst, const char* src, int max_len) {
    int dlen = strlen_k(dst);
    int i = 0;
    while (src[i] != '\0' && (dlen + i) < max_len - 1) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
}

int str_contains_k(const char* haystack, const char* needle) {
    if (needle[0] == '\0') return 1;
    for (int i = 0; haystack[i] != '\0'; i++) {
        int j = 0;
        while (needle[j] != '\0' && haystack[i + j] == needle[j]) {
            j++;
        }
        if (needle[j] == '\0') return 1;
    }
    return 0;
}

void int_to_str(int val, char* buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int neg = 0;
    unsigned int uval;
    if (val < 0) {
        neg = 1;
        uval = (unsigned int)(-val);
    } else {
        uval = (unsigned int)val;
    }
    char tmp[16];
    int i = 0;
    while (uval > 0) {
        tmp[i++] = '0' + (uval % 10);
        uval /= 10;
    }
    int out = 0;
    if (neg) buf[out++] = '-';
    while (i > 0) {
        buf[out++] = tmp[--i];
    }
    buf[out] = '\0';
}

/* =========================================================================
 * 4. PRIMITIVAS GRÁFICAS DE ALTA PERFORMANCE (BACKBUFFER + BLIT)
 * ========================================================================= */
static inline void vram_put_pixel(int x, int y, unsigned int color) {
    if (screen_bpp == 32) {
        volatile unsigned int* p = (volatile unsigned int*)(fb_bytes + (y * bytes_per_line) + (x * 4));
        *p = color;
    } else if (screen_bpp == 24) {
        int offset = y * bytes_per_line + x * 3;
        fb_bytes[offset]     = (unsigned char)(color & 0xFF);
        fb_bytes[offset + 1] = (unsigned char)((color >> 8) & 0xFF);
        fb_bytes[offset + 2] = (unsigned char)((color >> 16) & 0xFF);
    }
}

void put_pixel(int x, int y, unsigned int color) {
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return;
    backbuffer[y * screen_w + x] = color;
    if (!g_draw_to_backbuffer_only) {
        vram_put_pixel(x, y, color);
    }
}

unsigned int get_pixel(int x, int y) {
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return 0;
    return backbuffer[y * screen_w + x];
}

void fill_rect(int rx, int ry, int rw, int rh, unsigned int color) {
    int x0 = (rx < 0) ? 0 : rx;
    int y0 = (ry < 0) ? 0 : ry;
    int x1 = (rx + rw > screen_w) ? screen_w : (rx + rw);
    int y1 = (ry + rh > screen_h) ? screen_h : (ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

    if (g_draw_to_backbuffer_only) {
        for (int y = y0; y < y1; y++) {
            unsigned int* row_ptr = &backbuffer[y * screen_w + x0];
            int count = x1 - x0;
            for (int i = 0; i < count; i++) {
                row_ptr[i] = color;
            }
        }
    } else {
        for (int y = y0; y < y1; y++) {
            unsigned int* row_ptr = &backbuffer[y * screen_w + x0];
            for (int x = x0; x < x1; x++) {
                *row_ptr++ = color;
                vram_put_pixel(x, y, color);
            }
        }
    }
}

/* Copia o backbuffer inteiro para o Linear Framebuffer VBE de uma só vez (Zero Flicker!) */
void swap_buffers() {
    kernel_frames_rendered++;
    if (screen_bpp == 32 && bytes_per_line == screen_w * 4) {
        volatile unsigned int* dst = (volatile unsigned int*)fb_bytes;
        unsigned int* src = backbuffer;
        int total_pixels = screen_w * screen_h;
        for (int i = 0; i < total_pixels; i++) {
            dst[i] = src[i];
        }
    } else if (screen_bpp == 32) {
        for (int y = 0; y < screen_h; y++) {
            volatile unsigned int* dst = (volatile unsigned int*)(fb_bytes + y * bytes_per_line);
            unsigned int* src = &backbuffer[y * screen_w];
            for (int x = 0; x < screen_w; x++) {
                dst[x] = src[x];
            }
        }
    } else {
        for (int y = 0; y < screen_h; y++) {
            int offset = y * bytes_per_line;
            unsigned int* src = &backbuffer[y * screen_w];
            for (int x = 0; x < screen_w; x++) {
                unsigned int c = src[x];
                fb_bytes[offset]     = (unsigned char)(c & 0xFF);
                fb_bytes[offset + 1] = (unsigned char)((c >> 8) & 0xFF);
                fb_bytes[offset + 2] = (unsigned char)((c >> 16) & 0xFF);
                offset += 3;
            }
        }
    }
}

void draw_rect(int rx, int ry, int rw, int rh, unsigned int color) {
    if (rw <= 0 || rh <= 0) return;
    fill_rect(rx, ry, rw, 1, color);
    fill_rect(rx, ry + rh - 1, rw, 1, color);
    fill_rect(rx, ry, 1, rh, color);
    fill_rect(rx + rw - 1, ry, 1, rh, color);
}

void draw_char_scaled(int x, int y, char c, unsigned int fg, unsigned int bg, int opaque, int scale) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) uc = '?';
    const unsigned char* glyph = font8x16[uc];

    if (scale == 1) {
        for (int row = 0; row < 16; row++) {
            int py = y + row;
            if (py < 0 || py >= screen_h) continue;
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                int px = x + col;
                if (px < 0 || px >= screen_w) continue;
                if (bits & (0x80 >> col)) {
                    put_pixel(px, py, fg);
                } else if (opaque) {
                    put_pixel(px, py, bg);
                }
            }
        }
    } else {
        for (int row = 0; row < 16; row++) {
            unsigned char bits = glyph[row];
            for (int s_y = 0; s_y < 2; s_y++) {
                int py = y + row * 2 + s_y;
                if (py < 0 || py >= screen_h) continue;
                for (int col = 0; col < 8; col++) {
                    int px1 = x + col * 2;
                    int px2 = px1 + 1;
                    if (bits & (0x80 >> col)) {
                        put_pixel(px1, py, fg);
                        put_pixel(px2, py, fg);
                    } else if (opaque) {
                        put_pixel(px1, py, bg);
                        put_pixel(px2, py, bg);
                    }
                }
            }
        }
    }
}

void draw_string_scaled(int x, int y, const char* str, unsigned int fg, unsigned int bg, int opaque, int scale) {
    int cur_x = x;
    int char_w = (scale == 2) ? 16 : 8;
    for (int i = 0; str[i] != '\0'; i++) {
        draw_char_scaled(cur_x, y, str[i], fg, bg, opaque, scale);
        cur_x += char_w;
    }
}

/* =========================================================================
 * 5. LEITOR CMOS RTC (RELÓGIO REAL EM HARDWARE)
 * ========================================================================= */
static unsigned char read_cmos(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd_to_bin(unsigned char val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

static void get_rtc_time_str(char* buf) {
    unsigned char sec  = read_cmos(0x00);
    unsigned char min  = read_cmos(0x02);
    unsigned char hr   = read_cmos(0x04);
    unsigned char regb = read_cmos(0x0B);

    if (!(regb & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = bcd_to_bin(hr);
    }

    buf[0] = 'R';
    buf[1] = 'T';
    buf[2] = 'C';
    buf[3] = ' ';
    buf[4] = '0' + (hr / 10) % 10;
    buf[5] = '0' + (hr % 10);
    buf[6] = ':';
    buf[7] = '0' + (min / 10) % 10;
    buf[8] = '0' + (min % 10);
    buf[9] = ':';
    buf[10] = '0' + (sec / 10) % 10;
    buf[11] = '0' + (sec % 10);
    buf[12] = '\0';
}

/* =========================================================================
 * 6. DRIVER DO TERMINAL E CURSOR DO MOUSE (COM RESTAURAÇÃO INSTANTÂNEA)
 * ========================================================================= */
static const unsigned char cursor_mask[16][16] = {
    {1,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0, 0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0, 0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0, 0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0, 0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0, 0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1, 0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,1,1, 1,0,0,0,0,0,0,0},
    {1,2,1,2,2,1,0,0, 0,0,0,0,0,0,0,0},
    {1,1,0,1,2,2,1,0, 0,0,0,0,0,0,0,0},
    {1,0,0,0,1,2,2,1, 0,0,0,0,0,0,0,0},
    {0,0,0,0,1,2,2,1, 0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,0, 0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
};

void restore_mouse_bg() {
    if (mouse_backup_x < 0) return;
    for (int dy = 0; dy < 16; dy++) {
        int py = mouse_backup_y + dy;
        if (py < 0 || py >= screen_h) continue;
        for (int dx = 0; dx < 16; dx++) {
            int px = mouse_backup_x + dx;
            if (px < 0 || px >= screen_w) continue;
            unsigned int col = mouse_bg_backup[dy * 16 + dx];
            backbuffer[py * screen_w + px] = col;
            vram_put_pixel(px, py, col);
        }
    }
}

void draw_mouse_cursor(int mx, int my) {
    mouse_backup_x = mx;
    mouse_backup_y = my;
    for (int dy = 0; dy < 16; dy++) {
        int py = my + dy;
        for (int dx = 0; dx < 16; dx++) {
            int px = mx + dx;
            mouse_bg_backup[dy * 16 + dx] = get_pixel(px, py);
        }
    }

    for (int dy = 0; dy < 16; dy++) {
        int py = my + dy;
        if (py < 0 || py >= screen_h) continue;
        for (int dx = 0; dx < 16; dx++) {
            int px = mx + dx;
            if (px < 0 || px >= screen_w) continue;
            unsigned char p = cursor_mask[dy][dx];
            if (p == 1) {
                backbuffer[py * screen_w + px] = 0x00000000;
                vram_put_pixel(px, py, 0x00000000);
            } else if (p == 2) {
                backbuffer[py * screen_w + px] = 0x00FFFFFF;
                vram_put_pixel(px, py, 0x00FFFFFF);
            }
        }
    }
}

void term_draw_cursor(int on) {
    if (gui_active) return;
    int cw = (font_scale == 2) ? 16 : 8;
    int ch = (font_scale == 2) ? 32 : 16;
    unsigned int color = on ? COLOR_TERM_FG : COLOR_TERM_BG;
    fill_rect(term_cur_x * cw, term_cur_y * ch + ch - 2, cw, 2, color);
}

void term_redraw_all() {
    if (gui_active) return;
    g_draw_to_backbuffer_only = 1;
    fill_rect(0, 0, screen_w, screen_h, COLOR_TERM_BG);
    int cw = (font_scale == 2) ? 16 : 8;
    int ch = (font_scale == 2) ? 32 : 16;

    for (int r = 0; r < term_rows; r++) {
        int py = r * ch;
        for (int c = 0; c < term_cols; c++) {
            char ch_val = term_buffer[r][c];
            if (ch_val != ' ' && ch_val != '\0') {
                draw_char_scaled(c * cw, py, ch_val, term_fg_buffer[r][c], COLOR_TERM_BG, 0, font_scale);
            }
        }
    }
    g_draw_to_backbuffer_only = 0;
    swap_buffers();
    term_draw_cursor(1);
}

void term_scroll() {
    for (int r = 0; r < term_rows - 1; r++) {
        for (int c = 0; c < term_cols; c++) {
            term_buffer[r][c] = term_buffer[r + 1][c];
            term_fg_buffer[r][c] = term_fg_buffer[r + 1][c];
        }
    }
    for (int c = 0; c < term_cols; c++) {
        term_buffer[term_rows - 1][c] = ' ';
        term_fg_buffer[term_rows - 1][c] = COLOR_TERM_WHITE;
    }
    term_cur_y = term_rows - 1;
    if (!gui_active) {
        term_redraw_all();
    }
}

void term_clear() {
    for (int r = 0; r < MAX_TERM_ROWS; r++) {
        for (int c = 0; c < MAX_TERM_COLS; c++) {
            term_buffer[r][c] = ' ';
            term_fg_buffer[r][c] = COLOR_TERM_WHITE;
        }
    }
    term_cur_x = 0;
    term_cur_y = 0;
    if (!gui_active) {
        g_draw_to_backbuffer_only = 1;
        fill_rect(0, 0, screen_w, screen_h, COLOR_TERM_BG);
        g_draw_to_backbuffer_only = 0;
        swap_buffers();
        term_draw_cursor(1);
    }
}

void term_put_char(char c, unsigned int color) {
    if (!gui_active) term_draw_cursor(0);
    int cw = (font_scale == 2) ? 16 : 8;
    int ch = (font_scale == 2) ? 32 : 16;

    if (c == '\n') {
        term_cur_x = 0;
        term_cur_y++;
        if (term_cur_y >= term_rows) {
            term_scroll();
        } else if (!gui_active) {
            term_draw_cursor(1);
        }
        return;
    }

    if (c == '\b') {
        if (term_cur_x > 0) {
            term_cur_x--;
            term_buffer[term_cur_y][term_cur_x] = ' ';
            if (!gui_active) {
                fill_rect(term_cur_x * cw, term_cur_y * ch, cw, ch, COLOR_TERM_BG);
            }
        }
        if (!gui_active) term_draw_cursor(1);
        return;
    }

    term_buffer[term_cur_y][term_cur_x] = c;
    term_fg_buffer[term_cur_y][term_cur_x] = color;
    if (!gui_active) {
        draw_char_scaled(term_cur_x * cw, term_cur_y * ch, c, color, COLOR_TERM_BG, 1, font_scale);
    }

    term_cur_x++;
    if (term_cur_x >= term_cols) {
        term_cur_x = 0;
        term_cur_y++;
        if (term_cur_y >= term_rows) {
            term_scroll();
            return;
        }
    }
    if (!gui_active) term_draw_cursor(1);
}

void term_print(const char* str, unsigned int color) {
    for (int i = 0; str[i] != '\0'; i++) {
        term_put_char(str[i], color);
    }
}

void term_print_int(int val, unsigned int color) {
    char buf[16];
    int_to_str(val, buf);
    term_print(buf, color);
}

/* =========================================================================
 * 7. SISTEMA DE ARQUIVOS EM MEMÓRIA (RAMFS) & EDITOR KAZNOTES
 * ========================================================================= */
#define MAX_VFS_FILES 16
#define MAX_VFS_NAME  32
#define MAX_VFS_DATA  512

struct VfsFile {
    int used;
    char name[MAX_VFS_NAME];
    char data[MAX_VFS_DATA];
};

static struct VfsFile vfs_files[MAX_VFS_FILES];
static char notes_buffer[MAX_VFS_DATA];
static int notes_len = 0;
static int notes_saved_status = 0;

void vfs_set_file(const char* name, const char* content) {
    for (int i = 0; i < MAX_VFS_FILES; i++) {
        if (vfs_files[i].used && strcmp(vfs_files[i].name, name) == 0) {
            strcpy_k(vfs_files[i].data, content, MAX_VFS_DATA);
            return;
        }
    }
    for (int i = 0; i < MAX_VFS_FILES; i++) {
        if (!vfs_files[i].used) {
            vfs_files[i].used = 1;
            strcpy_k(vfs_files[i].name, name, MAX_VFS_NAME);
            strcpy_k(vfs_files[i].data, content, MAX_VFS_DATA);
            return;
        }
    }
}

void init_ramfs() {
    for (int i = 0; i < MAX_VFS_FILES; i++) vfs_files[i].used = 0;

    vfs_set_file("README.md",
        "# KazOS v2.0 Sovereign Edition\n"
        "Sistema Operacional escrito do zero (Bare-Metal x86 + Kaz).\n"
        "- Kernel HAL em C Freestanding + Bootloader MBR VBE 1024x768\n"
        "- Interface Grafica Kaz-X, Kaz-UI, Apps e Shell 100% em Kaz!\n"
        "- Double-Buffering em RAM para renderizacao fluida a 60 FPS.\n");

    vfs_set_file("sistema.conf",
        "[kazos]\n"
        "version = 2.0.0-sovereign\n"
        "display = 1024x768x32_LFB\n"
        "compositor = double_buffered_0x200000\n"
        "shell = ksh_2.0\n"
        "apps = TermiKAZ, KazCalc, KazNotes, KazMon, ControlCenter\n");

    vfs_set_file("hello.kaz",
        "// Exemplo de modulo nativo em Kaz para o KazOS\n"
        "import \"tipos.kaz\";\n\n"
        "function saudarKazOS(): int {\n"
        "    sys_term_print(\"Ola do Kernel Soberano KazOS!\\n\", 0x00FF7F);\n"
        "    return 42;\n"
        "}\n");

    vfs_set_file("notas.txt",
        "Bem-vindo ao KazNotes!\n"
        "Este bloco de notas roda dentro do gerenciador de janelas Kaz-X.\n"
        "Digite qualquer texto aqui quando esta janela estiver focada.\n"
        "Clique em [Salvar RamFS] e depois digite 'cat notas.txt' no terminal!\n");

    vfs_set_file("todo.txt",
        "[x] Bootloader VBE 1024x768 + 240 setores LBA\n"
        "[x] Double Buffering em RAM (Zero Flicker / 60 FPS)\n"
        "[x] Calculadora Nativa KazCalc em Kaz\n"
        "[x] Editor KazNotes + RamFS em memoria\n"
        "[x] Shell completo com neofetch, grep, wc, hexdump, expr\n");

    strcpy_k(notes_buffer, vfs_files[3].data, MAX_VFS_DATA);
    notes_len = strlen_k(notes_buffer);
}

int vfs_find(const char* name) {
    /* Ignora barra inicial caso o usuário digite /README.md */
    if (name[0] == '/') name++;
    for (int i = 0; i < MAX_VFS_FILES; i++) {
        if (vfs_files[i].used && strcmp(vfs_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* =========================================================================
 * 8. SYSCALLS / INTRÍNSECOS EXPOSTOS PARA OS MÓDULOS KAZ (.kaz)
 * ========================================================================= */
void sys_fill_rect(int x, int y, int w, int h, int color) {
    fill_rect(x, y, w, h, (unsigned int)color);
}

void sys_draw_rect(int x, int y, int w, int h, int color) {
    draw_rect(x, y, w, h, (unsigned int)color);
}

void sys_draw_text(int x, int y, const char* text, int fg, int bg, int opaque) {
    draw_string_scaled(x, y, text, (unsigned int)fg, (unsigned int)bg, opaque, 1);
}

void sys_draw_num(int x, int y, int num, int fg, int bg) {
    char buf[28];
    strcpy_k(buf, "* Cliques UI: ", 28);
    char nbuf[12];
    int_to_str(num, nbuf);
    strcat_k(buf, nbuf, 28);
    draw_string_scaled(x, y, buf, (unsigned int)fg, (unsigned int)bg, 0, 1);
}

void sys_draw_int_right(int rightX, int y, int num, int fg, int bg, int scale) {
    char buf[16];
    int_to_str(num, buf);
    int len = strlen_k(buf);
    int cw = (scale == 2) ? 16 : 8;
    int startX = rightX - (len * cw);
    draw_string_scaled(startX, y, buf, (unsigned int)fg, (unsigned int)bg, 0, scale);
}

void sys_draw_wallpaper(int w, int h, int c1, int c2, int style) {
    /* Desenha fundo com gradiente em faixas horizontais + grid sutil estilo engenharia */
    int bands = 16;
    int band_h = (h + bands - 1) / bands;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;

    for (int i = 0; i < bands; i++) {
        int y0 = i * band_h;
        int bh = band_h;
        if (y0 + bh > h) bh = h - y0;
        if (bh <= 0) break;
        int r = r1 + ((r2 - r1) * i) / bands;
        int g = g1 + ((g2 - g1) * i) / bands;
        int b = b1 + ((b2 - b1) * i) / bands;
        unsigned int col = (unsigned int)((r << 16) | (g << 8) | b);
        fill_rect(0, y0, w, bh, col);
    }

    /* Padrão de pontos discretos no desktop */
    unsigned int dot_col = (unsigned int)c2 + 0x0010161E;
    for (int y = 44; y < h - 16; y += 32) {
        for (int x = 112; x < w - 16; x += 32) {
            put_pixel(x, y, dot_col);
        }
    }
}

void sys_draw_rtc_clock(int x, int y, int fg, int bg) {
    char buf[16];
    get_rtc_time_str(buf);
    draw_string_scaled(x, y, buf, (unsigned int)fg, (unsigned int)bg, 0, 1);
}

void sys_term_print(const char* s, int color) {
    term_print(s, (unsigned int)color);
}

void sys_term_clear() {
    term_clear();
}

void sys_term_print_rtc() {
    char buf[16];
    get_rtc_time_str(buf);
    term_print(buf, COLOR_TERM_YELLOW);
}

/* Funções de parsing e utilitários para o Shell escrito em Kaz */
int sys_cmd_starts(const char* cmd, const char* prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (cmd[i] != prefix[i]) return 0;
        i++;
    }
    return (cmd[i] == '\0' || cmd[i] == ' ');
}

const char* sys_cmd_arg(const char* cmd) {
    int i = 0;
    while (cmd[i] != '\0' && cmd[i] != ' ') i++;
    while (cmd[i] == ' ') i++;
    return &cmd[i];
}

void sys_vfs_ls(const char* dir) {
    term_print("Diretorio ", COLOR_TERM_YELLOW);
    term_print(dir, COLOR_TERM_PROMPT);
    term_print(" (RamFS + Modulos Kernel Kaz):\n", COLOR_TERM_YELLOW);
    term_print("  PERMISSOES  TAMANHO  NOME               DESCRICAO / TIPO\n", 0x0094A3B8);

    int total_bytes = 0;
    int count = 0;
    for (int i = 0; i < MAX_VFS_FILES; i++) {
        if (vfs_files[i].used) {
            int sz = strlen_k(vfs_files[i].data);
            total_bytes += sz;
            count++;
            term_print("  -rw-r--r--  ", COLOR_TERM_WHITE);
            char sbuf[12];
            int_to_str(sz, sbuf);
            int pad = 5 - strlen_k(sbuf);
            while (pad-- > 0) term_print(" ", COLOR_TERM_WHITE);
            term_print(sbuf, COLOR_TERM_YELLOW);
            term_print(" B  ", COLOR_TERM_YELLOW);
            term_print(vfs_files[i].name, COLOR_TERM_FG);
            int npad = 19 - strlen_k(vfs_files[i].name);
            while (npad-- > 0) term_print(" ", COLOR_TERM_WHITE);
            term_print("[Arquivo RamFS RW]\n", COLOR_TERM_PROMPT);
        }
    }
    term_print("  drwxr-xr-x   60 KB  kaz_ui/*.kaz       [Codigo Fonte Nativo Kaz-UI & Apps]\n", COLOR_TERM_FG);
    term_print("  -rwxr-xr-x  240 S   kernel.bin         [Kernel Bare-Metal + Compositor]\n", COLOR_TERM_PROMPT);
    term_print("Total: ", 0x0094A3B8);
    term_print_int(count, COLOR_TERM_WHITE);
    term_print(" arquivos no RamFS (", 0x0094A3B8);
    term_print_int(total_bytes, COLOR_TERM_YELLOW);
    term_print(" bytes em uso)\n", 0x0094A3B8);
}

void sys_vfs_cat(const char* name) {
    int idx = vfs_find(name);
    if (idx < 0) {
        term_print("cat: arquivo nao encontrado: ", COLOR_TERM_RED);
        term_print(name, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }
    term_print("--- ", COLOR_TERM_PROMPT);
    term_print(vfs_files[idx].name, COLOR_TERM_YELLOW);
    term_print(" ---\n", COLOR_TERM_PROMPT);
    term_print(vfs_files[idx].data, COLOR_TERM_WHITE);
    int len = strlen_k(vfs_files[idx].data);
    if (len > 0 && vfs_files[idx].data[len - 1] != '\n') {
        term_print("\n", COLOR_TERM_WHITE);
    }
}

void sys_vfs_touch(const char* name) {
    int idx = vfs_find(name);
    if (idx >= 0) {
        term_print("touch: arquivo ja existe: ", COLOR_TERM_YELLOW);
        term_print(name, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }
    vfs_set_file(name, "");
    term_print("[RamFS] Arquivo criado: ", COLOR_TERM_FG);
    term_print(name, COLOR_TERM_WHITE);
    term_print("\n", COLOR_TERM_WHITE);
}

void sys_vfs_write_cmd(const char* cmd) {
    const char* rest = sys_cmd_arg(cmd);
    char fname[MAX_VFS_NAME];
    int i = 0;
    while (rest[i] != '\0' && rest[i] != ' ' && i < MAX_VFS_NAME - 1) {
        fname[i] = rest[i];
        i++;
    }
    fname[i] = '\0';
    while (rest[i] == ' ') i++;
    const char* content = &rest[i];

    if (fname[0] == '\0' || content[0] == '\0') {
        term_print("Uso: write <arquivo> <conteudo de texto>\n", COLOR_TERM_YELLOW);
        return;
    }
    char formatted[MAX_VFS_DATA];
    strcpy_k(formatted, content, MAX_VFS_DATA - 2);
    strcat_k(formatted, "\n", MAX_VFS_DATA);
    vfs_set_file(fname, formatted);

    if (strcmp(fname, "notas.txt") == 0) {
        strcpy_k(notes_buffer, formatted, MAX_VFS_DATA);
        notes_len = strlen_k(notes_buffer);
    }

    term_print("[RamFS] Gravado com sucesso em '", COLOR_TERM_FG);
    term_print(fname, COLOR_TERM_YELLOW);
    term_print("' (", COLOR_TERM_FG);
    term_print_int(strlen_k(formatted), COLOR_TERM_WHITE);
    term_print(" bytes).\n", COLOR_TERM_FG);
}

void sys_vfs_rm(const char* name) {
    int idx = vfs_find(name);
    if (idx < 0) {
        term_print("rm: arquivo nao encontrado: ", COLOR_TERM_RED);
        term_print(name, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }
    vfs_files[idx].used = 0;
    term_print("[RamFS] Arquivo removido: ", COLOR_TERM_YELLOW);
    term_print(name, COLOR_TERM_WHITE);
    term_print("\n", COLOR_TERM_WHITE);
}

void sys_vfs_grep_cmd(const char* cmd) {
    const char* rest = sys_cmd_arg(cmd);
    char pattern[64];
    int i = 0;
    while (rest[i] != '\0' && rest[i] != ' ' && i < 63) {
        pattern[i] = rest[i];
        i++;
    }
    pattern[i] = '\0';
    while (rest[i] == ' ') i++;
    const char* fname = &rest[i];

    if (pattern[0] == '\0' || fname[0] == '\0') {
        term_print("Uso: grep <padrao> <arquivo>\n", COLOR_TERM_YELLOW);
        return;
    }
    int idx = vfs_find(fname);
    if (idx < 0) {
        term_print("grep: arquivo nao encontrado: ", COLOR_TERM_RED);
        term_print(fname, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }

    const char* data = vfs_files[idx].data;
    char line[128];
    int lp = 0;
    int line_num = 1;
    int matches = 0;

    for (int k = 0; ; k++) {
        char c = data[k];
        if (c == '\n' || c == '\0') {
            line[lp] = '\0';
            if (lp > 0 && str_contains_k(line, pattern)) {
                term_print("L", COLOR_TERM_PROMPT);
                term_print_int(line_num, COLOR_TERM_YELLOW);
                term_print(": ", COLOR_TERM_PROMPT);
                term_print(line, COLOR_TERM_FG);
                term_print("\n", COLOR_TERM_WHITE);
                matches++;
            }
            lp = 0;
            line_num++;
            if (c == '\0') break;
        } else if (lp < 126) {
            line[lp++] = c;
        }
    }
    if (matches == 0) {
        term_print("(nenhuma ocorrencia encontrada)\n", 0x0094A3B8);
    }
}

void sys_vfs_wc(const char* name) {
    int idx = vfs_find(name);
    if (idx < 0) {
        term_print("wc: arquivo nao encontrado: ", COLOR_TERM_RED);
        term_print(name, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }
    const char* d = vfs_files[idx].data;
    int lines = 0, words = 0, bytes = 0, in_word = 0;
    while (d[bytes] != '\0') {
        char c = d[bytes];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
        bytes++;
    }
    term_print("  Linhas: ", COLOR_TERM_PROMPT);
    term_print_int(lines, COLOR_TERM_WHITE);
    term_print(" | Palavras: ", COLOR_TERM_PROMPT);
    term_print_int(words, COLOR_TERM_WHITE);
    term_print(" | Bytes: ", COLOR_TERM_PROMPT);
    term_print_int(bytes, COLOR_TERM_YELLOW);
    term_print(" (", COLOR_TERM_WHITE);
    term_print(vfs_files[idx].name, COLOR_TERM_FG);
    term_print(")\n", COLOR_TERM_WHITE);
}

void sys_vfs_hexdump(const char* name) {
    int idx = vfs_find(name);
    if (idx < 0) {
        term_print("hexdump: arquivo nao encontrado: ", COLOR_TERM_RED);
        term_print(name, COLOR_TERM_WHITE);
        term_print("\n", COLOR_TERM_WHITE);
        return;
    }
    const char* hex = "0123456789ABCDEF";
    const unsigned char* d = (const unsigned char*)vfs_files[idx].data;
    int len = strlen_k(vfs_files[idx].data);
    if (len > 96) len = 96; /* Mostra os primeiros 96 bytes organizados */

    for (int offset = 0; offset < len; offset += 16) {
        char line[80];
        line[0] = '0'; line[1] = '0';
        line[2] = hex[(offset >> 4) & 0xF];
        line[3] = hex[offset & 0xF];
        line[4] = ':'; line[5] = ' ';
        int p = 6;
        for (int b = 0; b < 16; b++) {
            if (offset + b < len) {
                unsigned char v = d[offset + b];
                line[p++] = hex[(v >> 4) & 0xF];
                line[p++] = hex[v & 0xF];
                line[p++] = ' ';
            } else {
                line[p++] = ' '; line[p++] = ' '; line[p++] = ' ';
            }
        }
        line[p++] = ' '; line[p++] = '|';
        for (int b = 0; b < 16 && (offset + b) < len; b++) {
            unsigned char v = d[offset + b];
            line[p++] = (v >= 32 && v < 127) ? (char)v : '.';
        }
        line[p++] = '|';
        line[p++] = '\n';
        line[p] = '\0';
        term_print(line, COLOR_TERM_PROMPT);
    }
}

void sys_shell_expr(const char* expr) {
    int i = 0;
    while (expr[i] == ' ') i++;
    if (expr[i] == '\0') {
        term_print("Uso: expr <num1> <op> <num2> (ex: expr 125 * 8)\n", COLOR_TERM_YELLOW);
        return;
    }
    int a = 0, neg_a = 0;
    if (expr[i] == '-') { neg_a = 1; i++; }
    while (expr[i] >= '0' && expr[i] <= '9') {
        a = a * 10 + (expr[i] - '0');
        i++;
    }
    if (neg_a) a = -a;
    while (expr[i] == ' ') i++;
    char op = expr[i];
    if (op != '\0') i++;
    while (expr[i] == ' ') i++;
    int b = 0, neg_b = 0;
    if (expr[i] == '-') { neg_b = 1; i++; }
    while (expr[i] >= '0' && expr[i] <= '9') {
        b = b * 10 + (expr[i] - '0');
        i++;
    }
    if (neg_b) b = -b;

    int res = 0;
    if (op == '+') res = a + b;
    else if (op == '-') res = a - b;
    else if (op == '*' || op == 'x') res = a * b;
    else if (op == '/') {
        if (b == 0) { term_print("Erro: divisao por zero!\n", COLOR_TERM_RED); return; }
        res = a / b;
    } else if (op == '%') {
        if (b == 0) { term_print("Erro: modulo por zero!\n", COLOR_TERM_RED); return; }
        res = a % b;
    } else {
        res = a;
    }
    term_print("= ", COLOR_TERM_PROMPT);
    term_print_int(res, COLOR_TERM_FG);
    term_print("\n", COLOR_TERM_WHITE);
}

void sys_shell_neofetch() {
    term_print("   _  __          ___  ____    ", COLOR_TERM_FG);     term_print("root@kazos-baremetal\n", COLOR_TERM_PROMPT);
    term_print("  | |/ /__ _ ____/ _ \\/ ___|   ", COLOR_TERM_FG);     term_print("--------------------\n", COLOR_TERM_WHITE);
    term_print("  | ' // _` |_  / | | \\___ \\   ", COLOR_TERM_FG);   term_print("OS:      KazOS v2.0 Sovereign Edition (x86_32)\n", COLOR_TERM_WHITE);
    term_print("  | . \\ (_| |/ /| |_| |___) |  ", COLOR_TERM_PROMPT); term_print("Kernel:  Kaz-HAL Bare-Metal Ring0 (No Linux/POSIX)\n", COLOR_TERM_WHITE);
    term_print("  |_|\\_\\__,_/___|\\___/|____/   ", COLOR_TERM_PROMPT); term_print("Compiler:kazos_cc.kaz (100% Self-Hosted Kaz)\n", COLOR_TERM_FG);
    term_print("                               ", COLOR_TERM_WHITE);   term_print("Display: VESA VBE 1024x768 @ 32bpp (Double-Buffered)\n", COLOR_TERM_YELLOW);
    term_print("  [ S O V E R E I G N   O S ]  ", COLOR_TERM_YELLOW);  term_print("WM / UI: Kaz-X Compositor + Kaz-UI Toolkit\n", COLOR_TERM_PROMPT);
    term_print("                               ", COLOR_TERM_WHITE);   term_print("Apps:    TermiKAZ, KazCalc, KazNotes, KazMon\n", COLOR_TERM_FG);
    term_print("                               ", COLOR_TERM_WHITE);   term_print("Memory:  4.2 MB Used / 128 MB RAM (VRAM @ 0xFD000000)\n", COLOR_TERM_WHITE);
}

void sys_shell_ps() {
    term_print("  PID  RING  ESTADO   MEMORIA   NOME DA TAREFA / SERVICO\n", COLOR_TERM_YELLOW);
    term_print("    1  R0    ATIVO     64 KB    kernel_hal (VBE + PS/2 + CMOS + RamFS)\n", COLOR_TERM_WHITE);
    term_print("    2  R0    ATIVO   3072 KB    kaz_x_compositor (Backbuffer 1024x768 60FPS)\n", COLOR_TERM_FG);
    term_print("    3  R0    ATIVO     32 KB    ksh_shell (TermiKAZ Command Interpreter)\n", COLOR_TERM_PROMPT);
    term_print("    4  R0    PRONTO    16 KB    kaz_calc (Calculadora Cientifica/Inteira)\n", COLOR_TERM_WHITE);
    term_print("    5  R0    PRONTO    24 KB    kaz_notes (Editor de Texto RamFS)\n", COLOR_TERM_WHITE);
    term_print("    6  R0    PRONTO    16 KB    kaz_mon (Monitor de Recursos do Sistema)\n", COLOR_TERM_WHITE);
}

void sys_shell_free() {
    term_print("              TOTAL       USADO       LIVRE      DESCRICAO\n", COLOR_TERM_YELLOW);
    term_print("Mem RAM:    131072 KB     4320 KB   126752 KB    Kernel + Backbuffer 0x200000\n", COLOR_TERM_FG);
    term_print("VBE VRAM:     4096 KB     3072 KB     1024 KB    Linear Framebuffer 1024x768x32\n", COLOR_TERM_PROMPT);
    term_print("RamFS:           8 KB        2 KB        6 KB    16 Slots de Arquivos Dinamicos\n", COLOR_TERM_WHITE);
}

void sys_shell_history() {
    if (history_count == 0) {
        term_print("(historico vazio)\n", 0x0094A3B8);
        return;
    }
    for (int i = 0; i < history_count; i++) {
        term_print("  ", COLOR_TERM_WHITE);
        term_print_int(i + 1, COLOR_TERM_YELLOW);
        term_print("  ", COLOR_TERM_WHITE);
        term_print(cmd_history[i], COLOR_TERM_FG);
        term_print("\n", COLOR_TERM_WHITE);
    }
}

void sys_shell_uptime(int cmds) {
    term_print("KazOS Sovereign ativo | Relogio: ", COLOR_TERM_WHITE);
    sys_term_print_rtc();
    term_print(" | Frames GUI: ", COLOR_TERM_WHITE);
    term_print_int((int)kernel_frames_rendered, COLOR_TERM_FG);
    term_print(" | Comandos executados: ", COLOR_TERM_WHITE);
    term_print_int(cmds, COLOR_TERM_PROMPT);
    term_print("\n", COLOR_TERM_WHITE);
}

void sys_reboot() {
    term_print("Reiniciando KazOS via controlador 8042...\n", COLOR_TERM_YELLOW);
    outb(0x64, 0xFE);
}

void sys_poweroff() {
    term_print("Desligando KazOS (ACPI Poweroff)...\n", COLOR_TERM_YELLOW);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
}

/* Renderiza o buffer vivo do terminal dentro da janela gráfica TermiKAZ! */
void sys_render_term_in_rect(int rx, int ry, int rw, int rh, int active) {
    fill_rect(rx, ry, rw, rh, 0x000B0F14);
    draw_rect(rx, ry, rw, rh, active ? 0x0000B87C : 0x00283545);

    int max_cols = (rw - 12) / 8;
    int max_rows = (rh - 12) / 16;
    if (max_cols > term_cols) max_cols = term_cols;

    int start_row = 0;
    if (term_cur_y >= max_rows) {
        start_row = term_cur_y - max_rows + 1;
    }

    for (int r = 0; r < max_rows; r++) {
        int buf_r = start_row + r;
        if (buf_r >= term_rows) break;
        int py = ry + 6 + r * 16;
        for (int c = 0; c < max_cols; c++) {
            char ch_val = term_buffer[buf_r][c];
            if (ch_val != ' ' && ch_val != '\0') {
                draw_char_scaled(rx + 6 + c * 8, py, ch_val, term_fg_buffer[buf_r][c], 0x000B0F14, 0, 1);
            }
        }
    }

    /* Desenha cursor dentro da janela TermiKAZ */
    int vis_cur_y = term_cur_y - start_row;
    int vis_cur_x = term_cur_x;
    if (vis_cur_x >= max_cols) vis_cur_x = max_cols - 1;
    if (vis_cur_y >= 0 && vis_cur_y < max_rows) {
        unsigned int cur_color = active ? 0x0000FF7F : 0x00556677;
        fill_rect(rx + 6 + vis_cur_x * 8, ry + 6 + vis_cur_y * 16 + 14, 8, 2, cur_color);
    }
}

/* Funções do Aplicativo KazNotes (Editor de Texto em Janela) */
void sys_notes_save() {
    vfs_set_file("notas.txt", notes_buffer);
    notes_saved_status = 1;
}

void sys_notes_clear() {
    notes_buffer[0] = '\0';
    notes_len = 0;
    notes_saved_status = 0;
}

void sys_notes_template() {
    const char* tpl =
        "// Modulo Kaz gerado no KazNotes\n"
        "function calcularDobro(int x): int {\n"
        "    return x * 2;\n"
        "}\n";
    strcpy_k(notes_buffer, tpl, MAX_VFS_DATA);
    notes_len = strlen_k(notes_buffer);
    notes_saved_status = 0;
}

void sys_render_notes_in_rect(int rx, int ry, int rw, int rh, int active) {
    fill_rect(rx, ry, rw, rh, 0x000F172A);
    draw_rect(rx, ry, rw, rh, active ? 0x006366F1 : 0x00334155);

    /* Coluna lateral para numeração de linhas */
    fill_rect(rx + 1, ry + 1, 30, rh - 2, 0x001E293B);

    int max_cols = (rw - 42) / 8;
    int max_rows = (rh - 30) / 16;
    int row = 0, col = 0;

    draw_string_scaled(rx + 6, ry + 6, " 1", 0x0064748B, 0x001E293B, 0, 1);

    for (int i = 0; i < notes_len; i++) {
        char c = notes_buffer[i];
        if (c == '\n') {
            row++;
            col = 0;
            if (row >= max_rows) break;
            char lbuf[4];
            lbuf[0] = (row + 1 >= 10) ? ('0' + (row + 1) / 10) : ' ';
            lbuf[1] = '0' + ((row + 1) % 10);
            lbuf[2] = '\0';
            draw_string_scaled(rx + 6, ry + 6 + row * 16, lbuf, 0x0064748B, 0x001E293B, 0, 1);
            continue;
        }
        if (col >= max_cols) {
            row++;
            col = 0;
            if (row >= max_rows) break;
        }
        draw_char_scaled(rx + 36 + col * 8, ry + 6 + row * 16, c, 0x00F8FAFC, 0x000F172A, 0, 1);
        col++;
    }

    if (row < max_rows) {
        unsigned int cur_col = active ? 0x00818CF8 : 0x00475569;
        fill_rect(rx + 36 + col * 8, ry + 6 + row * 16 + 14, 8, 2, cur_col);
    }

    /* Barra de status inferior do KazNotes */
    int stY = ry + rh - 20;
    fill_rect(rx + 1, stY, rw - 2, 19, 0x001E293B);
    if (notes_saved_status) {
        draw_string_scaled(rx + 8, stY + 2, "[Salvo em /home/kaz/notas.txt!]", 0x0000FF7F, 0x001E293B, 0, 1);
    } else {
        draw_string_scaled(rx + 8, stY + 2, "Editando notas.txt (Digite no teclado)", 0x0094A3B8, 0x001E293B, 0, 1);
    }
    sys_draw_int_right(rx + rw - 48, stY + 2, notes_len, 0x0038BDF8, 0x001E293B, 1);
    draw_string_scaled(rx + rw - 44, stY + 2, "B", 0x0038BDF8, 0x001E293B, 0, 1);
}

/* Renderiza o Monitor de Sistema (KazMon) */
void sys_render_monitor_in_rect(int rx, int ry, int rw, int rh) {
    /* Card 1: Uso de Memória Física e VRAM */
    fill_rect(rx, ry, rw, 120, 0x001E293B);
    draw_rect(rx, ry, rw, 120, 0x00334155);
    draw_string_scaled(rx + 10, ry + 8, "Alocacao de Memoria Bare-Metal & Framebuffer:", 0x0038BDF8, 0x001E293B, 0, 1);

    /* Barra 1: Kernel + Backbuffer RAM */
    draw_string_scaled(rx + 10, ry + 32, "RAM Kernel + Backbuffer (4.2 MB / 128 MB)", 0x00F8FAFC, 0x001E293B, 0, 1);
    fill_rect(rx + 10, ry + 50, rw - 20, 14, 0x000F172A);
    fill_rect(rx + 10, ry + 50, 48, 14, 0x0000FF7F);
    draw_rect(rx + 10, ry + 50, rw - 20, 14, 0x00475569);

    /* Barra 2: VBE Linear Framebuffer VRAM */
    draw_string_scaled(rx + 10, ry + 72, "VRAM VBE 1024x768x32 (3.0 MB / 4.0 MB)", 0x00F8FAFC, 0x001E293B, 0, 1);
    fill_rect(rx + 10, ry + 90, rw - 20, 14, 0x000F172A);
    fill_rect(rx + 10, ry + 90, ((rw - 20) * 3) / 4, 14, 0x0038BDF8);
    draw_rect(rx + 10, ry + 90, rw - 20, 14, 0x00475569);

    /* Card 2: Tabela de Tarefas Ativas no Kernel Ring 0 */
    int tY = ry + 130;
    int tH = rh - 130;
    fill_rect(rx, tY, rw, tH, 0x001E293B);
    draw_rect(rx, tY, rw, tH, 0x00334155);
    draw_string_scaled(rx + 10, tY + 8, "PID  MODULO KAZ / SUBSISTEMA         ESTADO     RING", 0x00FFD166, 0x001E293B, 0, 1);
    draw_string_scaled(rx + 10, tY + 32, "001  kernel_hal (VBE+PS2+RTC)        ONLINE     Ring0", 0x00F8FAFC, 0x001E293B, 0, 1);
    draw_string_scaled(rx + 10, tY + 52, "002  kaz_x_compositor (60FPS Blit)   ATIVO      Ring0", 0x0000FF7F, 0x001E293B, 0, 1);
    draw_string_scaled(rx + 10, tY + 72, "003  ksh_shell + RamFS (16 slots)    ATIVO      Ring0", 0x0038BDF8, 0x001E293B, 0, 1);
    draw_string_scaled(rx + 10, tY + 92, "004  kaz_apps (Calc + Notes + Mon)   PRONTO     Ring0", 0x00F8FAFC, 0x001E293B, 0, 1);

    draw_string_scaled(rx + 10, tY + 122, "Quadros Compostos (SwapBuffers):", 0x0094A3B8, 0x001E293B, 0, 1);
    sys_draw_int_right(rx + 330, tY + 122, (int)kernel_frames_rendered, 0x0000FF7F, 0x001E293B, 1);
}

/* =========================================================================
 * 9. INCLUI O CÓDIGO GERADO PELOS MÓDULOS KAZ (kaz_ui/*.kaz)
 * ========================================================================= */
#include "kaz_ui_gen.h"

void render_gui_with_mouse() {
    /* Desenha toda a cena no Backbuffer em RAM e faz um único swap_buffers() para a VRAM! */
    g_draw_to_backbuffer_only = 1;
    kaz_x_render(screen_w, screen_h);
    g_draw_to_backbuffer_only = 0;
    swap_buffers();
    draw_mouse_cursor(mouse_x, mouse_y);
}

void delay_cycles(unsigned int count) {
    volatile unsigned int i;
    for (i = 0; i < count; i++) {
        __asm__ volatile("nop");
    }
}

void show_gui_loading_screen() {
    /* Tela de inicialização (Splash / Loading animado) com identidade visual soberana */
    const char* etapas[] = {
        "Inicializando HAL VBE 1024x768x32...",
        "Montando RamFS & Estruturas Inode...",
        "Carregando Fonte Moderna Clean Sans...",
        "Subindo Compositor Kaz-X (Double-Buffered)...",
        "Iniciando Suite de Aplicativos Soberana...",
        "Pronto para Uso!"
    };
    int total_etapas = 6;
    int splash_w = 540;
    int splash_h = 240;
    int sx = (screen_w - splash_w) / 2;
    int sy = (screen_h - splash_h) / 2;

    for (int step = 0; step < total_etapas; step++) {
        g_draw_to_backbuffer_only = 1;

        // Fundo do Desktop durante o boot
        sys_draw_wallpaper(screen_w, screen_h, 0x000F172A, 0x001E1B4B, 0);

        // Sombra do Card Splash
        sys_fill_rect(sx + 8, sy + 8, splash_w, splash_h, 0x0006090E);

        // Card Central
        sys_fill_rect(sx, sy, splash_w, splash_h, 0x00131D28);
        sys_draw_rect(sx, sy, splash_w, splash_h, 0x0000B87C);
        sys_draw_rect(sx + 1, sy + 1, splash_w - 2, splash_h - 2, 0x00334155);

        // Logo e Título
        draw_string_scaled(sx + (splash_w - 180) / 2, sy + 32, "KazOS Sovereign", 0x0000FF7F, 0x00131D28, 0, 2);
        draw_string_scaled(sx + (splash_w - 280) / 2, sy + 70, "Sistema Operacional Bare-Metal Soberano", 0x0094A3B8, 0x00131D28, 0, 1);

        // Barra de Progresso
        int bar_x = sx + 40;
        int bar_y = sy + 120;
        int bar_w = splash_w - 80;
        int bar_h = 16;
        sys_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x000A1017);
        sys_draw_rect(bar_x, bar_y, bar_w, bar_h, 0x00475569);

        int prog_w = ((step + 1) * (bar_w - 4)) / total_etapas;
        if (prog_w > 0) {
            sys_fill_rect(bar_x + 2, bar_y + 2, prog_w, bar_h - 4, 0x0000B87C);
        }

        // Texto do status da etapa
        draw_string_scaled(bar_x, bar_y + 30, etapas[step], 0x00F8FAFC, 0x00131D28, 0, 1);

        // Porcentagem à direita
        int pct = ((step + 1) * 100) / total_etapas;
        char pct_str[16];
        int pi = 0;
        if (pct == 100) { pct_str[pi++] = '1'; pct_str[pi++] = '0'; pct_str[pi++] = '0'; }
        else { pct_str[pi++] = '0' + (pct / 10); pct_str[pi++] = '0' + (pct % 10); }
        pct_str[pi++] = '%';
        pct_str[pi] = '\0';
        draw_string_scaled(bar_x + bar_w - 36, bar_y + 30, pct_str, 0x0038BDF8, 0x00131D28, 0, 1);

        g_draw_to_backbuffer_only = 0;
        swap_buffers();

        // Delay suave para cada etapa (cerca de 200-250ms)
        delay_cycles(28000000);
    }
}

void launch_gui() {
    show_gui_loading_screen();
    gui_active = 1;
    render_gui_with_mouse();
}

void return_to_terminal() {
    gui_active = 0;
    term_redraw_all();
    term_print("\n[Retornou ao Console Fullscreen KazOS - Digite 'x' para abrir o Kaz-X]\n", COLOR_TERM_PROMPT);
    term_print("kaz> ", COLOR_TERM_PROMPT);
}

/* =========================================================================
 * 10. DRIVER DO MOUSE PS/2
 * ========================================================================= */
void ps2_wait_write() {
    int timeout = 100000;
    while (timeout-- && (inb(0x64) & 2));
}

void ps2_wait_read() {
    int timeout = 100000;
    while (timeout-- && !(inb(0x64) & 1));
}

void mouse_write(unsigned char val) {
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, val);
}

unsigned char mouse_read() {
    ps2_wait_read();
    return inb(0x60);
}

void init_ps2_mouse() {
    ps2_wait_write();
    outb(0x64, 0xA8);

    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    unsigned char status = inb(0x60) | 2;
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();
}

/* =========================================================================
 * 11. PROCESSAMENTO DE COMANDOS E TECLADO (COM SHIFT E HISTÓRICO)
 * ========================================================================= */
void replace_current_cmd_line(const char* new_cmd) {
    while (cmd_index > 0) {
        cmd_index--;
        term_put_char('\b', COLOR_TERM_WHITE);
    }
    strcpy_k(cmd_buffer, new_cmd, CMD_BUFFER_SIZE);
    cmd_index = strlen_k(cmd_buffer);
    term_print(cmd_buffer, COLOR_TERM_WHITE);
    if (gui_active) {
        render_gui_with_mouse();
    }
}

void execute_shell_line(char* cmd) {
    if (cmd[0] != '\0') {
        if (history_count < MAX_HISTORY) {
            strcpy_k(cmd_history[history_count++], cmd, CMD_BUFFER_SIZE);
        } else {
            for (int i = 0; i < MAX_HISTORY - 1; i++) {
                strcpy_k(cmd_history[i], cmd_history[i + 1], CMD_BUFFER_SIZE);
            }
            strcpy_k(cmd_history[MAX_HISTORY - 1], cmd, CMD_BUFFER_SIZE);
        }
    }
    history_view_idx = history_count;

    int action = kaz_shell_exec(cmd);
    if (action == 1) {
        launch_gui();
    } else if (action == 2) {
        if (font_scale == 2) {
            font_scale = 1;
            term_cols = screen_w / 8;
            term_rows = screen_h / 16;
            term_clear();
            term_print("Fonte alterada para 1x Normal (8x16).\n", COLOR_TERM_YELLOW);
        } else {
            font_scale = 2;
            term_cols = screen_w / 16;
            term_rows = screen_h / 32;
            term_clear();
            term_print("Fonte alterada para 2x Grande (16x32).\n", COLOR_TERM_YELLOW);
        }
    } else if (action >= 3 && action <= 6) {
        /* Abre diretamente o app solicitado (3=KazCalc, 4=KazNotes, 5=KazMon, 6=KazAI) no Kaz-X */
        kaz_x_open_app(action);
        launch_gui();
    }
}

static const char scancode_table[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
  '*',   0, ' '
};

static const char scancode_shift_table[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,
  '*',   0, ' '
};

/* =========================================================================
 * 12. PONTO DE ENTRADA DO KERNEL
 * ========================================================================= */
void kmain() {
    struct vbe_mode_info* minfo = (struct vbe_mode_info*)VBE_INFO_ADDR;
    if (minfo->physbase != 0 && minfo->x_resolution > 0) {
        fb_bytes = (volatile unsigned char*)minfo->physbase;
        screen_w = minfo->x_resolution;
        screen_h = minfo->y_resolution;
        screen_bpp = minfo->bits_per_pixel;
        bytes_per_line = minfo->bytes_per_scanline;
    } else {
        fb_bytes = (volatile unsigned char*)0xFD000000;
        screen_w = 1024;
        screen_h = 768;
        screen_bpp = 32;
        bytes_per_line = 1024 * 4;
    }

    term_cols = screen_w / (font_scale * 8);
    term_rows = screen_h / (font_scale * 16);

    /* Inicializa RamFS e arquivos padrão */
    init_ramfs();

    /* Inicializa o Window Manager Kaz-X v2.0 (escrito em Kaz) */
    kaz_x_init(screen_w, screen_h);

    /* Inicializa Terminal e Banner escrito em Kaz */
    term_clear();
    kaz_shell_banner(screen_w, screen_h);
    term_print("kaz> ", COLOR_TERM_PROMPT);

    init_ps2_mouse();
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;

    unsigned char mouse_cycle = 0;
    signed char mouse_byte[3] = {0, 0, 0};

    while (1) {
        unsigned char status = inb(0x64);

        if (status & 0x01) {
            unsigned char data = inb(0x60);

            if (status & 0x20) {
                if (mouse_cycle == 0) {
                    if (data & 0x08) {
                        mouse_byte[0] = data;
                        mouse_cycle++;
                    }
                } else if (mouse_cycle == 1) {
                    mouse_byte[1] = data;
                    mouse_cycle++;
                } else if (mouse_cycle == 2) {
                    mouse_byte[2] = data;
                    mouse_cycle = 0;

                    int prev_left = mouse_left;
                    mouse_left = mouse_byte[0] & 0x1;
                    mouse_right = mouse_byte[0] & 0x2;

                    int dx = mouse_byte[1];
                    int dy = mouse_byte[2];
                    if (mouse_byte[0] & 0x10) dx |= 0xFFFFFF00;
                    if (mouse_byte[0] & 0x20) dy |= 0xFFFFFF00;

                    int new_x = mouse_x + dx;
                    int new_y = mouse_y - dy;
                    if (new_x < 0) new_x = 0;
                    if (new_x >= screen_w - 1) new_x = screen_w - 1;
                    if (new_y < 0) new_y = 0;
                    if (new_y >= screen_h - 1) new_y = screen_h - 1;

                    if (gui_active) {
                        if (mouse_left && !prev_left) {
                            int res = kaz_x_on_mouse_down(mouse_x, mouse_y, screen_w, screen_h);
                            if (res == 2) {
                                return_to_terminal();
                                continue;
                            } else if (res == 1) {
                                render_gui_with_mouse();
                            }
                        }

                        if (!mouse_left && prev_left) {
                            kaz_x_on_mouse_up();
                        }

                        if (new_x != mouse_x || new_y != mouse_y) {
                            mouse_x = new_x;
                            mouse_y = new_y;
                            if (kaz_x_on_mouse_move(mouse_x, mouse_y, screen_w, screen_h)) {
                                render_gui_with_mouse();
                            } else {
                                restore_mouse_bg();
                                draw_mouse_cursor(mouse_x, mouse_y);
                            }
                        }
                    } else {
                        mouse_x = new_x;
                        mouse_y = new_y;
                    }
                }
            } else {
                unsigned char scancode = data;
                if (scancode == 0xE0) {
                    extended_scancode = 1;
                    continue;
                }

                /* Controle de Shift Esquerdo (0x2A) e Direito (0x36) */
                if (scancode == 0x2A || scancode == 0x36) {
                    shift_pressed = 1;
                    extended_scancode = 0;
                    continue;
                }
                if (scancode == 0xAA || scancode == 0xB6) {
                    shift_pressed = 0;
                    extended_scancode = 0;
                    continue;
                }

                if (!(scancode & 0x80)) {
                    if (scancode == 1) { /* ESC */
                        extended_scancode = 0;
                        if (gui_active) {
                            /* Se já estiver com diálogo aberto, ESC cancela e fecha o diálogo */
                            /* Se não estiver aberto, abre o diálogo de confirmação */
                            kaz_x_toggle_dialogo_sair();
                            render_gui_with_mouse();
                            continue;
                        }
                    }

                    /* Se o diálogo de confirmação de saída estiver aberto na GUI: */
                    if (gui_active && kaz_x_is_dialogo_sair_aberto()) {
                        extended_scancode = 0;
                        char c_key = shift_pressed ? scancode_shift_table[scancode] : scancode_table[scancode];
                        if (c_key == '\n' || c_key == 's' || c_key == 'S') {
                            /* Enter ou 'S' confirma a saída para o Console */
                            kaz_x_confirmar_sair();
                            return_to_terminal();
                            continue;
                        } else if (c_key == 'n' || c_key == 'N') {
                            /* 'N' cancela */
                            kaz_x_cancelar_sair();
                            render_gui_with_mouse();
                            continue;
                        }
                        /* Ignora qualquer outra tecla enquanto o modal estiver aberto */
                        continue;
                    }

                    /* Navegação no histórico com Setas Cima (0x48) e Baixo (0x50) */
                    if (extended_scancode) {
                        extended_scancode = 0;
                        if (!gui_active || kaz_x_is_term_active()) {
                            if (scancode == 0x48 && history_count > 0) { /* Seta Cima */
                                if (history_view_idx > 0) history_view_idx--;
                                replace_current_cmd_line(cmd_history[history_view_idx]);
                                continue;
                            } else if (scancode == 0x50 && history_count > 0) { /* Seta Baixo */
                                if (history_view_idx < history_count - 1) {
                                    history_view_idx++;
                                    replace_current_cmd_line(cmd_history[history_view_idx]);
                                } else {
                                    history_view_idx = history_count;
                                    replace_current_cmd_line("");
                                }
                                continue;
                            }
                        }
                        continue;
                    }

                    if (scancode < 128) {
                        char c = shift_pressed ? scancode_shift_table[scancode] : scancode_table[scancode];
                        if (c == 0) continue;

                        /* 1. Se o KazNotes estiver ativo na GUI, redireciona a digitação para o documento! */
                        if (gui_active && kaz_x_is_notes_active()) {
                            if (c == '\b') {
                                if (notes_len > 0) {
                                    notes_len--;
                                    notes_buffer[notes_len] = '\0';
                                    notes_saved_status = 0;
                                    render_gui_with_mouse();
                                }
                            } else if (notes_len < MAX_VFS_DATA - 2) {
                                notes_buffer[notes_len++] = c;
                                notes_buffer[notes_len] = '\0';
                                notes_saved_status = 0;
                                render_gui_with_mouse();
                            }
                            continue;
                        }

                        /* 2. Se a Calculadora KazCalc estiver ativa na GUI, aceita números e operadores pelo teclado! */
                        if (gui_active && kaz_x_is_calc_active()) {
                            if (c >= '0' && c <= '9') { calcDigito(c - '0'); render_gui_with_mouse(); continue; }
                            if (c == '+') { calcDefinirOperador(1); render_gui_with_mouse(); continue; }
                            if (c == '-') { calcDefinirOperador(2); render_gui_with_mouse(); continue; }
                            if (c == '*') { calcDefinirOperador(3); render_gui_with_mouse(); continue; }
                            if (c == '/') { calcDefinirOperador(4); render_gui_with_mouse(); continue; }
                            if (c == '%') { calcDefinirOperador(5); render_gui_with_mouse(); continue; }
                            if (c == '\n' || c == '=') { calcIgual(); render_gui_with_mouse(); continue; }
                            if (c == 'c' || c == 'C' || c == '\b') { calcLimpar(); render_gui_with_mouse(); continue; }
                        }

                        /* 3. Digitação no Console puro ou na janela TermiKAZ */
                        if (!gui_active || kaz_x_is_term_active()) {
                            if (c == '\n') {
                                term_put_char('\n', COLOR_TERM_WHITE);
                                cmd_buffer[cmd_index] = '\0';
                                execute_shell_line(cmd_buffer);
                                cmd_index = 0;
                                term_print("kaz> ", COLOR_TERM_PROMPT);
                                if (gui_active) {
                                    render_gui_with_mouse();
                                }
                            } else if (c == '\b') {
                                if (cmd_index > 0) {
                                    cmd_index--;
                                    term_put_char('\b', COLOR_TERM_WHITE);
                                    if (gui_active) {
                                        render_gui_with_mouse();
                                    }
                                }
                            } else {
                                if (cmd_index < CMD_BUFFER_SIZE - 1) {
                                    cmd_buffer[cmd_index++] = c;
                                    term_put_char(c, COLOR_TERM_WHITE);
                                    if (gui_active) {
                                        render_gui_with_mouse();
                                    }
                                }
                            }
                        }
                    }
                } else {
                    extended_scancode = 0;
                }
            }
        }
    }
}
