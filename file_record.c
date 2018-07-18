#include "cdr_include.h"



/*
can data ---->>>> cache.0 ----->>>> cache*.1 ----->>>> cache.2 ----->>>> mysql
    cache.0   读取数据的缓存文件，最大缓存1K，然后转换为cache.1文件
    cache*.1  需要记录到mysql数据文件的过渡文件,单个文件最大为CDR_FILE_DIR_MAX_SIZE_BYTE
    cache*.2  需要记录到mysql的数据文件    
*/

/* ---------------------------------------- 文件内部函数 ---------------------------------------- */

/* 当文件cache.1大于CDR_FILE_DIR_MAX_SIZE_BYTE，需要另存为新的文件，防止cache.1文件过大 */
void cdr_cache1_to_cache1_proc()
{
    int i;
    int cache1_num = 0;
    char name_old[200] = {0};
    char name_new[200] = {0};

    if (get_file_size("/opt/myapp/cdr_recorder/file/cache.1") < CDR_FILE_DIR_MAX_SIZE_BYTE)
    {
        return;
    }
    
    /* 当cache1文件大于CDR_FILE_DIR_MAX_SIZE_BYTE时，需要另起新文件记录 */
    cache1_num = cdr_get_file_num("/opt/myapp/cdr_recorder/file/", ".1"); /* 获取当前已经存在的cache.1文件 */
    if (cache1_num < 1)
    {
        /* 当前目录无cache文件 */
        cdr_diag_log(CDR_LOG_ERROR, "The file cdr_cache1_to_cache1_proc error, cache1_num is null");
        return;
    }
    
    /* 当前只有1个cache.1文件，将cache.1改为cache1.1 */
    if (cache1_num == 1) 
    {
        rename("/opt/myapp/cdr_recorder/file/cache.1", "/opt/myapp/cdr_recorder/file/cache1.1");
        cdr_diag_log(CDR_LOG_DEBUG, "The file  add new file cache1.1");
        return;
    }
    
    /* 当前有多个个cache.1文件，例如有3个，将cache2.1改为cache3.1, 将cache1.1改为cache2.1,将cache.1改为cache1.1, */
    for (i = cache1_num; i > 1; i--)
    {
        sprintf(name_old, "/opt/myapp/cdr_recorder/file/cache%d.1", i - 1);
        sprintf(name_new, "/opt/myapp/cdr_recorder/file/cache%d.1", i);
        rename(name_old, name_new);
    }
    rename("/opt/myapp/cdr_recorder/file/cache.1", "/opt/myapp/cdr_recorder/file/cache1.1");
    
    cdr_diag_log(CDR_LOG_DEBUG, "The file  add new file cache%u.1", cache1_num);    
    return;
}

void cdr_cache0_to_cache1_proc()
{
    /* 其他进程正在操作cache1，直接返回 */
    if (g_cache1_file_busy == 1)
    {
        return;
    }
    g_cache1_file_busy = 1;
    
    
    /* cache1不存在，直接cache0转换为cache1 */
    if (access("/opt/myapp/cdr_recorder/file/cache.1", F_OK) != 0)
    {
        rename("/opt/myapp/cdr_recorder/file/cache.0", "/opt/myapp/cdr_recorder/file/cache.1");        
        g_cache1_file_busy = 0;
        
        cdr_diag_log(CDR_LOG_DEBUG, "The file add new cache.1");
        return;
    }
    
    /* cache1存在，cache0数据添加到cache1 */
    (void)cdr_cpy_file("/opt/myapp/cdr_recorder/file/cache.0", "/opt/myapp/cdr_recorder/file/cache.1");
    remove("/opt/myapp/cdr_recorder/file/cache.0"); /* 删除cache.0 */
    
    cdr_cache1_to_cache1_proc(); /* 防止cache.1文件过大 */
    
    g_cache1_file_busy = 0;
    return;
}

/* 根据can数据的实际长度，转换成字符串，
   每一字节数值小于0xf的前面补加0，如0x123，长度2字节，转换后“0123“ */
void cdr_can_data_to_str_by_len(long long can_data, int data_len, char *data_str)
{
    switch (data_len) /* 最长8个字节 */
    {
        case 1 :
        sprintf(data_str, "%02llx", can_data);
        return;
        
        case 2 :
        sprintf(data_str, "%04llx", can_data);
        return;

        case 3 :
        sprintf(data_str, "%06llx", can_data);
        return;
        
        case 4 :
        sprintf(data_str, "%08llx", can_data);
        return;
        
        case 5 :
        sprintf(data_str, "%010llx", can_data);
        return;
        
        case 6 :
        sprintf(data_str, "%012llx", can_data);
        return;
        
        case 7 :
        sprintf(data_str, "%014llx", can_data);
        return;
        
        case 8 :
        sprintf(data_str, "%016llx", can_data);
        return;
        
        default :
        return;        
    }

    return;
}

/* 将获取到的can数据写到缓存文件cache.0 */
void cdr_write_can_data_to_file(cdr_can_frame_t *data)
{
    FILE *fp;
    char time_info[30] = {0};
    char data_info[20] = {0};

    g_write_file_no_proc_times = 0;
    if ((fp = fopen("/opt/myapp/cdr_recorder/file/cache.0","a")) == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "The file %s can not be opened", "cache.0");
        g_system_event_occur[CDR_EVENT_FILE_RECORD_FAULT] = 1;
        return;
    }
    
    /* 讲数据添加时间戳写入缓存文件cache.0 */
    cdr_get_system_time(CDR_TIME_MS, time_info);
    cdr_can_data_to_str_by_len(data->data, data->len, data_info);
    fprintf(fp, "'%02x','%s','%x','%x','%02x','%02x','%s','%02x'\r\n", 
        CDR_ID_TO_PF(data->id), time_info, CDR_ID_TO_PRIORITY(data->id), CDR_ID_TO_RSV(data->id), CDR_ID_TO_PS_DA(data->id), CDR_ID_TO_SA(data->id),
        data_info, data->len); /* 数据均是十六进制保存 */
    fclose(fp);
    
    cdr_diag_log(CDR_LOG_DEBUG, "The cdr_write_can_data_to_file pf=%x data=%llx", CDR_ID_TO_PF(data->id), data->data);
    
    /* 当缓存文件cache.0大于CDR_CACHE0_MAX_SIZE_BYTE时，需要另起新文件记录 */
    if (get_file_size("/opt/myapp/cdr_recorder/file/cache.0") >= CDR_CACHE0_MAX_SIZE_BYTE)
    {
        cdr_cache0_to_cache1_proc(); /* 将文件cache.0内容添加到cache.1文件 */
    }    
    
    g_system_event_occur[CDR_EVENT_FILE_RECORD_FAULT] = 0;
    return;
}

void cdr_simtest_time_data_proc(cdr_can_frame_t *data)
{
    FILE *fp;
    char time_info[100] = {0};    
    int year, mon, day, hour, min, sec;
    
    if ((fp = fopen("/opt/myapp/test/time_data_sim", "r")) == NULL) /* 文件打开失败 */
    {
        return;
    }
    
    fgets(time_info, 100, fp);
    if (time_info[0] == '\0')
    {
        fclose(fp);    
        return;
    }
    
    year = 0;
    mon = 0;
    day = 0;
    hour = 0;
    min = 0;
    sec = 0;
    cdr_diag_log(CDR_LOG_DEBUG, "time_info %s", time_info);
    sscanf(time_info, "%d-%d-%d-%d-%d-%d", &year, &mon, &day, &hour, &min, &sec);
    
    data->id = CDR_CAN_DATA_TIME_CALIBRATION_PF << 16;
    data->data = (long long)year << 56;
    data->data |= (long long)mon << 48; 
    data->data |= (long long)day << 40; 
    data->data |= (long long)hour << 32; 
    data->data |= (long long)min << 24; 
    data->data |= (long long)sec << 16; 

    cdr_diag_log(CDR_LOG_DEBUG, "send---------------------------------------------id 0x%x  -------data 0x%x", data->id, data->data);    
    cdr_diag_log(CDR_LOG_DEBUG, "send-%d-%d-%d-%d-%d-%d", year, mon, day, hour, min, sec);
    
    fclose(fp);    
    remove("/opt/myapp/test/time_data_sim");
    return;
}

void cdr_simtest_can_data_proc(cdr_can_frame_t *data)
{
    FILE *fp;
    FILE *fp_tmp;
    char str_line[100] = {0};
    char str_line_temp[100] = {0};
    char can_id[10] = {0};
    int num = 0;
    int first_line = 1;
    
    fp = fopen("/opt/myapp/test/can_data_sim", "r");
    fp_tmp = fopen("/opt/myapp/test/can_data_sim_temp", "w");
    if (fp == NULL || fp_tmp == NULL)
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_simtest_can_data_proc open file fail");
        return;
    }    
    
    first_line    = 1;    
    while (!feof(fp))
    {
        memset(str_line, 0, sizeof(str_line));
        fgets(str_line, 1000, fp);    
        
        /* 最后一行 */
        if (str_line[0] == '\0' || str_line[0] == '\n')
        {
            break;
        }    
        
        if (first_line == 1)
        {
            sscanf(str_line, "%8s,%d;", can_id, &num);
            if (num <= 0)
            {                
                continue;
            }
            
            data->id = cdr_hex_to_int(can_id);
            data->data = num;
            cdr_diag_log(CDR_LOG_DEBUG, "send---------------------------------------------id %s  -------data %u", can_id, num);
            num--;
            sprintf(str_line_temp, "%8s,%d;\n", can_id, num);
            first_line = 0;
            continue;
        }
        
        fprintf(fp_tmp, "%s", str_line);    
    }
    
    if (first_line == 1)
    {
        fclose(fp);
        fclose(fp_tmp);
        remove("/opt/myapp/test/can_data_sim");
        remove("/opt/myapp/test/can_data_sim_temp");
        return;
    }    
    
    fprintf(fp_tmp, "%s", str_line_temp);
    fclose(fp);
    fclose(fp_tmp);
    
    remove("/opt/myapp/test/can_data_sim");
    rename("/opt/myapp/test/can_data_sim_temp", "/opt/myapp/test/can_data_sim");    
    return;    
}

void cdr_simtest_heart_data_proc(cdr_can_frame_t *data)
{
    FILE *fp;
    FILE *fp_tmp;
    char str_line[100] = {0};
    char str_line_temp[100] = {0};
    char sa_hex[5] = {0};
    int sec = 0;
    int sa = 0;
    int first_line = 1;
    int index = 0;
    int i;
    time_t now_time;
    struct tm info;    

    fp = fopen("/opt/myapp/test/heart_data_sim", "r");
    fp_tmp = fopen("/opt/myapp/test/heart_data_sim_temp", "w");
    if (fp == NULL || fp_tmp == NULL)
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_simtest_heart_data_proc open file fail");
        return;
    }    
    
    first_line    = 1;    
    while (!feof(fp))
    {
        memset(str_line, 0, sizeof(str_line));
        fgets(str_line, 1000, fp);
        
        /* 最后一行 */
        if (str_line[0] == '\0' || str_line[0] == '\n')
        {
            break;
        }    
        
        if (first_line == 1)
        {
            sscanf(str_line, "%2s,%d;", sa_hex, &sec);
            sa = cdr_hex_to_int(sa_hex);        
            index = 0;
            for (i = 1; i < g_dev_run_time.num; i++)
            {
                if (sa == g_dev_run_time.sa[i])
                {
                    index = i;
                    break;
                }
            }
            
            if (index != 0)
            {
                /* 判断是否超时 */
                time(&now_time);
                sscanf(g_dev_run_time.power_on_time[index], "%d-%d-%d %d:%d:%d", &info.tm_year, &info.tm_mon, &info.tm_mday, &info.tm_hour, &info.tm_min, &info.tm_sec);        
                info.tm_year = info.tm_year - 1900;
                info.tm_mon = info.tm_mon - 1;
                if (now_time - mktime(&info) > sec)
                {
                    continue;
                }            
            }
            
            data->id = sa | (CDR_CAN_DATA_HEART_BEAT_PF << 16);
            data->data = sec;
            cdr_diag_log(CDR_LOG_DEBUG, "send---------------------------------------------id 0x%x  -------data %u", data->id, sec);            

            sprintf(str_line_temp, "%s", str_line);    
            first_line = 0;
            continue;
        }
        fprintf(fp_tmp, "%s", str_line);    
    }    
        
    if (first_line == 1)
    {
        fclose(fp);
        fclose(fp_tmp);
        remove("/opt/myapp/test/heart_data_sim");
        remove("/opt/myapp/test/heart_data_sim_temp");
        return;
    }
    
    fprintf(fp_tmp, "%s", str_line_temp);
    fclose(fp);
    fclose(fp_tmp);
    
    remove("/opt/myapp/test/heart_data_sim");
    rename("/opt/myapp/test/heart_data_sim_temp", "/opt/myapp/test/heart_data_sim");
    return;    
}

int cdr_receive_can_data_simtest(cdr_can_frame_t *data)
{
    int sim_info;
    
    /* 没有模拟数据 */
    if ((access("/opt/myapp/test/can_data_sim", F_OK) != 0) && (access("/opt/myapp/test/heart_data_sim", F_OK) != 0)
        && (access("/opt/myapp/test/time_data_sim", F_OK) != 0))
    {
        return CDR_ERROR;
    }
    
    /* 时间校准存在，首先处理时间校准数据 */
    if (access("/opt/myapp/test/time_data_sim", F_OK) == 0) /* 模拟can数据 */
    {
        cdr_simtest_time_data_proc(data);
        return CDR_OK;
    }
    
    /* 两类数据同时存在，随机处理一个 */
    if ((access("/opt/myapp/test/can_data_sim", F_OK) == 0) && (access("/opt/myapp/test/heart_data_sim", F_OK) == 0))
    {
        sim_info = rand() % 2;
        if (sim_info == 0)
        {
            cdr_simtest_can_data_proc(data);
            return CDR_OK;
        }
        else
        {
            cdr_simtest_heart_data_proc(data);
            return CDR_OK;            
        }
    }

    if (access("/opt/myapp/test/can_data_sim", F_OK) == 0) /* 模拟can数据 */
    {
        cdr_simtest_can_data_proc(data);
        return CDR_OK;
    }    
    
    if (access("/opt/myapp/test/heart_data_sim", F_OK) == 0) /* 模拟心跳数据 */
    {
        cdr_simtest_heart_data_proc(data);
        return CDR_OK;
    }    
    
    return CDR_ERROR;
}

int cdr_receive_can_data(cdr_can_frame_t *data)
{
    int i;
    int ret;
    int read_bytes = 0;
    long long val = 0;
    struct can_frame frame;
    struct timeval time_out;
    fd_set rset;    

     /* 数据模拟 */
    if (cdr_receive_can_data_simtest(data) == CDR_OK)
    {
        if (data->id == 0 && data->data == 0)
        {
            return CDR_ERROR;
        }
        return CDR_OK;
    }
    
    /* 检测can数据，最大检测时间1s */
    FD_ZERO(&rset);
    FD_SET(g_socket_fd, &rset);    
    time_out.tv_sec = 1;
    time_out.tv_usec = 0;
    ret = select((g_socket_fd + 1), &rset, NULL, NULL, &time_out);
    if (ret == 0)
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_receive_can_data select time_out");
        return CDR_ERROR;
    }    
    
    /* 接受报文 */
    memset(&frame, 0 , sizeof(frame));
    read_bytes = read(g_socket_fd, &frame, sizeof(frame));
    if (read_bytes < sizeof(frame))     /* 有问题报文 */
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_receive_can_data frame error, read_bytes=0x%x(0x%x)", read_bytes, sizeof(frame));
        return CDR_ERROR;
    }
    
    if (frame.can_dlc > 8) /* 长度不对 */
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_receive_can_data frame data len err, len=%u", frame.can_dlc);
        return CDR_ERROR;
    }
    
    /* 数据赋值，带出 */
    data->id = frame.can_id;
    data->len = frame.can_dlc;    
    for (i = 0; i < data->len; i++)
    {
        val = frame.data[i];
        data->data |= val << (8 * (data->len - 1 - i)); /* 8个1字节数据，转换成long long */
    }
    
    cdr_diag_log(CDR_LOG_DEBUG, "cdr_receive_can_data id=0x%08x  len=0x%x data=0x%llx", data->id, data->len, data->data);
    return CDR_OK;
}

#define CDR_CACHE0_PROC_WAIT_MAX_TIMES           2
void cdr_no_can_data_proc()
{
    /* 连续CDR_CACHE0_PROC_WAIT_MAX_TIMES个周期未接收到can数据，将缓存到cache0文件，转换成cache1文件 */
    g_write_file_no_proc_times++;
    if (g_write_file_no_proc_times < CDR_CACHE0_PROC_WAIT_MAX_TIMES)
    {
        return;
    }
    
    if (access("/opt/myapp/cdr_recorder/file/cache.0", F_OK) == 0) /* cache.0文件存在 */
    {
        cdr_cache0_to_cache1_proc();
    }
    else
    {
        g_write_file_no_proc_times = CDR_CACHE0_PROC_WAIT_MAX_TIMES;
    }
    
    return;
}

void cdr_time_calibration_proc(cdr_can_frame_t *data)
{
    char time_info[50] = {0};
    int year, mon, day, hour, min, sec, usec;
    int i;
    
    g_dev_time_calibration_busy = 1;
    
    usec  =  (data->data & 0xffff);
    sec   =  (data->data >> 16) & 0xff;
    min   = (data->data >> 24) & 0xff;
    hour  = (data->data >> 32) & 0xff;
    day   = (data->data >> 40) & 0xff;
    mon   = (data->data >> 48) & 0xff;
    year  = ((data->data >> 56) & 0xff) + 100;
    
    while (1)
    {
        if (!g_dev_run_time_record_busy) /* 等待之前的时间记录 */
        {
            break;
        }
    }    
    
    for (i = 0; i < g_dev_run_time.num; i++)
    {
        cdr_get_system_time(CDR_TIME_S, g_dev_run_time.calibration_before_time[i]);
        sprintf(g_dev_run_time.calibration_after_time[i], "%u-%02u-%02u %02u:%02u:%02u", year + 1900, mon, day, hour, min, sec);
    }
    
    memset(time_info, 0, sizeof(time_info));
    sprintf(time_info, "date %04u.%02u.%02u-%02u:%02u:%02u", year + 1900, mon, day, hour, min, sec);
    system(time_info);
    system("hwclock -w"); /* 同步到硬件时钟 */

    cdr_diag_log(CDR_LOG_INFO, "cdr_time_calibration_proc set time %u-%02u-%02u %02u:%02u:%02u.%03u", 
        year + 1900, mon, day, hour, min, sec, usec);
    cdr_user_log(CDR_USR_LOG_TYPE_TIME_CALIBRATION, CDR_OK);
    
    g_dev_time_calibration_busy = 0;
    return;
}

void cdr_dev_running_time_proc()
{
    int i;
    int j;
    time_t now_time;
    struct tm info;
    
    time(&now_time);
    
    /* 0是测试仪自己，因此从1开始查看 */
    for (i = 1; i < g_dev_run_time.num; i++)
    {
        /* 超出门限，认为设备关机*/
        sscanf(g_dev_run_time.power_off_time[i], "%d-%d-%d %d:%d:%d", &info.tm_year, &info.tm_mon, &info.tm_mday, &info.tm_hour, &info.tm_min, &info.tm_sec);        
        info.tm_year = info.tm_year - 1900;
        info.tm_mon = info.tm_mon - 1;

        cdr_diag_log(CDR_LOG_DEBUG, "cdr_dev_running_time_proc dev 0x%x off timediff %d", g_dev_run_time.sa[i], (now_time - mktime(&info)));
        if ((now_time - mktime(&info) > CDR_DEV_RUNNING_TIME_THRESHOLD) && (g_dev_run_time.is_update[i] == 0)) /* 超出门限，并且没有需要记录的数据 */
        {
            cdr_diag_log(CDR_LOG_INFO, "cdr_dev_running_time_proc dev 0x%x power off", g_dev_run_time.sa[i]);
            for (j = i; j < (g_dev_run_time.num - 1); j++) /* 将关机设备的数据删除，数据覆盖 */
            {
                g_dev_run_time.is_update[j] = g_dev_run_time.is_update[j + 1];
                g_dev_run_time.add_id[j]    = g_dev_run_time.add_id[j + 1];
                g_dev_run_time.sa[j]        = g_dev_run_time.sa[j + 1];
                memcpy(g_dev_run_time.power_on_time[j], g_dev_run_time.power_on_time[j + 1], 28 * sizeof(char));
                memcpy(g_dev_run_time.calibration_before_time[j], g_dev_run_time.calibration_before_time[j + 1], 28 * sizeof(char));
                memcpy(g_dev_run_time.calibration_after_time[j], g_dev_run_time.calibration_after_time[j + 1], 28 * sizeof(char));
                memcpy(g_dev_run_time.power_off_time[j], g_dev_run_time.power_off_time[j + 1], 28 * sizeof(char));                
            }
            
            /* 最后一个单元清空 */
            g_dev_run_time.is_update[g_dev_run_time.num - 1] = 0;
            g_dev_run_time.add_id[g_dev_run_time.num - 1] = 0;
            g_dev_run_time.sa[g_dev_run_time.num - 1] = 0;
            memset(g_dev_run_time.power_on_time[g_dev_run_time.num - 1]            ,0, 28 * sizeof(char));
            memset(g_dev_run_time.calibration_before_time[g_dev_run_time.num - 1]  ,0, 28 * sizeof(char));
            memset(g_dev_run_time.calibration_after_time[g_dev_run_time.num - 1]   ,0, 28 * sizeof(char));
            memset(g_dev_run_time.power_off_time[g_dev_run_time.num - 1]           ,0, 28 * sizeof(char));
            
            /* 全局变量递减 */
            i--;
            g_dev_run_time.num--;
        }
    }
    
    return;
}

/* 用于监控设备的开关机时间 */
void cdr_heart_beat_proc(cdr_can_frame_t *data)
{
    int i;
    int new_dev = 1;
    int index = g_dev_run_time.num;
    int sa = CDR_ID_TO_SA(data->id);
    
    /* 等待记录仪开始记录时间 */
    if (g_dev_run_time.num < 1)
    {
        return;
    }    
    
    /* 循环查找，是否有过时间记录，0是测试仪自己，因此从1开始查看 */
    for (i = 1; i < g_dev_run_time.num; i++)
    {
        if (sa == g_dev_run_time.sa[i])
        {
            new_dev = 0; /* 正在运行的设备 */
            index = i;
            break;
        }
    }

    /* 新设备接入 */
    if (new_dev)
    {
        cdr_get_system_time(CDR_TIME_S, g_dev_run_time.power_on_time[index]);
        cdr_get_system_time(CDR_TIME_S, g_dev_run_time.power_off_time[index]);
        g_dev_run_time.sa[index] = sa;
        g_dev_run_time.add_id[index] = 0;
        g_dev_run_time.num++;
        g_dev_run_time.is_update[index] = 1;
        return;
    }
    
    /* 正在运行的设备，更新下电时间 */
    cdr_get_system_time(CDR_TIME_S, g_dev_run_time.power_off_time[index]);
    g_dev_run_time.is_update[index] = 1; /* 设置标记，代表数据需要记录 */
    return;
}

void cdr_can_data_proc(cdr_can_frame_t *data)
{
    /* 心跳报文，用于反应车载设备的运行时间 */
    if (CDR_ID_TO_PF(data->id) == CDR_CAN_DATA_HEART_BEAT_PF)
    {
        cdr_heart_beat_proc(data);
        return;
    }
    
    /* 时间校准处理 */
    if ((CDR_ID_TO_PF(data->id) == CDR_CAN_DATA_TIME_CALIBRATION_PF) && (g_time_calibration_invalid == 0))
    {
        cdr_time_calibration_proc(data);
        g_time_calibration_invalid = 1;
    }

    cdr_write_can_data_to_file(data);    
    return;
}

/* ---------------------------------------- 文件对外函数 ---------------------------------------- */
void cdr_record_can_data()
{
    cdr_can_frame_t data = {0};

    cdr_diag_log(CDR_LOG_INFO, "cdr_record_can_data >>>>>>>>>>>>>>>>>>>>>>>>>>in");

    while (1) 
    {        
        g_pthread_record_data_active = 1;
        
        memset(&data, 0, sizeof(data));
        
        /* 未接收到can数据 */        
        if (cdr_receive_can_data(&data) != CDR_OK)
        {
            cdr_no_can_data_proc();
            continue;
        }
        
        cdr_can_data_proc(&data);
        cdr_dev_running_time_proc();
    }
    
    return;
}

