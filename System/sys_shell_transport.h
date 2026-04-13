#ifndef __SYS_SHELL_TRANSPORT_H
#define __SYS_SHELL_TRANSPORT_H

#include <stdint.h>

/*
 * Shell 传输抽象层
 * 目标：让 core 不直接依赖具体串口/协议，便于后续从 NRT 切换到独立 Shell UART。
 */

/* 传输类型枚举 */
typedef enum {
    SHELL_TP_NRT_FRAME = 0,   /* NRT 协议帧承载（当前方案） */
    SHELL_TP_UART_STREAM = 1  /* 独立 UART 字符流（后续方案） */
} shell_transport_type_t;

/* Shell 连接的对端信息：消息来自哪个哪个传输类型、哪个会话 */
typedef struct {
    shell_transport_type_t type;
    uint8_t session_id;
} shell_peer_t;

/* Shell 传输接口函数指针表 */
typedef void (*shell_rx_cb_t)(const shell_peer_t *peer, const uint8_t *data, uint16_t len);

/* Shell 统一驱动接口 */
typedef struct {
    int (*init)(void);
    int (*start)(shell_rx_cb_t rx_cb);
    int (*send)(const shell_peer_t *peer, const uint8_t *data, uint16_t len);
    int (*stop)(void);
} shell_transport_vtable_t;

#endif /* __SYS_SHELL_TRANSPORT_H */
