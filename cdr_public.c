#include "cdr_include.h"


/* ---------------------------------------- 文件内部函数 ---------------------------------------- */
/* 不同类型的日志打印 */
char *cdr_log_type(int type)
{
    switch (type)
    {
        case CDR_LOG_INFO :
        return "INFO";
        
        case CDR_LOG_ERROR :
        return "ERROR";
        
        case CDR_LOG_DEBUG :
        return "DEBUG";
        
        default :
        return "NONE";
    }
}

/* 诊断日志打印：等待上一个日志的打印，多进程打印日志可能会冲突，这里要保护下，防止重入 */
void cdr_wait_last_diaglog_proc()
{
    if (!g_diaglog_record_busy)
    {
        return;
    }

    while (1)
    {
        if (!g_diaglog_record_busy)
        {    
            return;
        }
    }
    
    return;
}

/* 用户日志打印：等待上一个日志的打印，多进程打印日志可能会冲突，这里要保护下，防止重入 */
void cdr_wait_last_userlog_proc()
{
    if (!g_userlog_record_busy)
    {
        return;
    }

    while (1)
    {
        if (!g_userlog_record_busy)
        {
            return;
        }
    }
    
    return;
}

/* ---------------------------------------- 文件对外函数 ---------------------------------------- */

/* 获取系统时间，返回格式：2018-04-22 22:23:24 或 2018-04-22 22:23:24.999 */
void cdr_get_system_time(int type, char *time_info)
{
   time_t now_time;
   struct timeval time_ms;
   struct tm *info;
   int sec;

   /* 最小时间为秒级，格式为2018-04-22 22:23:24 */    
   time(&now_time);
   info = localtime(&now_time);
   if (info == 0)
   {
       cdr_diag_log(CDR_LOG_ERROR, "get_system_time info is NULL");
       return;
   }
   
   /* 最小时间为毫秒级，格式为2018-04-22 22:23:24.999 */    
   if (type == CDR_TIME_MS)
   {
        gettimeofday(&time_ms, NULL);
        sec = time_ms.tv_sec % 60;
        /*
            1、用localtime获取秒级时间，用gettimeofday获取ms级时间
            2、如果两者获取的“秒值”不一样，说明已经秒已经进位，需要调用localtime重新获取秒值。
        */        
        if (sec != info->tm_sec)
        {
            time(&now_time);
            info = localtime(&now_time);
            if (info == 0)
            {
                cdr_diag_log(CDR_LOG_ERROR, "get_system_time info is NULL");
                return;
            }
        }
        
        sprintf(time_info, "%u-%02u-%02u %02u:%02u:%02u.%03u", info->tm_year + 1900, info->tm_mon + 1, info->tm_mday, info->tm_hour, info->tm_min, info->tm_sec, (int)(time_ms.tv_usec / 1000));
        return;       
   }

   sprintf(time_info, "%u-%02u-%02u %02u:%02u:%02u", info->tm_year + 1900, info->tm_mon + 1, info->tm_mday, info->tm_hour, info->tm_min, info->tm_sec);
   return;
}

/* 获取文件大小，单位字节 */
unsigned long get_file_size(const char *file)  
{  
    unsigned long file_size = 0;
    struct stat stat_buff;
    
    if (stat(file, &stat_buff) < 0) /* 获取文件信息失败 */
    {  
        return file_size;  
    }

    file_size = stat_buff.st_size;
    return file_size;
}

/* 获取文件创建时间 */
unsigned long long get_file_create_time(const char *file)  
{  
    unsigned long long file_create_time = 0;
    struct stat stat_buff;
    
    if (stat(file, &stat_buff) < 0) /* 获取文件信息失败 */
    {  
        return file_create_time;  
    }

    file_create_time = stat_buff.st_ctime;
    return file_create_time;
}

/* 复制file_in内容到file_out */
int cdr_cpy_file(char *file_in, char *file_out)
{
    FILE *fp_in;
    FILE *fp_out;
    int c;
    
    if ((fp_in = fopen(file_in, "r")) == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_cpy_file faile, the file %s can not be opened.\r\n", file_in);
        return CDR_ERROR;
    }
    
    if ((fp_out = fopen(file_out, "a")) == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_cpy_file faile, the file %s can not be opened.\r\n", file_out);
        return CDR_ERROR;
    }
    
    /* 一个一个字符的复制，至到最后一个字符 */
    while (!feof(fp_in))
    {
        c = fgetc(fp_in);
        if (c == EOF)
        {
            break;
        }
        fputc(c, fp_out);
    }
    
    fclose(fp_in);
    fclose(fp_out);
    return CDR_OK;
}

/* 判断path目录下，file_name类型的文件个数 */
int cdr_get_file_num(char *path, char *file_name)
{
    DIR *dir = opendir(path);
    struct dirent *ptr = NULL;
    int file_num = 0;
    
    if (dir == NULL)
    {
        return file_num;
    }

	/* 循环读取目录 */    
    while((ptr = readdir(dir)) != NULL)
    {
        if(ptr->d_type == 8)    //8：代表文件file类型
        {
            if (strstr(ptr->d_name, file_name) != NULL)  /* 遍历的文件名，包含入参file_name，即满足条件 */
            {
                file_num++;
            }
        }
    }
    
    closedir(dir);
    return file_num;
}

/* 获取path目录下创建时间最久的文件，带出的file_name是有目录的 */
void cdr_get_oldest_filename(char *path, char *file_name)
{
    DIR *dir = opendir(path);
    struct dirent *ptr = NULL;
	unsigned long long file_create_time = 0;
	unsigned long long temp = 0;
    char name[200] = {0};
	char dir_file[200] = {0};
	
    if (dir == NULL)
    {
        return;
    }
    
	/* 循环读取目录 */
    while((ptr = readdir(dir)) != NULL)
    {
        if(ptr->d_type == 8)    //8：代表文件file类型
        {
			/* 目录+文件名，确认文件 */
			memset(dir_file, 0, sizeof(dir_file));
			sprintf(dir_file, "%s%s", path, ptr->d_name);
			
			/* 检查是否最旧的文件 */
			temp = get_file_create_time(dir_file);
			if ((file_create_time == 0) || (temp < file_create_time))
			{
				file_create_time = temp;
				memset(name, 0, sizeof(name));
				sprintf(name, "%s", dir_file);
			}
        }
    }
    
	sprintf(file_name, "%s", name);
    closedir(dir);
    return;
}

/* 记录用户日志，最后要保存到数据库Mysql中，对用户可见 */
void cdr_user_log(int type, int result)
{
    FILE *fp;
	char time_info[30] = {0};
	
	g_user_log_no_proc_times = 0;
	
    cdr_wait_last_userlog_proc();
    g_userlog_record_busy = 1;	
	
	if ((fp = fopen("/opt/myapp/cdr_recorder/log/log.log","a")) == NULL)
    {
        printf("The file %s can not be opened.\r\n", "log.log");
        g_userlog_record_busy = 0;
        return;
    }
	
	cdr_get_system_time(CDR_TIME_S, time_info);
	fprintf(fp, "'%s', '%u', '%s'\r\n", time_info, type, (result == CDR_OK) ? "pass" : "fail");
	fclose(fp);
	
	g_userlog_record_busy = 0;
	cdr_diag_log(CDR_LOG_DEBUG, "cdr_user_log ok, type=%u, result=%u", type, result);
    return;
}

/*
    1、诊断日志的记录，目录diag/diag.log；
    2、单个日志的文件大小，大于CDR_FILE_DIR_MAX_SIZE_BYTE时，另起新文件开始记录，防止单个文件过大
*/
void cdr_diag_log(int type, const char *format, ...)
{
    FILE *fp;
    va_list args;
    char time_info[30] = {0};
    char file_info[200] = {0};
    
    if (CDR_DEBUG_INVALID && (type == CDR_LOG_DEBUG))
    {
        return; /* 屏蔽debug的打印 */
    } 
    
    cdr_wait_last_diaglog_proc();
    g_diaglog_record_busy = 1;
    
    if ((fp = fopen("/opt/myapp/cdr_recorder/diag/diag.log","a")) == NULL)
    {
        printf("The file %s can not be opened.\r\n", "diag.log");
		printf("errno=%d\n",errno);
		printf("Mesg:%s\n",strerror(errno));
        g_diaglog_record_busy = 0;
        return;
    }
    
	/* 在日志开始加上时间 */
    cdr_get_system_time(CDR_TIME_MS, time_info);
    fprintf(fp, "%23s [%5s]: ", time_info, cdr_log_type(type));
    
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    
    fprintf(fp, ".\r\n");
    fclose(fp);

    /* 当日志大小大于CDR_FILE_DIR_MAX_SIZE_BYTE时，需要另起新文件记录 */
    if (get_file_size("/opt/myapp/cdr_recorder/diag/diag.log") >= CDR_FILE_DIR_MAX_SIZE_BYTE)
    {
		/* 预留的文件数量超过门限，删除最老的文件 */
		while (1)
		{
			if (cdr_get_file_num("/opt/myapp/cdr_recorder/diag/bf/", ".log") >= CDR_DIR_BF_MAX_NUM)
			{
				memset(file_info, 0 , sizeof(file_info));
				cdr_get_oldest_filename("/opt/myapp/cdr_recorder/diag/bf/", file_info);				
				remove(file_info);
				printf("delete oldest diaglog file %s\r\n", file_info);
				continue;
			}
			break;
		}
		
		memset(file_info, 0 , sizeof(file_info));
        sprintf(file_info, "/opt/myapp/cdr_recorder/diag/bf/diag%s.log", time_info);
        rename("/opt/myapp/cdr_recorder/diag/diag.log", file_info);
    }
    
    g_diaglog_record_busy = 0;
    return;
}

/* 获取硬盘总大小，单位M */
int cdr_get_disk_size_total()
{
    int size = 0;
    struct statfs disk_info;
    unsigned long long block_size;
    unsigned long long total_size;
    
    statfs("/opt/myapp", &disk_info); 
    block_size = disk_info.f_bsize;
    total_size = block_size * disk_info.f_blocks;
    
    size = (int)(total_size >> 20); /* 转换为M */
    return size;
}

/* 获取硬盘可用空间大小，单位M */
int cdr_get_disk_size_free()
{
    int size = 0;
    struct statfs disk_info;
    unsigned long long block_size;
    unsigned long long total_size;
    
    statfs("/opt/myapp", &disk_info); 
    block_size = disk_info.f_bsize;
    total_size = block_size * disk_info.f_bfree;
    
    size = (int)(total_size >> 20); /* 转换为M */
    return size;
}

/* 十六进制字符串转整形 */
int cdr_hex_to_int(char *hex_info)  
{  
    int i;  
    int n = 0;  

    for (i = 0; (hex_info[i] >= '0' && hex_info[i] <= '9') || (hex_info[i] >= 'a' && hex_info[i] <= 'z') || (hex_info[i] >='A' && hex_info[i] <= 'Z'); i++)  
    {  
        if (tolower(hex_info[i]) > '9')  
        {  
            n = 16 * n + (10 + tolower(hex_info[i]) - 'a');  
        }  
        else  
        {  
            n = 16 * n + (tolower(hex_info[i]) - '0');  
        }  
    }
    return n;  
}  

void cdr_set_led_state(int type)
{
	/* 状态灯未变化，避免重复设置 */
	if (g_led_state.state == type)
	{
		return;
	}	
	cdr_diag_log(CDR_LOG_INFO, "cdr_set_led_state led change, old=0x%x, new=0x%x", g_led_state.state, type);
	
	/* 设置状态灯 */
	memset(&g_led_state, 0, sizeof(g_led_state));
    g_led_state.state = type;	
	switch (type)
	{
		/* 红灯常亮 */
		case CDR_LED_RED_CONTINUOUS : 
		sprintf(g_led_state.set_red, "1");
		sprintf(g_led_state.set_green, "0");
		sprintf(g_led_state.set_yellow, "0");
		g_led_state.is_flash = 0;
		break;
		
		/* 红灯闪烁 */
		case CDR_LED_RED_FLASH :
		sprintf(g_led_state.set_red, "1");
		sprintf(g_led_state.set_green, "0");
		sprintf(g_led_state.set_yellow, "0");
		g_led_state.is_flash = 1;
		break;
		
		/* 黄灯常亮 */
		case CDR_LED_YELLOW_CONTINUOUS :
		sprintf(g_led_state.set_red, "0");
		sprintf(g_led_state.set_green, "0");
		sprintf(g_led_state.set_yellow, "1");
		g_led_state.is_flash = 0;
		break;
		
		/* 黄灯闪烁 */
		case CDR_LED_YELLOW_FLASH :
		sprintf(g_led_state.set_red, "0");
		sprintf(g_led_state.set_green, "0");
		sprintf(g_led_state.set_yellow, "1");
		g_led_state.is_flash = 1;
		break;
		
		/* 绿灯闪烁 */
		case CDR_LED_GREEN_FLASH :
		sprintf(g_led_state.set_red, "0");
		sprintf(g_led_state.set_green, "1");
		sprintf(g_led_state.set_yellow, "0");
		g_led_state.is_flash = 1;
		break;
		
		/* 绿灯常亮 */
		case CDR_LED_GREEN_CONTINUOUS :
		sprintf(g_led_state.set_red, "0");
		sprintf(g_led_state.set_green, "1");
		sprintf(g_led_state.set_yellow, "0");
		g_led_state.is_flash = 0;
		break;
				
		default :
		break;
	}    
    
    return;
}

/* 设置系统重启 */
void cdr_system_reboot()
{
    cdr_diag_log(CDR_LOG_INFO, "cdr_system_reboot now==============================");
    //reboot(RB_AUTOBOOT);
    return;
}

void cdr_led_control()
{
	int fd_red;
	int fd_green;
	int fd_yellow;

	cdr_diag_log(CDR_LOG_INFO, "cdr_led_control >>>>>>>>>>>>>>>>>>>>>>>>>>in");
	
	/* 打开文件进行操作，控制led状态 */
	fd_red    = open("/sys/class/gpio/gpio49/value", O_RDWR);
	if (fd_red < 0)
	{
		cdr_diag_log(CDR_LOG_ERROR, "cdr_led_control open gpio value error red %d", fd_red);
		return;
	}	
	fd_green  = open("/sys/class/gpio/gpio50/value", O_RDWR);
	if (fd_green < 0)
	{
		cdr_diag_log(CDR_LOG_ERROR, "cdr_led_control open gpio value error green %d", fd_green);
		close(fd_red);
		return;
	}	
	fd_yellow = open("/sys/class/gpio/gpio102/value", O_RDWR);
	if (fd_yellow < 0)
	{
		cdr_diag_log(CDR_LOG_ERROR, "cdr_led_control open gpio value error yellow %d", fd_yellow);
		close(fd_red);
		close(fd_yellow);
		return;
	}	
	
    while (1) 
    {
		write(fd_red,    g_led_state.set_red,    1);
		write(fd_green,  g_led_state.set_green,  1);
		write(fd_yellow, g_led_state.set_yellow, 1);
		sleep(1);
		
		/* 处理闪烁 */
		if (g_led_state.is_flash)
		{
			write(fd_red,    "0", 1);
			write(fd_green,  "0", 1);
			write(fd_yellow, "0", 1);
			sleep(1);			
		}
	}
	
	close(fd_red);
	close(fd_green);
	close(fd_yellow);
	return;
}
