TARGET   = broadcast

CC       = gcc
CFLAGS   = -std=gnu99 -g -Wall -I.

LINKER   = gcc -o
LFLAGS   = -pthread -Wall -I. -lm

SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
rm       = rm -f

$(BINDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	@$(LINKER) $@ $(LFLAGS) $(OBJECTS)
	@echo "Linking complete"

$(OBJECTS): $(OBJDIR)/%.o:$(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@$(CC) $(CFLAGS) -c $< -o $@ $(CFLAGS)
	@echo "Compiled "$<" successfully"

.PHONEY: clean
clean:
	@$(rm) $(OBJECTS)
	@echo "Objects cleaned up"

.PHONEY: remove
remove: clean
	@$(rm) $(BINDIR)/$(TARGET)
	@echo "Executable cleaned up"
