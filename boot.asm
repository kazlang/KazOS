[org 0x7c00]
[bits 16]

KERNEL_SEG       equ 0x1000
KERNEL_OFFSET    equ 0x10000
KERNEL_SECTORS_1 equ 120
KERNEL_SEG_2     equ 0x1F00
KERNEL_SECTORS_2 equ 120
VBE_INFO_ADDR    equ 0x0500

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [BOOT_DRIVE], dl

    ; 1. Le o kernel ANTES de trocar de modo de video (240 setores)
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

    ; 2. Exibe Menu de Resolucao
    mov si, msg_menu
    call print_str

    ; Timeout de ~3s (55 ticks BIOS)
    xor ax, ax
    int 0x1a
    add dx, 55
    mov bx, dx

.wait_key:
    mov ah, 1
    int 0x16
    jnz .read_key

    xor ax, ax
    int 0x1a
    cmp dx, bx
    jb .wait_key
    mov al, 49          ; "1" -> 1024x768
    jmp .apply

.read_key:
    xor ah, ah
    int 0x16

.apply:
    mov bx, 0x4118      ; padrao: 1024x768x32
    cmp al, 50          ; "2"
    je .set_720p
    cmp al, 51          ; "3"
    je .set_1080p
    cmp al, 52          ; "4"
    je .set_800
    jmp .set_vbe

.set_720p:
    mov bx, 0x418F      ; 1280x720x32 LFB
    jmp .set_vbe

.set_1080p:
    mov bx, 0x4192      ; 1920x1080x32 LFB
    jmp .set_vbe

.set_800:
    mov bx, 0x4115      ; 800x600x32 LFB

.set_vbe:
    mov cx, bx
    mov di, VBE_INFO_ADDR
    mov ax, 0x4F01      ; Get Mode Info
    int 0x10
    cmp ax, 0x004F
    jne .fallback

    mov ax, 0x4F02      ; Set VBE Mode
    int 0x10
    cmp ax, 0x004F
    je enter_pm

.fallback:
    ; Fallback universal para 1024x768 (ou 800x600 se falhar)
    mov bx, 0x4118
    mov cx, 0x4118
    mov di, VBE_INFO_ADDR
    mov ax, 0x4F01
    int 0x10
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    je enter_pm

    mov bx, 0x4115
    mov cx, 0x4115
    mov di, VBE_INFO_ADDR
    mov ax, 0x4F01
    int 0x10
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
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm

print_str:
    lodsb
    test al, al
    jz .ret
    mov ah, 0x0E
    int 0x10
    jmp print_str
.ret:
    ret

disk_error:
    mov si, msg_disk
    call print_str
.hang:
    hlt
    jmp .hang

align 4
disk_address_packet:
    db 0x10, 0
    dw KERNEL_SECTORS_1, 0x0000, KERNEL_SEG
    dd 1, 0

align 4
disk_address_packet_2:
    db 0x10, 0
    dw KERNEL_SECTORS_2, 0x0000, KERNEL_SEG_2
    dd 121, 0

gdt_start:
    dd 0, 0
gdt_code:
    dw 0xffff, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xffff, 0
    db 0, 10010010b, 11001111b, 0
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
msg_disk db "Erro disco", 0
msg_menu db 13, 10, "=== KazOS Resolution ===", 13, 10
         db " [1] 1024x768 (Default)", 13, 10
         db " [2] 1280x720 (16:9 HD)", 13, 10
         db " [3] 1920x1080 (16:9 Full HD)", 13, 10
         db " [4] 800x600 (4:3)", 13, 10
         db " Escolha [1-4] (Auto 3s): ", 0

times 510-($-$$) db 0
dw 0xaa55
