[org 0x7c00]
[bits 16]

; Layout de memoria:
;   0x0500  VbeModeInfoBlock (256 bytes)
;   0x7C00  Bootloader (pilha real-mode cresce para baixo a partir daqui)
;   0x10000 Kernel (codigo + dados + bss)
;   0x90000 Pilha do modo protegido
KERNEL_SEG    equ 0x1000
KERNEL_OFFSET equ 0x10000
KERNEL_SECTORS_1 equ 120        ; Primeiros 60 KB em 0x1000:0x0000 (0x10000..0x1EFFF)
KERNEL_SEG_2     equ 0x1F00
KERNEL_SECTORS_2 equ 120        ; Mais 60 KB em 0x1F00:0x0000 (0x1F000..0x2DFFF) -> Total 120 KB
VBE_INFO_ADDR equ 0x0500

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    ; 1. Le o kernel ANTES de trocar o modo de video (LBA estendido em 2 blocos)
    mov si, disk_address_packet
    mov dl, [BOOT_DRIVE]
    mov ah, 0x42
    int 0x13
    jc disk_error

    mov si, disk_address_packet_2
    mov dl, [BOOT_DRIVE]
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; 2. VBE 1024x768x24/32 LFB (0x4118), fallback 800x600 (0x4115)
    xor ax, ax
    mov es, ax
    mov di, VBE_INFO_ADDR
    mov cx, 0x4118
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne try_800x600
    mov bx, 0x4118
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    je enter_pm

try_800x600:
    xor ax, ax
    mov es, ax
    mov di, VBE_INFO_ADDR
    mov cx, 0x4115
    mov ax, 0x4F01
    int 0x10
    mov bx, 0x4115
    mov ax, 0x4F02
    int 0x10

enter_pm:
    ; 3. A20 rapido + modo protegido
    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm

disk_error:
    mov si, msg_disk
.print:
    lodsb
    test al, al
    jz .hang
    mov ah, 0x0E
    int 0x10
    jmp .print
.hang:
    hlt
    jmp .hang

align 4
disk_address_packet:
    db 0x10
    db 0
    dw KERNEL_SECTORS_1
    dw 0x0000           ; offset
    dw KERNEL_SEG       ; segmento -> 0x1000:0000 = 0x10000
    dd 1
    dd 0

align 4
disk_address_packet_2:
    db 0x10
    db 0
    dw KERNEL_SECTORS_2
    dw 0x0000           ; offset
    dw KERNEL_SEG_2     ; segmento -> 0x1F00:0000 = 0x1F000
    dd 121
    dd 0

gdt_start:
    dd 0x0, 0x0
gdt_code:
    dw 0xffff, 0x0
    db 0x0, 10011010b, 11001111b, 0x0
gdt_data:
    dw 0xffff, 0x0
    db 0x0, 10010010b, 11001111b, 0x0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000
    mov ebp, esp
    call KERNEL_OFFSET
    jmp $

BOOT_DRIVE db 0
msg_disk db "KazOS: erro de disco", 0

times 510-($-$$) db 0
dw 0xaa55
