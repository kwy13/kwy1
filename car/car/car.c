#include "stdio.h"
#include "string.h"
#include "soc_osal.h"
#include "securec.h"
#include "osal_debug.h"
#include "cmsis_os2.h"
#include "hal_bsp_ap3216/hal_bsp_ap3216.h"
#include "hal_bsp_rgb/hal_bsp_aw2013.h"
#include "hal_bsp_oled/hal_bsp_ssd1306.h"
#include "hal_bsp_sht20/hal_bsp_sht20.h"
#include "hal_bsp_nfc/hal_bsp_nfc.h"
#include "app_init.h"
#include "pinctrl.h"
#include "i2c.h"
#include "gpio.h"
#include "uart.h"
#include <stdlib.h>
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "wifi/wifi_connect.h"
#include "bsp/bsp.h"

#define I2C_MASTER_ADDRESS 0x0
#define I2C_ADDR 0X3c    // 器件的I2C从机地址
#define I2C_IDX 1        // 模块的I2C总线号
#define I2C_SPEED 100000 // 100KHz
/* io */
#define I2C_SCL_MASTER_PIN 16
#define I2C_SDA_MASTER_PIN 15
#define CONFIG_PIN_MODE 2


//aw2013
#define RGB_ON 255
#define RGB_OFF 0
#define TASK_DELAY_TIME 100


//ssd1306
#define DELAY_TIME_MS 85
#define mqtt_delay 200
#define SEC_MAX 60
#define MIN_MAX 60
#define HOUR_MAX 24

#define PWM_RED_MAX 255


#define UART_RECV_SIZE 200


float temperature = 0;
float humidity = 0;
uint16_t ir = 0;
uint16_t als = 0;
uint16_t ps = 0; // 人体红外传感器 接近传感器 光照强度传感器

osThreadId_t car_task_ID; // 任务1

static uint8_t uart_rx_flag = 0;
uint8_t uart_recv[UART_RECV_SIZE] = {0};
uart_buffer_config_t g_app_uart_buffer_config = {.rx_buffer = uart_recv, .rx_buffer_size = UART_RECV_SIZE};

char car_state = 0;             //车的状态，为0则代代表没有启动,处于CAR ALARM   为1代表车辆启动，处于CAR OPEN
char gps_data[50]={0};          //提取到的GPS坐标





/********************************MQTT配置********************************/
osThreadId_t mqtt_init_task_id; // mqtt订阅数据任务
#define SERVER_IP_ADDR "ed5bc095fa.st1.iotda-device.cn-east-3.myhuaweicloud.com" // 接入地址
#define SERVER_IP_PORT 8883                                                       // 端口号
#define CLIENT_ID "682fd7defde7ae3745a4b575_mxb_test_0_0_2025052606"                                        // 设备id
#define MQTT_CMDTOPIC_SUB "$oc/devices/my_beep/sys/commands/set/#" // 平台下发命令
#define MQTT_DATATOPIC_PUB "$oc/devices/my_beep/sys/properties/report"                 // 属性上报topic
#define MQTT_CLIENT_RESPONSE "$oc/devices/my_beep/sys/commands/response/request_id=%s" // 命令响应topic
#define DATA_SEVER_NAME "switch"
#define DATA_ATTR_NAME "beep_state"
#define MQTT_DATA_SEND "{\"services\": [{\"service_id\": \"%s\",\"properties\": {\"%s\": %s }}]}" // 上报数据格式
#define DATA_ATTR_NAME_MOTOR "motor_state"
#define MQTT_DATA_SEND_STR "{\"services\": [{\"service_id\": \"%s\",\"properties\": {\"%s\": \"%s\" }}]}"
#define DATA_ATTR_NAME_GPS_DATA "GPS_DATA"

// const char *gps_data = "132_456_8N9_\r\n771asdad";
#define KEEP_ALIVE_INTERVAL 120
#define IOT
#ifdef IOT
char *g_username = "682fd7defde7ae3745a4b575_mxb_test";
char *g_password = "60dcb12183139137333da12d87f2f7c0bbcd41722d4cdd1568408d69af3914b2";
#endif
char g_send_buffer[512] = {0};   // 发布数据缓冲区
char g_response_id[100] = {0};  // 保存命令id缓冲区
char g_response_buf[] =
    "{\"result_code\": 0,\"response_name\": \"beep\",\"paras\": {\"result\": \"success\"}}"; // 响应json
uint8_t g_cmdFlag;
MQTTClient client;
volatile MQTTClient_deliveryToken deliveredToken;
extern int MQTTClient_init(void);


/* 回调函数，处理连接丢失 */
void connlost(void *context, char *cause)
{
    unused(context);
    printf("Connection lost: %s\n", cause);
}
int mqtt_subscribe(const char *topic)
{
    printf("subscribe start\r\n");
    MQTTClient_subscribe(client, topic, 1);
    return 0;
}

int mqtt_publish(const char *topic, char *msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret = 0;
    pubmsg.payload = msg;
    pubmsg.payloadlen = (int)strlen(msg);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    printf("[payload]:  %s, [topic]: %s\r\n", msg, topic);
    ret = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt publish failed\r\n");
        return ret;
    }

    return ret;
}

/* 回调函数，处理消息到达 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("Message with token value %d delivery confirmed\n", dt);

    deliveredToken = dt;
}
// 解析字符串并保存到数组中
void parse_after_equal(const char *input, char *output)
{
    const char *equalsign = strchr(input, '=');
    if (equalsign != NULL) {
        // 计算等于号后面的字符串长度
        strcpy(output, equalsign + 1);
    }
}
int messageArrived(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);
    printf("[Message recv topic]: %s\n", topic_name);
    printf("[Message]: %s\n", (char *)message->payload);

    const char *payload = (char *)message->payload;

    // 控制蜂鸣器逻辑
    if (strstr(payload, DATA_ATTR_NAME) != NULL) {
        if (strstr(payload, "true") != NULL)
            my_io_setval(SENSOR_IO, GPIO_LEVEL_LOW);
        else
            my_io_setval(SENSOR_IO, GPIO_LEVEL_HIGH);
    }

    // 控制电机逻辑
    if (strstr(payload, DATA_ATTR_NAME_MOTOR) != NULL) {
        if (strstr(payload, "true") != NULL)
            my_io_setval(MOTOR_IO, GPIO_LEVEL_LOW);
        else
            my_io_setval(MOTOR_IO, GPIO_LEVEL_HIGH);
    }

    // 解析命令 ID 用于响应
    parse_after_equal(topic_name, g_response_id);
    g_cmdFlag = 1;
    memset((char *)message->payload, 0, message->payloadlen);
    printf("payload contains beep_state? %s\n", strstr(payload, DATA_ATTR_NAME) ? "YES" : "NO");
    printf("payload contains motor_state? %s\n", strstr(payload, DATA_ATTR_NAME_MOTOR) ? "YES" : "NO");


    return 1;
}


static errcode_t mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    /* 初始化MQTT客户端 */
    MQTTClient_init();
    /* 创建 MQTT 客户端 */
    ret = MQTTClient_create(&client, SERVER_IP_ADDR, CLIENT_ID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("Failed to create MQTT client, return code %d\n", ret);
        return ERRCODE_FAIL;
    }
    conn_opts.keepAliveInterval = KEEP_ALIVE_INTERVAL;
    conn_opts.cleansession = 1;
#ifdef IOT
    conn_opts.username = g_username;
    conn_opts.password = g_password;
#endif
    // 绑定回调函数
    MQTTClient_setCallbacks(client, NULL, connlost, messageArrived, delivered);

    // 尝试连接
    if ((ret = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", ret);
        MQTTClient_destroy(&client); // 连接失败时销毁客户端
        return ERRCODE_FAIL;
    }
    printf("Connected to MQTT broker!\n");
    osDelay(mqtt_delay);
    // 订阅MQTT主题
    mqtt_subscribe(MQTT_CMDTOPIC_SUB);
    while (1) {
        // 响应平台命令部分
        osDelay(mqtt_delay); // 需要延时 否则会发布失败
        if (g_cmdFlag) {
            sprintf(g_send_buffer, MQTT_CLIENT_RESPONSE, g_response_id);
            // 设备响应命令
            mqtt_publish(g_send_buffer, g_response_buf);
            g_cmdFlag = 0;
            memset(g_response_id, 0, sizeof(g_response_id) / sizeof(g_response_id[0]));
        }
        // 属性上报部分
        //蜂鸣器
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, DATA_ATTR_NAME,
                my_io_readval(SENSOR_IO) ? "true" : "false");
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        
        //电机
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, DATA_ATTR_NAME_MOTOR,
                my_io_readval(MOTOR_IO) ? "false" : "true");
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);        
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));

        //GPS数据
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(g_send_buffer, MQTT_DATA_SEND_STR, DATA_SEVER_NAME, DATA_ATTR_NAME_GPS_DATA, gps_data);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));

        char value_buf[16] = {0};
        //温度
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", (int)temperature);  // 将 int 转为字符串
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, "temperature", value_buf);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        
        //湿度
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", (int)humidity);  // 将 int 转为字符串
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, "humid", value_buf);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));

        //光照强度
        osDelay(mqtt_delay);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", (int)als);  // 将 int 转为字符串
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, "light", value_buf);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
    }
    return ERRCODE_SUCC;
}

void mqtt_init_task(const char *argument)
{
    unused(argument);
    my_gpio_init(SENSOR_IO);
    my_gpio_init(MOTOR_IO);
    wifi_connect();
    osDelay(DELAY_TIME_MS);
    mqtt_connect();
}


/********************************MQTT配置********************************/






int find_point_num(int data)
{
    int i;
    for(i = data;i<UART_RECV_SIZE;i++)
        if(uart_recv[i] == '.')
            return i;
    return 0;
}

int find_douhao_num(int data)
{
    int i;
    for(i = data;i<UART_RECV_SIZE;i++)
        if(uart_recv[i] == ',')
            return i;
    return 0;
}


//uart2  7 RX   8 TX
void uart2_gpio_init(void)
{
    uapi_pin_set_mode(GPIO_07, PIN_MODE_2);         
    uapi_pin_set_mode(GPIO_08, PIN_MODE_2);
}

void uart2_init_config(void)
{
    uart_attr_t attr = {
        .baud_rate = 9600, .data_bits = UART_DATA_BIT_8, .stop_bits = UART_STOP_BIT_1, .parity = UART_PARITY_NONE};

    uart_pin_config_t pin_config = {.tx_pin = S_MGPIO8, .rx_pin = S_MGPIO7, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE};
    uapi_uart_deinit(UART_BUS_2);
    int ret = uapi_uart_init(UART_BUS_2, &pin_config, &attr, NULL, &g_app_uart_buffer_config);
    if (ret != 0) {
        printf("uart2 init failed ret = %02x\n", ret);
    }
}

void uart_read_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        return;
    }
    if (memcpy_s(uart_recv, length, buffer, length) != EOK) {
        return;
    }
    uart_rx_flag = 1;
}

void mp3_uart2_init(void)
{
    uart2_gpio_init();
    uart2_init_config();
    if (uapi_uart_register_rx_callback(2, UART_RX_CONDITION_MASK_IDLE, 1, uart_read_handler) == ERRCODE_SUCC) {
    }
}

// void gps_uart1_init(void)
// {
//     uart1_gpio_init();
//     uart1_init_config();
//     if (uapi_uart_register_rx_callback(0, UART_RX_CONDITION_MASK_IDLE, 1, uart_read_handler) == ERRCODE_SUCC) {
//     }
// }

void mp3_0001_play(void)        //车辆解锁播放
{
    const uint8_t  mp3_0001[] = {0x7E,0x04,0x41,0x00,0x01,0xEF};
    uapi_uart_write(UART_BUS_2, mp3_0001, sizeof(mp3_0001), 0);
}

void mp3_0002_play(void)        //车辆报警播放   未解锁状态下有人靠近
{
    const uint8_t  mp3_0002[] = {0x7E,0x04,0x41,0x00,0x02,0xEF};
    uapi_uart_write(UART_BUS_2, mp3_0002, sizeof(mp3_0002), 0);
}

void mp3_0003_play(void)        //车辆上锁播放   
{
    const uint8_t  mp3_0003[] = {0x7E,0x04,0x41,0x00,0x03,0xEF};
    uapi_uart_write(UART_BUS_2, mp3_0003, sizeof(mp3_0003), 0);
}


//GPIO10 BEEP   GPIO13 LED
void led_beep_init(void)
{
    uapi_pin_set_mode(GPIO_10, HAL_PIO_FUNC_GPIO);                      //
    uapi_pin_set_mode(GPIO_13, HAL_PIO_FUNC_GPIO);
    // 配置GPIO为输出模式 低电平
    uapi_gpio_set_dir(GPIO_10, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GPIO_10, GPIO_LEVEL_LOW);
    // 配置GPIO为输出模式 高电平
    uapi_gpio_set_dir(GPIO_13, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(GPIO_13, GPIO_LEVEL_HIGH);
    // while (1) {
    //     osDelay(DELAY_TIME_MS);    // 延时5s
    //     // uapi_gpio_toggle(GPIO_10); // 电平反转
    //     uapi_gpio_toggle(GPIO_13);
    //     printf("gpio toggle.\n");
    // }
}

uint32_t i2c_init(void)
{
    uint32_t result;
    uint32_t baudrate = I2C_SPEED;
    uint32_t hscode = I2C_MASTER_ADDRESS;

    uapi_pin_set_mode(I2C_SCL_MASTER_PIN, CONFIG_PIN_MODE);
    uapi_pin_set_mode(I2C_SDA_MASTER_PIN, CONFIG_PIN_MODE);
    uapi_pin_set_pull(I2C_SCL_MASTER_PIN, PIN_PULL_TYPE_UP);
    uapi_pin_set_pull(I2C_SDA_MASTER_PIN, PIN_PULL_TYPE_UP);

    result = uapi_i2c_master_init(AP3216C_I2C_IDX, baudrate, hscode);
    if (result != ERRCODE_SUCC) {
        printf("I2C Init status is 0x%x!!!\r\n", result);
        return result;
    }
    else 
        printf("i2c init success\r\n");
    osDelay(50);        //延时500ms

    return 0;
}


extern void base_ap3216_demo(void);
extern void base_rgb_demo(void);
extern void base_ssd1306_demo(void);
extern void blink_entry(void);

static void car(void)
{   
    char display_buffer[20] = {0};
    uint8_t hour = 10;
    uint8_t min = 30;
    uint8_t sec = 0;



    

    uint8_t pwm_red = 0;

    uint8_t ndefLen = 0;     // ndef包的长度
    uint8_t ndef_Header = 0; // ndef消息开始标志位-用不到
    size_t i = 0;

    
    /**************************INIT********************************/
    mp3_uart2_init();
    led_beep_init();
    i2c_init();
    AW2013_Init(); // 三色LED灯的初始化
    AP3216C_Init();
    SHT20_Init();
    nfc_Init();
    ssd1306_init(); // OLED 显示屏初始化
    ssd1306_cls();  // 清屏


    ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_0, "  CAR LOCKED  ", TEXT_SIZE_16);
    ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_3, "   2025-05-14  ", TEXT_SIZE_16);

    AW2013_Control_Red(RGB_OFF);
    AW2013_Control_Green(RGB_OFF);
    AW2013_Control_Blue(RGB_OFF);


    /**************************INIT********************************/



    while (1) {
        /**************************OLED********************************/
        if(car_state == 0 && ps > 500)
            ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_0, "  CAR ALARM  ", TEXT_SIZE_16);
        else if(car_state == 0)
            ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_0, "  CAR LOCKED  ", TEXT_SIZE_16);
        else
            ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_0, "  CAR OPEN  ", TEXT_SIZE_16);

        memset_s(display_buffer, sizeof(display_buffer), 0, sizeof(display_buffer)); 
        if (sprintf_s(display_buffer, sizeof(display_buffer), "temp%02dhum%02dal%03d", (int)temperature, (int)humidity, (int)als) > 0) {
            ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_1, display_buffer, TEXT_SIZE_16);
        }

        sec++;
        pwm_red+=10;

        if (pwm_red > (PWM_RED_MAX - 1)) {
            pwm_red = 0;
        }

        if (sec > (SEC_MAX - 1)) {
            sec = 0;
            min++;
        }
        if (min > (MIN_MAX - 1)) {
            min = 0;
            hour++;
        }
        if (hour > (HOUR_MAX - 1)) {
            hour = 0;
        }
        memset_s(display_buffer, sizeof(display_buffer), 0, sizeof(display_buffer));
        if (sprintf_s(display_buffer, sizeof(display_buffer), "    %02d:%02d:%02d   ", hour, min, sec) > 0) {
            ssd1306_show_str(OLED_TEXT16_COLUMN_0, OLED_TEXT16_LINE_2, display_buffer, TEXT_SIZE_16);
        }
        /**************************OLED********************************/


        /**************************LED********************************/
        AW2013_Control_Green(pwm_red);
        /**************************LED********************************/


        /**************************AP3216C********************************/
        AP3216C_ReadData(&ir, &als, &ps);
        printf("ir = %d    als = %d    ps = %d\r\n", ir, als, ps);
        /**************************AP3216C********************************/

        /**************************SHT20********************************/
        SHT20_ReadData(&temperature, &humidity);
        printf("temperature = %d  humidity = %d\r\n", (int)temperature, (int)humidity);
        /**************************SHT20********************************/

        /**************************NFC********************************/
        if (NT3HReadHeaderNfc(&ndefLen, &ndef_Header) != true) {
                printf("NT3HReadHeaderNfc is failed.\r\n");
                return;
                }
                // 将 ndefLen 加上 NDEF_HEADER_SIZE，因为之前获取的长度不包含头部字节
                ndefLen += NDEF_HEADER_SIZE;
                // 检查 ndefLen 是否小于等于 NDEF_HEADER_SIZE 如果是，说明数据长度异常，可能没有有效数据
                if (ndefLen <= NDEF_HEADER_SIZE) {
                    printf("ndefLen <= 2\r\n");
                    return;
                }
                // 使用 malloc 函数动态分配一块内存，用于存储读取的 NDEF 数据
                uint8_t *ndefBuff = (uint8_t *)malloc(ndefLen + 1);
                if (ndefBuff == NULL) {
                    printf("ndefBuff malloc is Falied!\r\n");
                    return;
                }
                // 将 NDEF 数据读取到 ndefBuff 中
                if (get_NDEFDataPackage(ndefBuff, ndefLen) != ERRCODE_SUCC) {
                    printf("get_NDEFDataPackage is failed. \r\n");
                    return;
                }

                // printf("start print ndefBuff.\r\n");
                // 使用 for 循环遍历 ndefBuff 数组，打印每个字节的数据
                // for (i = 0; i < ndefLen; i++) {
                //     printf("0x%x ", ndefBuff[i]);
                // }
                printf("start print nfc data: ");
                for (i = 9; i < ndefLen; i++) {
                    printf("0x%x ", ndefBuff[i]);
                }
                printf("\r\n");

                //NFC密码是123789
                if(car_state == 0 && ndefBuff[9] == 0x31 && ndefBuff[10] == 0x32 && ndefBuff[11] == 0x33
                    && ndefBuff[12] == 0x37 && ndefBuff[13] == 0x38 && ndefBuff[14] == 0x39)
                    {
                        mp3_0001_play();
                        car_state = 1;
                    }
                else if(car_state == 1 && ndefBuff[9] == 0x37 && ndefBuff[10] == 0x38 && ndefBuff[11] == 0x39
                    && ndefBuff[12] == 0x31 && ndefBuff[13] == 0x32 && ndefBuff[14] == 0x33)
                    {
                        car_state = 0;
                        mp3_0003_play();
                    }
                    
        /**************************NFC********************************/
        
        /**************************LED BEEP********************************/
        // GPIO10 BEEP  
        if(car_state == 0 && ps > 500)
        {
            mp3_0002_play();                                        //Mp3报警行人请勿靠近
            uapi_gpio_set_val(GPIO_10,GPIO_LEVEL_HIGH);             //蜂鸣器报警
        }
        else
            uapi_gpio_set_val(GPIO_10,GPIO_LEVEL_LOW);
        // uapi_gpio_set_val(GPIO_10,GPIO_LEVEL_HIGH);
        /**************************LED BEEP********************************/
        
        /**************************MP3 UART********************************/
        // uapi_uart_write(UART_BUS_2, (const uint8_t *)"test\r\n", sizeof("test\r\n"), 0);       
        // mp3_0002_play(); 
        /**************************MP3 UART********************************/


        /**************************GPS UART*******************************/
        // printf("gps test\r\n");
        if(uart_rx_flag == 1)           //接收到
        {
            uart_rx_flag = 0;
            int i = 0;
            for(i=0;i<UART_RECV_SIZE;i++)
            {
                if(uart_recv[i] == '$' &&
                    uart_recv[i+1] == 'G' &&
                    uart_recv[i+2] == 'N' &&
                    uart_recv[i+3] == 'G' &&
                    uart_recv[i+4] == 'G' &&
                    uart_recv[i+5] == 'A')

                {   
                    // 定位逗号位置
                int comma1 = find_douhao_num(i);              // 时间前的逗号
                int comma2 = find_douhao_num(comma1 + 1);     // 纬度开始前的逗号
                int comma3 = find_douhao_num(comma2 + 1);     // 纬度方向前的逗号
                int comma4 = find_douhao_num(comma3 + 1);     // 经度开始前的逗号
                int comma5 = find_douhao_num(comma4 + 1);     // 经度方向前的逗号

                // ---------- 纬度提取与转换 ----------
                int j = 0;
                int deg_lat = (uart_recv[comma2 + 1] - '0') * 10 + (uart_recv[comma2 + 2] - '0');
                int min1_lat = (uart_recv[comma2 + 3] - '0') * 10 + (uart_recv[comma2 + 4] - '0');
                int frac_lat = (uart_recv[comma2 + 6] - '0') * 1000 +
                            (uart_recv[comma2 + 7] - '0') * 100 +
                            (uart_recv[comma2 + 8] - '0') * 10 +
                            (uart_recv[comma2 + 9] - '0') * 1;
                int sec_lat = (frac_lat * 60 + 5000) / 10000;
                if (sec_lat >= 60)
                {
                    sec_lat -= 60;
                    min1_lat++;
                    if (min1_lat >= 60)
                    {
                        min1_lat -= 60;
                        deg_lat++;
                    }
                }

                gps_data[j++] = (deg_lat / 10) + '0';
                gps_data[j++] = (deg_lat % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = (min1_lat / 10) + '0';
                gps_data[j++] = (min1_lat % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = (sec_lat / 10) + '0';
                gps_data[j++] = (sec_lat % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = uart_recv[comma3 + 1];  // 'N' or 'S'
                gps_data[j++] = '\r';
                gps_data[j++] = '\n';

                // ---------- 经度提取与转换 ----------
                int deg_lon = (uart_recv[comma4 + 1] - '0') * 100 +
                            (uart_recv[comma4 + 2] - '0') * 10 +
                            (uart_recv[comma4 + 3] - '0') * 1;
                int min1_lon = (uart_recv[comma4 + 4] - '0') * 10 + (uart_recv[comma4 + 5] - '0') * 1;
                int frac_lon = (uart_recv[comma4 + 7] - '0') * 1000 +
                            (uart_recv[comma4 + 8] - '0') * 100 +
                            (uart_recv[comma4 + 9] - '0') * 10 +
                            (uart_recv[comma4 +10] - '0') * 1;
                int sec_lon = (frac_lon * 60 + 5000) / 10000;
                if (sec_lon >= 60)
                {
                    sec_lon -= 60;
                    min1_lon++;
                    if (min1_lon >= 60)
                    {
                        min1_lon -= 60;
                        deg_lon++;
                    }
                }

                gps_data[j++] = (deg_lon / 100) + '0';
                gps_data[j++] = ((deg_lon / 10) % 10) + '0';
                gps_data[j++] = (deg_lon % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = (min1_lon / 10) + '0';
                gps_data[j++] = (min1_lon % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = (sec_lon / 10) + '0';
                gps_data[j++] = (sec_lon % 10) + '0';
                gps_data[j++] = '_';

                gps_data[j++] = uart_recv[comma5 + 1];  // 'E' or 'W'
                gps_data[j++] = '\r';
                gps_data[j++] = '\n';
                gps_data[j] = '\0';

                 for (int k = 0; k < j; k++) {
                if (gps_data[k] == ' ' || gps_data[k] == '\0')
                    gps_data[k] = '_';
            }

                    // gps_data[28] = '\r';
                    // gps_data[29] = '\n';
                    printf("gps_data \r\n %s",gps_data);
                    // memset(gps_data, 0, 50);
                    // memset(gps_data, 0, 50);
                    break;
                }
            }
            // printf("gps\r\n");
            // printf("uart int rx = [%s]\n", uart_recv);
            memset(uart_recv, 0, UART_RECV_SIZE);
        }
        
        /**************************GPS UART*******************************/
        osDelay(100);
    }
}



 void car_task(void)
{
    printf("Enter base_ap3216_demo()!\r\n");

    osThreadAttr_t attr;
    attr.name = "ap3216_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 0x2000;
    attr.priority = osPriorityNormal3;

    car_task_ID = osThreadNew((osThreadFunc_t)car, NULL, &attr);
    if (car_task_ID != NULL) {
        printf("ID = %d, Create car_task_ID is OK!\r\n", car_task_ID);
    }


    printf("Enter HUAWEI IOT example()!");
    osThreadAttr_t options;
    options.name = "mqtt_init_task";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 0x2000;
    options.priority = osPriorityNormal6;

    mqtt_init_task_id = osThreadNew((osThreadFunc_t)mqtt_init_task, NULL, &options);
    if (mqtt_init_task_id != NULL) {
        printf("ID = %d, Create mqtt_init_task_id is OK!", mqtt_init_task_id);
    }
}



app_run(car_task);



