TARGET=server-http

CC = gcc
# CFLAGS = -fsanitize=address -fno-omit-frame-pointer -g -O1 -Wall -Wextra -O2 -Iinclude -I/usr/include/postgresql -I/usr/local/include/libchttpx -I/usr/include/hiredis
CFLAGS = -Wall -Wextra -O2 -Iinclude -I/usr/local/include -I/usr/include/postgresql -I/usr/include/hiredis
CLANG_FORMAT = clang-format

OBJDIR = .out
BINDIR = .build
LIN_LDFLAGS = -pthread -luuid -lchttpx -lpq -lcrypto -ls3 -lcurl -lssl -largon2 -lhiredis -lcjson -lmaxminddb
WIN_LDFLAGS = -lws2_32

LIN_SRCS = $(filter-out ./lib/cjson/cJSON.c, $(shell find . -name '*.c'))
WIN_SRCS = $(wildcard *.c) $(wildcard */*.c) $(wildcard */*/*.c)

LIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIN_SRCS))
WIN_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(WIN_SRCS))

# LINux build
# -

lin: $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(LIN_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(LIN_OBJS) $(LIN_LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# WINdows build
# -

win:
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) -D_WIN32 -mconsole $(WIN_SRCS) -o $(BINDIR)/$(TARGET).exe $(WIN_LDFLAGS)

# LINux run
# -

lin-run: lin
	$(BINDIR)/$(TARGET)

# WINdows run
# -

win-run: win
	$(BINDIR)\$(TARGET).exe

# LINux format
# -

lin-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(LIN_SRCS)

# WINdows format
# -

win-format:
	@echo ">> Formatting clang source files"
	$(CLANG_FORMAT) -i $(WIN_SRCS)

run: run-lin

clean:
	rm -rf $(OBJDIR) $(BINDIR) *.a *.dll
	rm -rf $(RELEASE_DIR)
	rm -rf libchttpx.so
