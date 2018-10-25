# 指定编译器  
CC = /opt/gcc-4.4.4-glibc-2.11.1-multilib-1.0/arm-fsl-linux-gnueabi/bin/arm-linux-gcc

# CFLAG包括头文件目录  
CFLAGS = -I/mnt/code/mysql/include/mysql

# 头文件查找路径  
INC = -I./

# 静态链接库  
LDFLAGS = -L/mnt/code/mysql/lib/mysql
LDLIBS = -lmysqlclient -lpthread

# 目标  
TARGET = can_data_record

# 源文件  
SRC = cdr_main.c \
      mysql_record.c \
      file_record.c \
      cdr_public.c \
      cdr_fmea.c

# 源文件编译为目标文件  
OBJS = $(SRC:.c=.o)  

# 链接为可执行文件  
$(TARGET): $(OBJS)  
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)  

# 清除可执行文件和目标文件  
clean:  
	rm -f $(OBJS)  
	rm -f $(TARGET)  

# 编译规则 加入头文件 $@代表目标文件 $< 代表第一个依赖文件  
%.o:%.c  
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<  