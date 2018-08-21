#include "cdr_include.h"

static char *opt_host_name = NULL;
static char *opt_user_name = "root";
static char *opt_passowrd = "88224646baba";
static char *opt_socket_name = NULL;
static char *opt_db_name = "can_data";
static unsigned int opt_port_num = 0;
static unsigned int opt_flags = 0;


/* ---------------------------------------- 文件内部函数 ---------------------------------------- */
/* 创建表格 */
int mysql_create_table()
{
    char query_info[300] = {0};
    char *table_info[][2] = 
    {
        {CDR_DATA_TABLE_PF41,        CDR_DATA_TABLE_HEAD_PF},
        {CDR_DATA_TABLE_PF42,        CDR_DATA_TABLE_HEAD_PF}, 
        {CDR_DATA_TABLE_PF_ELSE,     CDR_DATA_TABLE_HEAD_PF},
        {CDR_DATA_TABLE_RUN_TIME,    CDR_DATA_TABLE_HEAD_RUNTIME},
        {CDR_DATA_TABLE_USER_LOG,    CDR_DATA_TABLE_HEAD_USER_LOG},
        {CDR_DATA_TABLE_EVENT_TYPE,  CDR_DATA_TABLE_HEAD_EVENT_TYPE},
    };
    int i;
    
    for (i = 0; i < (sizeof(table_info) / sizeof(table_info[0])); i++)
    {
        memset(query_info, 0, sizeof(query_info));        
        if (strcmp(table_info[i][0], CDR_DATA_TABLE_EVENT_TYPE) == 0) /* 特殊：支持中文字符 */
        {
            sprintf(query_info,"CREATE TABLE IF NOT EXISTS %s %s  default charset=utf8;", table_info[i][0], table_info[i][1]);
        }
        else
        {
            sprintf(query_info,"CREATE TABLE IF NOT EXISTS %s %s;", table_info[i][0], table_info[i][1]);
        }
        if (mysql_query(g_mysql_conn, query_info) != 0) /* 创建表格 */
        {
            cdr_diag_log(CDR_LOG_ERROR, "mysql_create_table fail, err:%s", mysql_error(g_mysql_conn));
            return CDR_ERROR;
        }
        cdr_diag_log(CDR_LOG_INFO, "mysql_create_table %s ok", table_info[i][0]);
    }
    
    cdr_diag_log(CDR_LOG_INFO, "mysql_create_table ..............................pass");
    return CDR_OK;
}

/* 清空表格 */
void mysql_clear_table(char *name)
{
    char info[30] = {0};
    
    sprintf(info, "truncate table %s", name);
    if (mysql_query(g_mysql_conn, info) != 0)
    {
        cdr_diag_log(CDR_LOG_ERROR, "mysql_clear_table %s fail, err:%s", name, mysql_error(g_mysql_conn));
        return;
    }
    
    cdr_diag_log(CDR_LOG_INFO, "mysql_clear_table %s ok", name);
    return; 
}

/* 获取表格中，最老数据的id */
int cdr_get_table_old_time_id(char *table, char *time)
{
    MYSQL_RES *result;
    MYSQL_ROW row;
    char statement[128] = {0};
    int id = 0;

    /* 查询数据 */
    sprintf(statement, "SELECT Serial from %s WHERE %s = (SELECT MIN(%s) from %s);", table, time, time, table);
    if(mysql_query(g_mysql_conn, statement ) != 0)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_table_old_time_id mysql_query fail, table:%s ,err:%s", table, mysql_error(g_mysql_conn));
        return id;
    }
    
    /* 保存查询到的数据到result */
    result = mysql_store_result(g_mysql_conn);
    if(result == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_table_old_time_id mysql_store_result fail, table:%s ,err:%s", table, mysql_error(g_mysql_conn));
        return id;
    }
    
    /* 获取值 */
    row = mysql_fetch_row(result);
    if(row == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_table_old_time_id mysql_fetch_row fail, table:%s ,err:%s", table, mysql_error(g_mysql_conn));
        return id;
    }
    id = atoi(row[0]);
    
    /* 释放资源 */
    mysql_free_result(result);
    return id;
}

/* 根据不同的表格式，解析数据，将insert格式改成update格式 */
void mysql_table_cover_old_data_proc(char *table_name, char *info, char *table_info)
{
    char buf1[100] = {0};  
    char buf2[100] = {0};  
    char buf3[100] = {0};  
    char buf4[100] = {0};
    char buf5[100] = {0};
    char buf6[100] = {0};
    char buf7[100] = {0};
    char buf8[100] = {0};
    
    if (strcmp(table_name, CDR_DATA_TABLE_RUN_TIME) == 0)
    {
        sscanf(info, "(SA, PowerOn_Time, PowerOff_Time) VALUES('%[^']', '%[^']', '%[^']')", buf1, buf2, buf3);
        sprintf(table_info, "UPDATE %s SET SA='%s', PowerOn_Time='%s', PowerOff_Time='%s', Running = TIMESTAMPDIFF(SECOND, PowerOn_Time, PowerOff_Time) WHERE Serial=%u;", 
            table_name, buf1, buf2, buf3, cdr_get_table_old_time_id(table_name, "PowerOff_Time"));            
    }
    else if (strcmp(table_name, CDR_DATA_TABLE_USER_LOG) == 0)
    {
        sscanf(info, "(Time, Event_Type, Result) VALUES('%[^']', '%[^']', '%[^']')", buf1, buf2, buf3);
        sprintf(table_info, "UPDATE %s SET Time='%s', Event_Type='%s', Result='%s' WHERE Serial=%u;", 
            table_name, buf1, buf2, buf3, cdr_get_table_old_time_id(table_name, "Time"));        
    }
    else
    {
        sscanf(info, "(PF,Time,Priority,Rsv,PSorDA,SA,Data,Data_Len) VALUES('%[^']', '%[^']', '%[^']', '%[^']', '%[^']', '%[^']', '%[^']', '%[^']')", 
            buf1, buf2, buf3,buf4, buf5, buf6,buf7, buf8);
        sprintf(table_info, "UPDATE %s SET PF='%s', Time='%s', Priority='%s', Rsv='%s', PSorDA='%s', SA='%s', Data='%s', Data_Len='%s' WHERE Serial=%u;", 
            table_name, buf1, buf2, buf3, buf4, buf5, buf6, buf7, buf8, cdr_get_table_old_time_id(table_name, "Time"));                
    }
    
    return;
}

/* 插入数据到表格 */
int mysql_insert_info_to_table(char *table_name, char *info)
{
    int cover_old_data = 0; /* 硬盘空间不足时 覆盖老数据 */
    char table_info[500] = {0};
    
    /* 两种情况下需要覆盖数据：
       1、硬盘无空间
       2、硬盘空间不足告警，并且表格等于PF_ELSE时 */
    if ((g_system_event_occur[CDR_EVENT_STORAGE_NULL] == 1) 
        || ((g_system_event_occur[CDR_EVENT_STORAGE_ALARM] == 1) && (strcmp(table_name, CDR_DATA_TABLE_PF_ELSE) == 0)))
    {
        /* CDR_DATA_TABLE_EVENT_TYPE初始化的时候创建，不存在覆盖不不覆盖的问题 */
        if (strcmp(table_name, CDR_DATA_TABLE_EVENT_TYPE) != 0) 
        {
            cover_old_data = 1; /* 覆盖 */
        }
    }
    
    /* 两种情况，1：覆盖老数据；2：直接插入新数据 */
    if (cover_old_data)
    {
        mysql_table_cover_old_data_proc(table_name, info, table_info);
    }
    else
    {
        sprintf(table_info, "INSERT INTO %s %s", table_name, info);
    }

    if (mysql_query(g_mysql_conn, table_info) != 0)  /* 表格插入数据 */
    {  
        cdr_diag_log(CDR_LOG_ERROR, "mysql_insert_info_to_table fail info:%s , err:%s", table_info, mysql_error(g_mysql_conn));
        return CDR_ERROR;
    }
    
    return CDR_OK;
}

int mysql_insert_pf_data_to_table(char *data)
{
    char table_name[20] = {0};
    char table_info[500] = {0};
    int pf = atoi(data + 1);
    
    if (pf == 41)
    {
        sprintf(table_name, "%s", CDR_DATA_TABLE_PF41);
    }
    else if (pf == 42)
    {
        sprintf(table_name, "%s", CDR_DATA_TABLE_PF42);
    }
    else
    {
        sprintf(table_name, "%s", CDR_DATA_TABLE_PF_ELSE);
    }
    
    sprintf(table_info, "(PF,Time,Priority,Rsv,PSorDA,SA,Data,Data_Len) VALUES(%s);", data);    
    g_system_event_occur[CDR_EVENT_DATA_RECORDING] = 1;
    return mysql_insert_info_to_table(table_name, table_info);
}

int mysql_init_table()
{
    int i;
    char *event_type_info[] = 
    {
        "CAN接口初始化; CAN interface initialization",
        "Mysql数据库初始化; MySQL database initialization",
        "数据记录存储启动自检; Data record self-test initialization",
        "线程初始化; Thread initialization",
        "数据记录,CAN数据到文本; Data record, can-data to file",
        "数据存储,文本到Mysql数据库; Data storage, file to mysql",
        "硬盘可用空间不足20%; Hard disk free size < 20%",
        "硬盘可用空间不足10%; Hard disk free size < 10%",
        "硬盘可用空间不足2%; Hard disk free size < 2%",
        "校准记录仪时间; recorder time calibration",
    };
    char info[200] = {0};
    
    mysql_clear_table("EVENT_TYPE");
    for (i = 0; i < (sizeof(event_type_info) / sizeof(event_type_info[0])); i++)
    {
        memset(info, 0, sizeof(info));
        mysql_set_character_set(g_mysql_conn, "utf8");
        sprintf(info, "(Event_Type, Event) VALUES(%u, '%s')", i + 1, event_type_info[i]);
        if (mysql_insert_info_to_table(CDR_DATA_TABLE_EVENT_TYPE, info) != CDR_OK)
        {  
            cdr_diag_log(CDR_LOG_INFO, "mysql_init_table event-type.......................fail");
            return CDR_ERROR;
        }
    }
    
    cdr_diag_log(CDR_LOG_INFO, "mysql_init_table ..............................pass");
    return CDR_OK;
}

/* 将cache1文件转为cache2文件 */
void cdr_cache1_to_cache2_proc()
{
    int i;
    int cache1_num;
    char name_old[200] = {0};
    char name_new[200] = {0};
    
    if (g_cache1_file_busy == 1)
    {
        return;
    }
    g_cache1_file_busy = 1;
    
    /* 获取当前目录下cache1的文件个数 */
    cache1_num = cdr_get_file_num("/opt/myapp/cdr_recorder/file/", ".1");
    
    /* 当前没有cache1文件 */
    if (cache1_num == 0)
    {
        g_cache1_file_busy = 0;
        return;
    }    
    
    if (cache1_num == 1) /* 只有一个cache1文件 */
    {
        rename("/opt/myapp/cdr_recorder/file/cache.1", "/opt/myapp/cdr_recorder/file/cache.2");
    }
    else
    {
        /* 将最老的cache1文件，转换为cache2，确保数据存储按照时间的顺序存储 */
        sprintf(name_old, "/opt/myapp/cdr_recorder/file/cache%d.1", cache1_num - 1);
        rename(name_old, "/opt/myapp/cdr_recorder/file/cache.2");
    }
    
    cdr_diag_log(CDR_LOG_DEBUG, "The file add new cache2 file, cache1_num %u", cache1_num);
    g_cache1_file_busy = 0;
    return;
}

void cdr_file_can_data_to_mysql()
{
    char str_line[1000] = {0};
    char file_name[200] = {0};
    char time_info[30] = {0};
    FILE *fp_cache;
    FILE *fp_tmp;
    int insert_fail = 0;

    /* 当前没有要存储的cache2文件，如果有cache1文件，将cache1转换为cache2 */
    if (access("/opt/myapp/cdr_recorder/file/cache.2", F_OK) != 0)
    {
        cdr_cache1_to_cache2_proc();
        g_system_event_occur[CDR_EVENT_DATA_RECORDING] = 0;
        return;
    }
    
    /* 打开cache2文件和临时数据缓存文件tmp */
    if (((fp_cache = fopen("/opt/myapp/cdr_recorder/file/cache.2", "r")) == NULL) || ((fp_tmp = fopen("/opt/myapp/cdr_recorder/file/tmp.dat","w")) == NULL))
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_file_can_data_to_mysql faile, the file cache.2 or can.dat can not be opened.\r\n");
        return;
    }

    while (!feof(fp_cache))
    {
        memset(str_line, 0, sizeof(str_line));
        fgets(str_line, 1000, fp_cache);
        
        /* 最后一行 */
        if (str_line[0] == '\0')
        {
            break;
        }
        
        /* 添加失败, 将后续的数据保存到tmp文件中 */
        if (insert_fail == 1)
        {
            fprintf(fp_tmp, "%s",str_line);
            continue;
        }
        
        if (mysql_insert_pf_data_to_table(str_line) != CDR_OK)
        {
            fprintf(fp_tmp, "%s",str_line);
            cdr_diag_log(CDR_LOG_DEBUG, "cdr_file_can_data_to_mysql fail, %s", str_line);
            insert_fail = 1; /* 表示插入失败 */
            continue;
        }

        cdr_diag_log(CDR_LOG_DEBUG, "cdr_file_can_data_to_mysql ok, %s", str_line);
    }
    fclose(fp_cache);
    fclose(fp_tmp);
    
    /* 添加失败, 将tmp文件转换为cache2文件，后续循环用新的cache2继续添加 */
    if (insert_fail == 1)
    {
        g_system_event_occur[CDR_EVENT_MYSQL_RECORD_FAULT] = 1;
        remove("/opt/myapp/cdr_recorder/file/cache.2");
        rename("/opt/myapp/cdr_recorder/file/tmp.dat", "/opt/myapp/cdr_recorder/file/cache.2");
        remove("/opt/myapp/cdr_recorder/file/tmp.dat");
        return;
    }
    
    /* 添加成功, 将cache2文件添加到备份文件中 */
    (void)cdr_cpy_file("/opt/myapp/cdr_recorder/file/cache.2", "/opt/myapp/cdr_recorder/file/bf/can.dat");
    remove("/opt/myapp/cdr_recorder/file/tmp.dat");
    remove("/opt/myapp/cdr_recorder/file/cache.2");
    
    /* 当文件大小大于CDR_FILE_DIR_MAX_SIZE_BYTE时，需要另起新文件记录 */
    if (get_file_size("/opt/myapp/cdr_recorder/file/bf/can.dat") >= CDR_FILE_DIR_MAX_SIZE_BYTE)
    {        
        cdr_get_system_time(CDR_TIME_S, time_info);
        sprintf(file_name, "/opt/myapp/cdr_recorder/file/bf/can%s.dat", time_info);
        rename("/opt/myapp/cdr_recorder/file/bf/can.dat", file_name);

        /* 预留的文件数量超过门限，删除最老的文件 */
        while (1)
        {
            if (cdr_get_file_num("/opt/myapp/cdr_recorder/file/bf/", ".dat") >= CDR_DIR_BF_MAX_NUM)
            {
                memset(file_name, 0 , sizeof(file_name));
                cdr_get_oldest_filename("/opt/myapp/cdr_recorder/file/bf/", file_name);
                remove(file_name);
                cdr_diag_log(CDR_LOG_INFO, "cdr_file_can_data_to_mysql delete oldest file %s", file_name);
                continue;
            }
            break;
        }
    }
    
    g_system_event_occur[CDR_EVENT_MYSQL_RECORD_FAULT] = 0;
    return;
}

void cdr_file_user_log_to_mysql()
{
    char str_line[1000] = {0};
    char name_new[200] = {0};
    char table_info[500] = {0};
    FILE *fp_log;
    FILE *fp_tmp;
    int insert_fail = 0;
    
    /* 当前没有要存储的log文件，直接返回 */
    if (access("/opt/myapp/cdr_recorder/log/log.log", F_OK) != 0)
    {
        return;
    }
    
    /* 文件被占用，直接返回 */
    if (g_userlog_record_busy)
    {
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_file_user_log_to_mysql g_userlog_record_busy");
        return;
    }
    
    /* 连续轮询3次无新的用户日志记录，将数据写到mysql */
    g_user_log_no_proc_times++;
    if (g_user_log_no_proc_times < 3) 
    {
        return;
    }
    g_user_log_no_proc_times = 0;
    g_userlog_record_busy = 1;
    
    /* 打开log.log文件和临时数据缓存文件tmp */
    if (((fp_log = fopen("/opt/myapp/cdr_recorder/log/log.log", "r")) == NULL) || ((fp_tmp = fopen("/opt/myapp/cdr_recorder/log/tmp.log","w")) == NULL))
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_file_user_log_to_mysql faile, the file log.log or tmp.log can not be opened.\r\n");
        g_userlog_record_busy = 0;
        return;
    }
    
    while (!feof(fp_log))
    {
        memset(str_line, 0, 1000);
        fgets(str_line, 1000, fp_log);
        
        /* 最后一行 */
        if (str_line[0] == '\0')
        {
            break;
        }
        
        /* 添加失败, 将后续的数据保存到tmp文件中 */
        if (insert_fail == 1)
        {
            fprintf(fp_tmp, "%s",str_line);
            continue;
        }
        sprintf(table_info, "(Time, Event_Type, Result) VALUES(%s);", str_line);
        
        if (mysql_insert_info_to_table(CDR_DATA_TABLE_USER_LOG, table_info) != CDR_OK)
        {
            fprintf(fp_tmp, "%s",str_line);
            cdr_diag_log(CDR_LOG_DEBUG, "cdr_file_user_log_to_mysql fail, %s", str_line);
            insert_fail = 1; /* 表示插入失败 */
            continue;
        }

        cdr_diag_log(CDR_LOG_DEBUG, "cdr_file_user_log_to_mysql ok, %s", str_line);
    }
    fclose(fp_log);
    fclose(fp_tmp);

    /* 添加失败, 将tmp文件转换为log文件，下一个周期继续添加 */
    if (insert_fail == 1)
    {
        remove("/opt/myapp/cdr_recorder/log/log.log");
        rename("/opt/myapp/cdr_recorder/log/tmp.log", "/opt/myapp/cdr_recorder/log/log.log");
        remove("/opt/myapp/cdr_recorder/log/tmp.log");
        g_userlog_record_busy = 0;
        return;
    }
    
    /* 添加成功, 删除文件 */
    remove("/opt/myapp/cdr_recorder/log/log.log");
    remove("/opt/myapp/cdr_recorder/log/tmp.log");
    g_userlog_record_busy = 0;    
    return;
}

void cdr_file_data_to_mysql()
{
    cdr_file_can_data_to_mysql();
    cdr_file_user_log_to_mysql();
    
    return;
}

int cdr_get_run_time_insert_id(int sa, char *power_on_time, char *power_off_time)
{
    MYSQL_RES *result;
    MYSQL_ROW row;
    char statement[128] = {0};
    int id = 0;

    /* 查询数据 */
    sprintf(statement, "SELECT Serial from %s WHERE SA='%x' AND PowerOn_Time='%s' AND PowerOff_Time='%s';", CDR_DATA_TABLE_RUN_TIME, sa, power_on_time, power_off_time);
    if(mysql_query(g_mysql_conn, statement ) != 0)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_run_time_insert_id mysql_query fail, table:%s ,err:%s", CDR_DATA_TABLE_RUN_TIME, mysql_error(g_mysql_conn));
        return id;
    }
    
    /* 保存查询到的数据到result */
    result = mysql_store_result(g_mysql_conn);
    if(result == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_run_time_insert_id mysql_store_result fail, table:%s ,err:%s", CDR_DATA_TABLE_RUN_TIME, mysql_error(g_mysql_conn));
        return id;
    }
    
    /* 获取值 */
    row = mysql_fetch_row(result);
    if(row == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "cdr_get_run_time_insert_id mysql_fetch_row fail, sa %x power on %s off %s table:%s ,err:%s", 
        sa, power_on_time, power_off_time, CDR_DATA_TABLE_RUN_TIME, mysql_error(g_mysql_conn));
        return id;
    }
    id = atoi(row[0]);
    
    /* 释放资源 */
    mysql_free_result(result);
    return id;
}

void cdr_global_data_to_mysql()
{
    int i;
    char table_info[500] = {0};
    cdr_dev_run_time_t dev_run_time;
    
    if (g_dev_time_calibration_busy)
    {
        return;
    }
    
    g_dev_run_time_record_busy = 1;

    memcpy(&dev_run_time, &g_dev_run_time, sizeof(dev_run_time));
    
    for (i = 0; i < dev_run_time.num; i++)
    {
        /* 不需要更新时间 */
        if (dev_run_time.is_update[i] == 0)
        {
            continue;
        }
        
        
        /* 第一次记录时间 */
        if (dev_run_time.add_id[i] == 0)
        {
            /* 区分校准时间前和校准时间后两种情况 */
            memset(table_info, 0 , sizeof(table_info));
            if ((dev_run_time.calibration_before_time[i][0] == 0) || (dev_run_time.calibration_after_time[i][0] == 0))
            {
                sprintf(table_info, "(SA, PowerOn_Time, PowerOff_Time) VALUES('%x', '%s', '%s');", 
                    dev_run_time.sa[i], dev_run_time.power_on_time[i], dev_run_time.power_off_time[i]);
            }
            else
            {
                sprintf(table_info, "(SA, PowerOn_Time, Calibration_Before_Time, Calibration_After_Time, PowerOff_Time) VALUES('%x', '%s', '%s', '%s', '%s');", 
                    dev_run_time.sa[i], dev_run_time.power_on_time[i], dev_run_time.calibration_before_time[i], dev_run_time.calibration_after_time[i], dev_run_time.power_off_time[i]);
            }
            
            if (mysql_insert_info_to_table(CDR_DATA_TABLE_RUN_TIME, table_info) != CDR_OK)
            {
                cdr_diag_log(CDR_LOG_ERROR, "cdr_global_data_to_mysql mysql_insert_info_to_table fail, info:%s", table_info);
                continue;
            }
            g_dev_run_time.add_id[i] = cdr_get_run_time_insert_id(dev_run_time.sa[i], dev_run_time.power_on_time[i], dev_run_time.power_off_time[i]);
            dev_run_time.add_id[i] = g_dev_run_time.add_id[i];
        }
        
        /* 更新运行时间sql语句区分校准时间前和校准时间后两种情况 */
        memset(table_info, 0 , sizeof(table_info));
        if ((dev_run_time.calibration_before_time[i][0] == 0) || (dev_run_time.calibration_after_time[i][0] == 0))
        {
            sprintf(table_info, "UPDATE %s SET PowerOff_Time='%s', Running = TIMESTAMPDIFF(SECOND, PowerOn_Time, PowerOff_Time) WHERE Serial=%u;", 
                CDR_DATA_TABLE_RUN_TIME, dev_run_time.power_off_time[i], dev_run_time.add_id[i]);
        }
        else
        {
            sprintf(table_info, "UPDATE %s SET PowerOff_Time='%s', Calibration_Before_Time='%s', Calibration_After_Time='%s', "
                "Running = (TIMESTAMPDIFF(SECOND, PowerOn_Time, PowerOff_Time) - TIMESTAMPDIFF(SECOND, Calibration_Before_Time, Calibration_After_Time)) WHERE Serial=%u;", 
                CDR_DATA_TABLE_RUN_TIME, dev_run_time.power_off_time[i], dev_run_time.calibration_before_time[i], dev_run_time.calibration_after_time[i], dev_run_time.add_id[i]);
        }
        if (mysql_query(g_mysql_conn, table_info) != 0)
        {
            cdr_diag_log(CDR_LOG_ERROR, "cdr_global_data_to_mysql update running time fail info:%s , err:%s", table_info, mysql_error(g_mysql_conn));
            continue;
        }
        
        g_dev_run_time.is_update[i] = 0;
        cdr_diag_log(CDR_LOG_DEBUG, "cdr_global_data_to_mysql ok, id:%u, sa:%x, on:%s, off:%s", 
            dev_run_time.add_id[i], dev_run_time.sa[i], dev_run_time.power_on_time[i], dev_run_time.power_off_time[i]);
    }
    
    g_dev_run_time_record_busy = 0;
    return;
}

/* ---------------------------------------- 文件对外函数 ---------------------------------------- */

/* 数据库初始化 */
int cdr_mysql_init()
{
    /* 初始化客户端开发库 */
    if (mysql_library_init(0, NULL, NULL))
    {
        cdr_diag_log(CDR_LOG_ERROR, "mysql_library_init fail");
        return CDR_ERROR;        
    }
    cdr_diag_log(CDR_LOG_INFO, "mysql_library_init ok");    
    
    /* 初始化连接处理器 */
    g_mysql_conn = mysql_init(NULL);
    if (g_mysql_conn == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "mysql_init fail");
        return CDR_ERROR;        
    }    
    cdr_diag_log(CDR_LOG_INFO, "mysql_init ok");
    
    /* 连接服务器 */
    if (mysql_real_connect(g_mysql_conn, opt_host_name, opt_user_name, opt_passowrd, opt_db_name, opt_port_num, opt_socket_name, opt_flags) == NULL)
    {
        cdr_diag_log(CDR_LOG_ERROR, "mysql_real_connet fail, mysql_error %s", mysql_error(g_mysql_conn));
        mysql_close(g_mysql_conn);
        return CDR_ERROR;
    }
    cdr_diag_log(CDR_LOG_INFO, "mysql_real_connet ok");
    
    /* 创建需要用到的表格 */
    if (mysql_create_table() != CDR_OK)
    {
        cdr_mysql_end();
        return CDR_ERROR;
    }
    
    /* 初始化表格 */
    if (mysql_init_table() != CDR_OK)
    {
        cdr_mysql_end();
        return CDR_ERROR;
    }    
    
    return CDR_OK;
}

void cdr_mysql_end()
{
    /* 断开服务器连接，终止客户端开发库 */
    mysql_close(g_mysql_conn);
    mysql_library_end();
    
    cdr_diag_log(CDR_LOG_INFO, "mysql_close ok");
    return;
}


void cdr_add_data_to_mysql()
{
    int i;
    
    cdr_diag_log(CDR_LOG_INFO, "cdr_add_data_to_mysql >>>>>>>>>>>>>>>>>>>>>>>>>>in");
    
    sleep(3); /* 延时3s */    
    
    while (1)
    {
        sleep(1); /* 延时1s */
        
        g_pthread_data_to_mysql_active = 1;
        
        cdr_file_data_to_mysql();
        cdr_global_data_to_mysql();
    }

    cdr_mysql_end();    
    return;    
}

