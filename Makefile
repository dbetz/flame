CFLAGS_NO_MODEL=-Wall -Os
CFLAGS= $(CFLAGS_NO_MODEL) -mcmm

HDRS=\
fds.h \
encoder.h \
ws2812.h

OBJS=\
fds.o \
fds_driver.o \
encoder_fw.cog \
ws2812.o \
ws2812_init.o \
ws2812b_init.o \
ws2812_term.o \
ws2812_driver.o \
eeprom.o

TARGET=flame

all:	$(TARGET).elf

%.cog: %.c $(HDRS)
	@propeller-elf-gcc $(CFLAGS_NO_MODEL) -mcog -r -o $@ $<
	@propeller-elf-objcopy --localize-text --rename-section .text=$@ $@
	@echo $@

%.dat:	%.spin
	@openspin -c -o $@ $<
	@echo $@
	
%.o: %.dat
	@propeller-elf-objcopy -I binary -B propeller -O propeller-elf-gcc --rename-section .data=.text $< $@
	@echo $@
	
%.o: %.c $(HDRS)
	@propeller-elf-gcc $(CFLAGS) -c -o $@ $<
	@echo $@

$(TARGET).elf: $(TARGET).o $(OBJS)
	@propeller-elf-gcc $(CFLAGS) -o $@ $(TARGET).o $(OBJS)
	@echo $@

run:	$(TARGET).elf
	@propeller-load $(TARGET).elf -r -t
	
flash:	$(TARGET).elf
	@propeller-load $(TARGET).elf -e
	
clean:
	@rm -rf *.o *.cog *.a *.elf *.dat
