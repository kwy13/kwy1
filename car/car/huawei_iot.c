/*
 * Copyright (c) 2024 Beijing HuaQingYuanJian Education Technology Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "wifi/wifi_connect.h"
#include "bsp/bsp.h"
osThreadId_t mqtt_init_task_id; // mqtt订阅数据任务

#define SERVER_IP_ADDR "3e602f0ba8.st1.iotda-device.cn-north-4.myhuaweicloud.com" // 接入地址
#define SERVER_IP_PORT 8883                                                       // 端口号

#define CLIENT_ID "BEEP_my_num_aaa_0_0_2025052517"                                        // 设备id

#define MQTT_CMDTOPIC_SUB "$oc/devices/my_beep/sys/commands/set/#" // 平台下发命令

#define MQTT_DATATOPIC_PUB "$oc/devices/my_beep/sys/properties/report"                 // 属性上报topic
#define MQTT_CLIENT_RESPONSE "$oc/devices/my_beep/sys/commands/response/request_id=%s" // 命令响应topic

#define DATA_SEVER_NAME "Switch"
#define DATA_ATTR_NAME "beep_state"
#define MQTT_DATA_SEND "{\"services\": [{\"service_id\": \"%s\",\"properties\": {\"%s\": %s }}]}" // 上报数据格式


#define DATA_ATTR_NAME_MOTOR "motor_state"

#define MQTT_DATA_SEND_STR "{\"services\": [{\"service_id\": \"%s\",\"properties\": {\"%s\": \"%s\" }}]}"
#define DATA_ATTR_NAME_GPS_DATA "GPS_DATA"

const char *gps_data = "132_456_8N9_771asdad";





// #define GPS_CLINT_ID


#define KEEP_ALIVE_INTERVAL 120
#define DELAY_TIME_MS 200
#define IOT

#ifdef IOT
char *g_username = "BEEP_my_num_aaa";
char *g_password = "4c2a99973ef5979bdb522ecba1cbf88d180a008d27f0721e7757a60903426fe5";
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
// /* 回调函数，处理接收到的消息 */
// int messageArrived(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
// {
//     unused(context);
//     unused(topic_len);
//     printf("[Message recv topic]: %s\n", topic_name);
//     printf("[Message]: %s\n", (char *)message->payload);
//     // 进行传感器控制
//     if (strstr((char *)message->payload, "true") != NULL)
//         my_io_setval(SENSOR_IO, GPIO_LEVEL_HIGH);
//     else
//         my_io_setval(SENSOR_IO, GPIO_LEVEL_LOW);
//     // 解析命令id
//     parse_after_equal(topic_name, g_response_id);
//     g_cmdFlag = 1;
//     memset((char *)message->payload, 0, message->payloadlen);

//     return 1; // 表示消息已被处理
// }

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
            my_io_setval(SENSOR_IO, GPIO_LEVEL_HIGH);
        else
            my_io_setval(SENSOR_IO, GPIO_LEVEL_LOW);
    }

    // 控制电机逻辑
    if (strstr(payload, DATA_ATTR_NAME_MOTOR) != NULL) {
        if (strstr(payload, "true") != NULL)
            my_io_setval(MOTOR_IO, GPIO_LEVEL_HIGH);
        else
            my_io_setval(MOTOR_IO, GPIO_LEVEL_LOW);
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
    osDelay(DELAY_TIME_MS);
    // 订阅MQTT主题
    mqtt_subscribe(MQTT_CMDTOPIC_SUB);
    while (1) {
        // 响应平台命令部分
        osDelay(DELAY_TIME_MS); // 需要延时 否则会发布失败
        if (g_cmdFlag) {
            sprintf(g_send_buffer, MQTT_CLIENT_RESPONSE, g_response_id);
            // 设备响应命令
            mqtt_publish(g_send_buffer, g_response_buf);
            g_cmdFlag = 0;
            memset(g_response_id, 0, sizeof(g_response_id) / sizeof(g_response_id[0]));
        }
        // 属性上报部分
        //蜂鸣器
        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, DATA_ATTR_NAME,
                my_io_readval(SENSOR_IO) ? "true" : "false");
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        
        //电机
        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, DATA_ATTR_NAME_MOTOR,
                my_io_readval(MOTOR_IO) ? "true" : "false");
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);        
        memset(g_send_buffer, 0, sizeof(g_send_buffer) / sizeof(g_send_buffer[0]));

        //GPS数据
        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(g_send_buffer, MQTT_DATA_SEND_STR, DATA_SEVER_NAME, DATA_ATTR_NAME_GPS_DATA, gps_data);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));

        //温度
        int temperature = 25;
        int humid = 300;
        int light = 400;
        char value_buf[16] = {0};

        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", temperature);  // 将 int 转为字符串
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, "temperature", value_buf);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        
        //湿度
        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", humid);  // 将 int 转为字符串
        sprintf(g_send_buffer, MQTT_DATA_SEND, DATA_SEVER_NAME, "humid", value_buf);
        mqtt_publish(MQTT_DATATOPIC_PUB, g_send_buffer);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));

        //光照强度
        osDelay(DELAY_TIME_MS);
        memset(g_send_buffer, 0, sizeof(g_send_buffer));
        sprintf(value_buf, "%d", light);  // 将 int 转为字符串
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
/*
#define MOTOR_CLINT_ID      "aaa_my_motor_0_0_2025052517"
char *motor_username = "aaa_my_motor";
char *motor_password = "e9c260d5b074c6e950df42d898ace5320150a4cb94b329b156201c62f87a1afb";
*/
static void network_wifi_mqtt_example(void)
{
    printf("Enter HUAWEI IOT example()!");

    osThreadAttr_t options;
    options.name = "mqtt_init_task";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 0x2000;
    options.priority = osPriorityNormal;

    mqtt_init_task_id = osThreadNew((osThreadFunc_t)mqtt_init_task, NULL, &options);
    if (mqtt_init_task_id != NULL) {
        printf("ID = %d, Create mqtt_init_task_id is OK!", mqtt_init_task_id);
    }
}
/* Run the sample. */
app_run(network_wifi_mqtt_example);