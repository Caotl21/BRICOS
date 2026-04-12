#include "task_shell_transport_nrt.h"

#include "driver_hydrocore.h"
#include "sys_port.h"

/*
 * NRT 发送分片上限：
 * Driver_Protocol_SendFrame 的 payload_len 为 uint8_t，最大 255。
 * 这里保守设置为 240，给后续扩展字段预留空间。
 */
#define SHELL_NRT_TX_CHUNK_MAX  (240u)
#define SHELL_BOOT_STATUS_TEXT  "startup_success"

/* Shell Core 在 start() 时注入的接收回调 */
static shell_rx_cb_t s_shell_rx_cb = 0;

/*
 * NRT 命令回调：把收到的 payload 转交给 Shell Core。
 * 当前按 NRT_FRAME 模式处理（一帧一命令）。
 */
static void prv_on_shell_req(const uint8_t *payload, uint16_t len)
{
    shell_peer_t peer;
    shell_rx_cb_t cb;

    if ((payload == 0) || (len == 0u)) {
        return;
    }

    SYS_ENTER_CRITICAL();
    cb = s_shell_rx_cb;
    SYS_EXIT_CRITICAL();

    if (cb == 0) {
        return;
    }

    peer.type = SHELL_TP_NRT_FRAME;
    peer.session_id = 0u;
    cb(&peer, payload, len);
}

static void prv_on_shell_boot_detect(const uint8_t *payload, uint16_t len)
{
    static const uint8_t s_boot_status[] = SHELL_BOOT_STATUS_TEXT;
    (void)payload;
    (void)len;

    Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                              (uint8_t)DATA_TYPE_SHELL_BOOT_STATUS,
                              s_boot_status,
                              (uint8_t)(sizeof(s_boot_status) - 1u),
                              USE_DMA);
}

static int prv_nrt_init(void)
{
    return 0;
}

static int prv_nrt_start(shell_rx_cb_t rx_cb)
{
    if (rx_cb == 0) {
        return -1;
    }

    SYS_ENTER_CRITICAL();
    s_shell_rx_cb = rx_cb;
    SYS_EXIT_CRITICAL();

    Driver_Protocol_Register((uint8_t)DATA_TYPE_SHELL_REQ, prv_on_shell_req);
    Driver_Protocol_Register((uint8_t)DATA_TYPE_SHELL_BOOT_DETECT, prv_on_shell_boot_detect);
    return 0;
}

static int prv_nrt_send(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    uint16_t offset = 0u;

    if ((peer == 0) || (data == 0)) {
        return -1;
    }

    (void)peer;

    if (len == 0u) {
        return 0;
    }

    while (offset < len) {
        uint16_t remain = (uint16_t)(len - offset);
        uint16_t chunk = (remain > SHELL_NRT_TX_CHUNK_MAX) ? SHELL_NRT_TX_CHUNK_MAX : remain;

        Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                                  (uint8_t)DATA_TYPE_SHELL_RESP,
                                  &data[offset],
                                  (uint8_t)chunk,
                                  USE_DMA);
        offset = (uint16_t)(offset + chunk);
    }

    return 0;
}

static int prv_nrt_stop(void)
{
    SYS_ENTER_CRITICAL();
    s_shell_rx_cb = 0;
    SYS_EXIT_CRITICAL();

    /* 注销 shell 命令处理，避免 stop 后仍被分发 */
    Driver_Protocol_Register((uint8_t)DATA_TYPE_SHELL_REQ, 0);
    Driver_Protocol_Register((uint8_t)DATA_TYPE_SHELL_BOOT_DETECT, 0);
    return 0;
}

static const shell_transport_vtable_t s_shell_nrt_vtable = {
    prv_nrt_init,
    prv_nrt_start,
    prv_nrt_send,
    prv_nrt_stop
};

const shell_transport_vtable_t *Task_ShellTransportNRT_GetVTable(void)
{
    return &s_shell_nrt_vtable;
}
