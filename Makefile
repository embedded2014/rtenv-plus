CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
QEMU_STM32 ?= ../qemu_stm32/arm-softmmu/qemu-system-arm

ARCH = CM3
VENDOR = ST
PLAT = STM32F10x

LIBDIR = .
CMSIS_LIB=$(LIBDIR)/libraries/CMSIS/$(ARCH)
STM32_LIB=$(LIBDIR)/libraries/STM32F10x_StdPeriph_Driver

CMSIS_PLAT_SRC = $(CMSIS_LIB)/DeviceSupport/$(VENDOR)/$(PLAT)

ROMDIR = rom0

all: main.bin

main.bin: kernel.c context_switch.s syscall.s syscall.h kconfig.h \
			utils.h string.c string.h task.c task.h \
			memory-pool.c memory-pool.c file.c file.h pipe.h fifo.c fifo.h \
			mqueue.c mqueue.h block.c block.h path.c path.h romdev.c romdev.h \
			event-monitor.c event-monitor.h list.c list.h\
			$(ROMDIR).o
	$(CROSS_COMPILE)gcc \
		-DUSER_NAME=\"$(USER)\" \
		-Wl,-Tmain.ld,-Map=main.map -nostartfiles \
		-I . \
		-I$(LIBDIR)/libraries/CMSIS/CM3/CoreSupport \
		-I$(LIBDIR)/libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x \
		-I$(CMSIS_LIB)/CM3/DeviceSupport/ST/STM32F10x \
		-I$(LIBDIR)/libraries/STM32F10x_StdPeriph_Driver/inc \
		-fno-common -ffreestanding -O0 \
		-gdwarf-2 -g3 -Wall -Werror \
		-mcpu=cortex-m3 -mthumb \
		-o main.elf \
		\
		$(CMSIS_LIB)/CoreSupport/core_cm3.c \
		$(CMSIS_PLAT_SRC)/system_stm32f10x.c \
		$(CMSIS_PLAT_SRC)/startup/gcc_ride7/startup_stm32f10x_md.s \
		$(STM32_LIB)/src/stm32f10x_rcc.c \
		$(STM32_LIB)/src/stm32f10x_gpio.c \
		$(STM32_LIB)/src/stm32f10x_usart.c \
		$(STM32_LIB)/src/stm32f10x_exti.c \
		$(STM32_LIB)/src/misc.c \
		\
		context_switch.s \
		syscall.s \
		stm32_p103.c \
		kernel.c \
		memcpy.s string.c task.c memory-pool.c file.c fifo.c mqueue.c block.c \
		path.c romdev.c event-monitor.c list.c pipe.c \
		$(ROMDIR).o
	$(CROSS_COMPILE)objcopy -Obinary main.elf main.bin
	$(CROSS_COMPILE)objdump -S main.elf > main.list

$(ROMDIR).o: $(ROMDIR).bin
	$(CROSS_COMPILE)objcopy -I binary -O elf32-littlearm -B arm \
		--prefix-sections '.rom' $< $@

$(ROMDIR).bin: $(ROMDIR) mkromfs
	./mkromfs -d $< -o $@

$(ROMDIR):
	mkdir -p $(ROMDIR)

mkromfs: mkromfs.c
	gcc -o mkromfs mkromfs.c

qemu: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 -kernel main.bin

qemudbg: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 \
		-gdb tcp::3333 -S \
		-kernel main.bin


qemu_remote: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 -kernel main.bin -vnc :1

qemudbg_remote: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 \
		-gdb tcp::3333 -S \
		-kernel main.bin \
		-vnc :1

qemu_remote_bg: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 \
		-kernel main.bin \
		-vnc :1 &

qemudbg_remote_bg: main.bin $(QEMU_STM32)
	$(QEMU_STM32) -M stm32-p103 \
		-gdb tcp::3333 -S \
		-kernel main.bin \
		-vnc :1 &

emu: main.bin
	bash emulate.sh main.bin

qemuauto: main.bin gdbscript
	bash emulate.sh main.bin &
	sleep 1
	$(CROSS_COMPILE)gdb -x gdbscript&
	sleep 5

qemuauto_remote: main.bin gdbscript
	bash emulate_remote.sh main.bin &
	sleep 1
	$(CROSS_COMPILE)gdb -x gdbscript&
	sleep 5

clean:
	rm -f *.elf *.bin *.list *.map *.o
	rm -rf mkromfs
