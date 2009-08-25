BUILD=../../build
DISK=$(BUILD)/disk.img
DISKMOUNT=../../disk
BIN=$(BUILD)/service_$(NAME).bin
DEP=$(BUILD)/service_$(NAME).dep
LIBC=../../libc
LDCONF=$(LIBC)/ld.conf

CC = gcc
CFLAGS = -nostdlib -nostartfiles -nodefaultlibs -I$(LIBC)/include -I../../lib/h -Wl,-T,$(LDCONF) $(CDEFFLAGS) 
CSRC=$(wildcard *.c)

LIBCA=$(BUILD)/libc.a
START=$(BUILD)/libc_startup.o
COBJ=$(patsubst %.c,$(BUILD)/service_$(NAME)_%.o,$(CSRC))

.PHONY: all clean

all:	$(BIN)

$(BIN):	$(LDCONF) $(COBJ) $(START) $(LIBCA)
		@echo "	" LINKING $(BIN)
		@$(CC) $(CFLAGS) -o $(BIN) $(START) $(COBJ) $(LIBCA);
		@echo "	" COPYING ON DISK
		@make -C ../../ mounthdd
		@$(SUDO) cp $(BIN) $(DISKMOUNT)/sbin/$(NAME)
		@make -C ../../ umounthdd

$(BUILD)/service_$(NAME)_%.o:		%.c
		@echo "	" CC $<
		@$(CC) $(CFLAGS) -o $@ -c $<

$(DEP):	$(CSRC)
		@echo "	" GENERATING DEPENDENCIES
		@$(CC) $(CFLAGS) -MM $(CSRC) > $(DEP);
		@# prefix all files with the build-path (otherwise make wouldn't find them)
		@sed --in-place -e "s/\([a-zA-Z_]*\).o:/$(subst /,\/,$(BUILD)\/service_$(NAME)_)\1.o:/g" $(DEP);

-include $(DEP)

clean:
		rm -f $(BIN) $(COBJ) $(DEP)
