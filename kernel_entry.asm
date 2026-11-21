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

    ; -------------------------------------------------------------
    ; Habilitação de Coprocessador FPU + Instruções Vetoriais SSE/AVX
    ; Garante que kernels de IA (.kzm) e cálculos vetoriais em Kaz
    ; não causem exceção #UD (Invalid Opcode)
    ; -------------------------------------------------------------
    ; 1. Configura CR0: Limpa Emulação (EM=bit 2) e ativa Monitor (MP=bit 1)
    mov eax, cr0
    and eax, 0xFFFFFFFB     ; Zera bit 2 (EM - Emulation)
    or  eax, 0x00000022     ; Seta bit 1 (MP - Monitor Coprocessor) e bit 5 (NE)
    mov cr0, eax

    ; 2. Configura CR4: Habilita OSFXSR (bit 9 - suporte a FXSAVE/FXRSTOR/SSE)
    ;    e OSXMMEXCPT (bit 10 - suporte a exceções SIMD unmasked)
    mov eax, cr4
    or  eax, (1 << 9) | (1 << 10)
    mov cr4, eax

    ; 3. Inicializa FPU para estado limpo
    fninit

    call kmain
.hang:
    hlt
    jmp .hang

