# 设置输出目录为目标位置
LIBRARY_NAME = libfishhook.a
SRC_FILES = fishhook.c

# 编译目标文件
CC = clang
CFLAGS = -Wall -Werror -fPIC
LIBRARY_OUTPUT = $(CURDIR)/$(LIBRARY_NAME)

all:
	$(CC) $(CFLAGS) -c $(SRC_FILES) -o fishhook.o
	ar rcs $(LIBRARY_OUTPUT) fishhook.o

clean:
	rm -f *.o $(LIBRARY_NAME)
