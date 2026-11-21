# Ferramentas do Sistema e Compilador Soberano Kaz
CC = gcc
LD = ld
ASM = nasm
QEMU = qemu-system-x86_64
KAZ = /home/armando/.kaz/bin/kaz

# Opções de compilação Bare-Metal 32-bit
CFLAGS = -m32 -march=i686 -mgeneral-regs-only -mno-red-zone -ffreestanding -fno-pie -fno-stack-protector -nostdlib -O2 -c
LDFLAGS = -m elf_i386 -T linker.ld --oformat binary

KAZ_MODULES = kaz_ui/tipos.kaz kaz_ui/widgets.kaz kaz_ui/shell.kaz kaz_ui/apps.kaz kaz_ui/desktop.kaz

all: run

# 1. Compila os módulos .kaz (Kaz-UI, Kaz-X e Shell) para código bare-metal usando kazos_cc.kaz
kaz_ui_gen.h: $(KAZ_MODULES) tools/kazos_cc.kaz
	$(KAZ) run tools/kazos_cc.kaz kaz_ui/desktop.kaz kaz_ui_gen.h

# 2. Cria a imagem final juntando o bootloader e o kernel (com padding seguro de disco)
os.img: boot.bin kernel.bin
	@size=$$(stat -c %s kernel.bin); if [ $$size -gt 122880 ]; then echo "ERRO: kernel.bin ($$size B) excede 240 setores lidos pelo boot.asm"; exit 1; fi
	cat boot.bin kernel.bin > os.img
	truncate -s 256K os.img

# 3. Compila o setor de boot (Assembly 16-bit MBR + VBE 1024x768)
boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

# 4. Compila o trampolim de entrada do kernel (Assembly 32-bit ELF)
kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

# 5. Compila o HAL do kernel integrando o código gerado pelo Kaz
kernel.o: kernel.c kaz_ui_gen.h font8x16.h
	$(CC) $(CFLAGS) kernel.c -o kernel.o

# 6. Faz a ligação (linkagem) dos objetos gerando o binário do kernel
kernel.bin: kernel_entry.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o kernel.o

# Executa no QEMU com display gráfico
run: os.img
	$(QEMU) -drive format=raw,file=os.img -display gtk

# Valida o toolkit Kaz-UI diretamente na VM do Kaz
test-kaz:
	$(KAZ) run kaz_ui/desktop.kaz

# Limpa ficheiros temporários gerados
clean:
	rm -f *.o *.bin os.img kaz_ui_gen.h
