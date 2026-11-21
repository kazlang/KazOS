[bits 32]
[extern kmain]
[extern __bss_start]
[extern __bss_end]

section .text.entry
global _start
_start:
    ; Zera a .bss (nao vem do disco)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kmain
.hang:
    hlt
    jmp .hang
