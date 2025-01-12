## 简单解释：用 Fishhook 替换 `open` 函数。

| 步骤           | 解释                                                                 |
|----------------|----------------------------------------------------------------------|
| **步骤 1**     | 原本的 `open` 函数会执行文件打开操作。                               |
| **步骤 2**     | 用 Fishhook 替换成你自定义的 `open` 函数。                                |
| **步骤 3**     | 自定义的 `open` 函数；首先记录文件路径，然后调用原始的 `open` 函数。         |



## 克隆仓库
```bash
git clone https://github.com/pxx917144686/fishhook.h.git

cd fishhook

make
```


## 举例：创建一个简单的 Makefile 来编译
```bash
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
```
## 执行 make 生成一个静态库文件 libfishhook.a

## 将libfishhook.a 放到项目中 或者：~/theos/libfishhook.a

## 项目xxxxxxxxxxx的 Makefile 内容 需要添加 fishhook

```bash
xxxxxxxxxxx_LIBRARIES = fishhook
```

