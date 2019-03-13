#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/reboot.h>
#include <sys/statfs.h>
#include "my_global.h"
#include "my_sys.h"
#include "mysql.h"

#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/socket.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/watchdog.h>


/* ---------------------------------------- 宏定义 ---------------------------------------- */

#define CDR_DEBUG_INVALID    0         /* 是否调试无效，用于日志的控制打印，软件最后发布时要定义成1 */

#define CDR_FAIL_TRY_TIMES               3   /* 失败最大尝试次数 */
#define CDR_EVENT_RECOVER_THRESHOLD      3   /* 恢复门限：连续无故障次数 */ 
#define CDR_WATCH_DOG_THRESHOLD          60  /* 喂狗门限，超过门限不喂狗，系统复位，单位s */ 

#define CDR_FILE_DIR_MAX_SIZE_BYTE       1000000   /* 1M */
#define CDR_CACHE0_MAX_SIZE_BYTE         1000      /* 最大缓存文件大小，1k */

#define CDR_DEV_RUNTIME_MAX_NUM          100    /* 能够同时记录运行时间的最大采集设备数量 */
#define CDR_DIR_BF_MAX_NUM               20      /* 备份文件的最大数量 */

#define CDR_RECORDER_SA                         0x59   /* 记录仪SA标识 */
#define CDR_CAN_DATA_TIME_CALIBRATION_PF        0x6f   /* 时间校准PF标识 */
#define CDR_CAN_DATA_HEART_BEAT_PF              0x39   /* 心跳报文PF标识 */

#define CDR_OK      0  /* 正常返回值 */
#define CDR_ERROR   1  /* 错误返回值 */

#define CDR_STORAGE_WARNING_THRESHOLD    20    /* 硬盘空间剩余20%提示 */
#define CDR_STORAGE_ALARM_THRESHOLD      10    /* 硬盘空间剩余10%告警，部分数据覆盖存储 */
#define CDR_STORAGE_NULL_THRESHOLD       2     /* 硬盘空间剩余2%告警，所有的数据都要覆盖存储 */

#define CDR_DEV_RUNNING_TIME_THRESHOLD   6    /* 超过门限没有坚持到设备心跳，认为设备关机，单位秒 */

#define CDR_ID_TO_PRIORITY(id)     (((id) >> 26) & 0x7)    /* 根据CAN ID解析PRIORITY */
#define CDR_ID_TO_RSV(id)          (((id) >> 24) & 0x3)    /* 根据CAN ID解析RSV */
#define CDR_ID_TO_PF(id)           (((id) >> 16) & 0xff)   /* 根据CAN ID解析PF */
#define CDR_ID_TO_PS_DA(id)        (((id) >> 8) & 0xff)    /* 根据CAN ID解析PS/DA */
#define CDR_ID_TO_SA(id)           ((id) & 0xff)           /* 根据CAN ID解析SA */

#define CDR_FILE_DIR_LOG           "/opt/myapp/cdr_recorder/log/"           /* 用户日志打印目录，需要记录到mysql数据库 */
#define CDR_FILE_DIR_DIAGLOG       "/opt/myapp/cdr_recorder/diag/"          /* 诊断日志打印目录，用于问题的定位 */
#define CDR_FILE_DIR_DIAGLOG_BF    "/opt/myapp/cdr_recorder/diag/bf/"       /* 诊断日志的缓存目录，数量有最大门限CDR_DIR_BF_MAX_NUM */
#define CDR_FILE_DIR_CAN           "/opt/myapp/cdr_recorder/file/"          /* 文件记录的目录 */
#define CDR_FILE_DIR_CAN_BF        "/opt/myapp/cdr_recorder/file/bf/"       /* 文件记录的备份目录，数量有最大门限CDR_DIR_BF_MAX_NUM */
#define CDR_FILE_DIR_NET           "/opt/myapp/cdr_recorder/netfile/"          /* 文件记录的目录 */
#define CDR_FILE_DIR_NET_BF        "/opt/myapp/cdr_recorder/netfile/bf/"       /* 文件记录的备份目录，数量有最大门限CDR_DIR_BF_MAX_NUM */


#define CDR_DATA_TABLE_PF41           "PF_41"         /* 存储PF等于0x41的数据 */
#define CDR_DATA_TABLE_PF42           "PF_42"         /* 存储PF等于0x42的数据 */
#define CDR_DATA_TABLE_PF_ELSE        "PF_ELSE"       /* 存储PF非0x41或0x42的数据 */
#define CDR_DATA_TABLE_RUN_TIME       "RUN_TIME"      /* 存储设备的运行时间 */
#define CDR_DATA_TABLE_USER_LOG       "USER_LOG"      /* 存储用户日志的记录 */
#define CDR_DATA_TABLE_EVENT_TYPE     "EVENT_TYPE"    /* 存储用户日志中事件类型的说明 */
#define CDR_DATA_TABLE_SELF_TEST      "SELF_TEST"     /* 临时表格，用户启动自检，启动完后表格删除 */
#define CDR_DATA_TABLE_NET            "NET_DATA"      /* 存储网口数据 */

/* 不同的表格对应的表头内容 */
#define CDR_DATA_TABLE_HEAD_PF        \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, PF VARCHAR(2), Time VARCHAR(23), Priority VARCHAR(2), Rsv VARCHAR(2), PSorDA VARCHAR(2), SA VARCHAR(2), Data VARCHAR(16), Data_Len  VARCHAR(2))"

#define CDR_DATA_TABLE_HEAD_RUNTIME   \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, SA VARCHAR(2), PowerOn_Time DATETIME, Calibration_Before_Time DATETIME, Calibration_After_Time DATETIME, PowerOff_Time DATETIME, Running INT(11))"

#define CDR_DATA_TABLE_HEAD_USER_LOG  \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, Time VARCHAR(23), Event_Type VARCHAR(2), Result VARCHAR(5))"

#define CDR_DATA_TABLE_HEAD_EVENT_TYPE  \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, Event_Type INT(11), Event VARCHAR(300))"

#define CDR_DATA_TABLE_HEAD_SELF_TEST  \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, Data_Value INT(11))"

#define CDR_DATA_TABLE_HEAD_NET     \
"(Serial INT(11) PRIMARY KEY AUTO_INCREMENT, Time VARCHAR(23), Send_No VARCHAR(10), \
Receive_No VARCHAR(10), Data_Len VARCHAR(4), Instruction VARCHAR(2), Data  VARCHAR(100), Verify VARCHAR(2), Token VARCHAR(2))"

#ifndef AF_CAN
#define AF_CAN 29
#endif
#ifndef PF_CAN
#define PF_CAN AF_CAN
#endif

#define WDT "/dev/watchdog"    /* 看门狗程序位置 */

/* ---------------------------------------- 结构体或枚举定义 ---------------------------------------- */
typedef enum cdr_log {
    CDR_LOG_INFO = 1,       /* 关键事件诊断日志记录 */ 
    CDR_LOG_ERROR,          /* 错误事件诊断日志记录 */ 
    CDR_LOG_DEBUG,          /* 调试诊断日志记录，CDR_DEBUG_INVALID置1日志将不会打印 */  
} cdr_log_t;

typedef enum cdr_time {
    CDR_TIME_MS = 1,        /* 获取时间精度到ms */
    CDR_TIME_S,             /* 获取时间精度到s */
} cdr_time_t;

typedef enum cdr_led_state {
    CDR_LED_RED_CONTINUOUS      = 0x1,        /* 红灯常亮 */
    CDR_LED_RED_FLASH           = 0x2,        /* 红灯闪烁 */
    CDR_LED_YELLOW_CONTINUOUS   = 0x4,        /* 黄灯常亮 */
    CDR_LED_YELLOW_FLASH        = 0x8,        /* 黄灯闪烁 */
    CDR_LED_GREEN_FLASH         = 0x10,       /* 绿灯闪烁 */
    CDR_LED_GREEN_CONTINUOUS    = 0x20,       /* 绿灯常亮 */    
    CDR_LED_ORANGE_FLASH        = 0x40,       /* 橙灯闪烁 */
} cdr_led_state_t;

typedef enum cdr_user_log_type {  
    CDR_USR_LOG_TYPE_INIT_CAN_IF = 1,           /* 用户日志事件类型：1、CAN接口初始化 */
    CDR_USR_LOG_TYPE_INIT_MYSQL,                /* 用户日志事件类型：2、MySQL初始化 */
    CDR_USR_LOG_TYPE_INIT_RECORD_SELTTEST,      /* 用户日志事件类型：3、数据存储自检 */
    CDR_USR_LOG_TYPE_INIT_PTHREAD,              /* 用户日志事件类型：4、线程启动 */
    CDR_USR_LOG_TYPE_FILE_RECORD_FAULT,         /* 用户日志事件类型：5、文件存储记录 */
    CDR_USR_LOG_TYPE_MYSQL_RECORD_FAULT,        /* 用户日志事件类型：6、数据库记录数据 */
    CDR_USR_LOG_TYPE_STORAGE_WARNING,           /* 用户日志事件类型：7、存储空间不足提示 */
    CDR_USR_LOG_TYPE_STORAGE_ALARM,             /* 用户日志事件类型：8、存储空间不足告警 */
    CDR_USR_LOG_TYPE_STORAGE_NULL,              /* 用户日志事件类型：9、存储空间不足 */
    CDR_USR_LOG_TYPE_TIME_CALIBRATION,          /* 用户日志事件类型：10、时间校准 */
    CDR_USR_LOG_TYPE_TIME_ABNORMAL,             /* 用户日志事件类型：11、时间异常，对应CDR_EVENT_BATTERY_NO_POWER */
} cdr_user_log_type_t;

typedef enum cdr_event {  
    CDR_EVENT_FILE_RECORD_FAULT = CDR_USR_LOG_TYPE_FILE_RECORD_FAULT,    /* 5、文件记录事件 */
    CDR_EVENT_MYSQL_RECORD_FAULT,                                        /* 6、数据库记录事件 */
    CDR_EVENT_STORAGE_WARNING,                                           /* 7、存储空间不足提示事件 */
    CDR_EVENT_STORAGE_ALARM,                                             /* 8、存储空间不足告警事件 */
    CDR_EVENT_STORAGE_NULL,                                              /* 9、存储空间不足事件 */
    CDR_EVENT_DATA_RECORDING,                                            /* 10、数据正常存储事件 */
    CDR_EVENT_BATTERY_NO_POWER,                                          /* 11、电池没电了 */
    CDR_EVENT_MAX,
} cdr_event_t;

typedef struct cdr_can_frame {
    int id;             /* can帧id */
    long long data;       /* can数据 */
    int len;            /* can数据长度 */
} cdr_can_frame_t;

typedef struct cdr_dev_run_time {
    int num;                                                      /* 当前记录到设备运行总数 */
    int is_update[CDR_DEV_RUNTIME_MAX_NUM];                       /* 是否需要更新设备的运行时间*/
    int add_id[CDR_DEV_RUNTIME_MAX_NUM];                          /* 设备运行时间记录在数据库中的位置 */
    int sa[CDR_DEV_RUNTIME_MAX_NUM];                              /* 当前设备的sa标识 */
    char power_on_time[CDR_DEV_RUNTIME_MAX_NUM][28];              /* 当前设备的上电时间 */
    char calibration_before_time[CDR_DEV_RUNTIME_MAX_NUM][28];    /* 当前设备校准前的时间 */
    char calibration_after_time[CDR_DEV_RUNTIME_MAX_NUM][28];     /* 当前设备校准后的时间 */
    char power_off_time[CDR_DEV_RUNTIME_MAX_NUM][28];             /* 当前设备的下电时间 */
} cdr_dev_run_time_t;

typedef struct cdr_led_set_state {
    int state;                                             /* LED的状态 */
    char set_red[4];                                       /* 红灯是否输出 */
    char set_green[4];                                     /* 绿灯是否输出 */
    char set_yellow[4];                                    /* 黄灯是否输出 */
    int is_flash;                                          /* 灯是否闪烁 */
} cdr_led_set_state_t;

/* ---------------------------------------- 全局变量定义 ---------------------------------------- */
int g_socket_fd;                                  /* 创建的套接字 */
int g_cache1_file_busy;                           /* 缓存文件cache.1是否正在被操作 */
int g_dev_time_calibration_busy;                  /* 是否正在进行时间校准 */
int g_dev_run_time_record_busy;                   /* 设备运行时间是否正在记录 */
int g_diaglog_record_busy;                        /* 诊断日志记录是否正在被操作，防止多线程重入 */
int g_userlog_record_busy;                        /* 用户日志记录是否正在被操作，防止多线程重入 */
int g_write_file_no_proc_times;                   /* 连续未收到can数据的次数 */ 
int g_user_log_no_proc_times;                     /* 连续未记录用户日志的次数 */ 
int g_system_event_occur[CDR_EVENT_MAX];          /* 系统事件是否发生，事件见列表cdr_event_t */
int g_system_event_occur_his[CDR_EVENT_MAX];      /* 系统事件是否发生过，事件见列表cdr_event_t */
int g_system_event_no_occur_num[CDR_EVENT_MAX];   /* 系统事件连续没有发生的次数，事件见列表cdr_event_t */
int g_wdt_fd;                                     /* 看门狗 */
int g_pthread_record_data_active;                 /* 数据记录线程是否正常运行 */  
int g_pthread_data_to_mysql_active;               /* 数据存储线程是否正常运行 */          
int g_time_calibration_invalid;                   /* 时间校准是否生效，开机运行仅第一次时间校准生效 */
cdr_led_set_state_t g_led_state;                  /* LED状态灯当前状态 */
cdr_dev_run_time_t g_dev_run_time;                /* 设备的运行时间记录 */
MYSQL *g_mysql_conn;                              /* 代开的数据库id */
MYSQL *g_mysql_conn_net;                          /* 网口代开的数据库id */
MYSQL *g_mysql_conn_net_file;                     /* 网口文件代开的数据库id */
int g_netcache_file_busy;                         /* 网口缓存文件cache是否正在被操作 */

/* ---------------------------------------- 函数声明 ---------------------------------------- */
/* cdr_main */
void cdr_global_init();
int cdr_main_can_if_init();
int cdr_main_mysql_init();
int cdr_main_record_self_test();
int cdr_creat_pthread_record_can_data(pthread_t *pthread_record_data);
int cdr_creat_pthread_add_data_to_mysql(pthread_t *pthread_data_to_mysql);
int cdr_creat_pthread_fmea_test(pthread_t *pthread_fmea);
int cdr_main_init_watch_dog();

/* cdr_public */
void cdr_get_system_time(int type, char *time_info);
unsigned long get_file_size(const char *file);
int cdr_cpy_file(char *file_in, char *file_out);
void cdr_diag_log(int type, const char *format, ...);
void cdr_set_led_state(int type);
void cdr_system_reboot();
void cdr_led_control();

/* file_record */
void cdr_record_can_data();

/* mysql_record */
void cdr_add_data_to_mysql();
void cdr_add_netdata_to_mysql();
void cdr_add_netfile_to_mysql();
void cdr_mysql_end();

/* cdr_fmea */
void cdr_fmea_test();