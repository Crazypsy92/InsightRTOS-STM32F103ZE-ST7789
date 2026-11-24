#include "app_ipc.h"

static rtos_queue_t raw_keys, controller_keys, lcd_commands, lcd_acks, monitor_messages;
static display_key_t raw_storage[APP_KEY_QUEUE_CAPACITY];
static display_key_t controller_storage[APP_KEY_QUEUE_CAPACITY];
static display_command_t command_storage[APP_COMMAND_QUEUE_CAPACITY];
static display_ack_t ack_storage[APP_ACK_QUEUE_CAPACITY];
static display_metric_t metric_storage[APP_METRIC_QUEUE_CAPACITY];

rtos_status_t App_IPC_Init(void)
{
    rtos_status_t status;
    status = rtos_queue_init(&raw_keys, raw_storage, sizeof(raw_storage),
                             sizeof(raw_storage[0]), APP_KEY_QUEUE_CAPACITY);
    if (status != RTOS_OK) return status;

    status = rtos_queue_init(&controller_keys, controller_storage, sizeof(controller_storage),
                             sizeof(controller_storage[0]), APP_KEY_QUEUE_CAPACITY);
    if (status != RTOS_OK) return status;

    status = rtos_queue_init(&lcd_commands, command_storage, sizeof(command_storage),
                             sizeof(command_storage[0]), APP_COMMAND_QUEUE_CAPACITY);
    if (status != RTOS_OK) return status;

    status = rtos_queue_init(&lcd_acks, ack_storage, sizeof(ack_storage),
                             sizeof(ack_storage[0]), APP_ACK_QUEUE_CAPACITY);
    if (status != RTOS_OK) return status;

    status = rtos_queue_init(&monitor_messages, metric_storage, sizeof(metric_storage),
                             sizeof(metric_storage[0]), APP_METRIC_QUEUE_CAPACITY);
    return status;
}

rtos_status_t App_IPC_SendKeyFromISR(const display_key_t *key)
{
    return rtos_queue_send_from_isr(&raw_keys, key);
}

rtos_status_t App_IPC_SendRawKeyFromThread(const display_key_t *key, uint32_t timeout)
{
    return rtos_queue_send(&raw_keys, key, timeout);
}

rtos_status_t App_IPC_ReceiveRawKey(display_key_t *key, uint32_t timeout)
{
    return rtos_queue_receive(&raw_keys, key, timeout);
}

rtos_status_t App_IPC_SendControllerKey(const display_key_t *key, uint32_t timeout)
{
    return rtos_queue_send(&controller_keys, key, timeout);
}

rtos_status_t App_IPC_ReceiveControllerKey(display_key_t *key, uint32_t timeout)
{
    return rtos_queue_receive(&controller_keys, key, timeout);
}

rtos_status_t App_IPC_SendCommand(const display_command_t *cmd, uint32_t timeout)
{
    return rtos_queue_send(&lcd_commands, cmd, timeout);
}

rtos_status_t App_IPC_ReceiveCommand(display_command_t *cmd, uint32_t timeout)
{
    return rtos_queue_receive(&lcd_commands, cmd, timeout);
}

rtos_status_t App_IPC_SendAck(const display_ack_t *ack, uint32_t timeout)
{
    return rtos_queue_send(&lcd_acks, ack, timeout);
}

rtos_status_t App_IPC_ReceiveAck(display_ack_t *ack, uint32_t timeout)
{
    return rtos_queue_receive(&lcd_acks, ack, timeout);
}

rtos_status_t App_IPC_SendMetric(const display_metric_t *metric, uint32_t timeout)
{
    return rtos_queue_send(&monitor_messages, metric, timeout);
}

rtos_status_t App_IPC_ReceiveMetric(display_metric_t *metric, uint32_t timeout)
{
    return rtos_queue_receive(&monitor_messages, metric, timeout);
}

void App_IPC_GetQueueDepths(uint32_t depths[5])
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    depths[0] = raw_keys.count;
    depths[1] = controller_keys.count;
    depths[2] = lcd_commands.count;
    depths[3] = lcd_acks.count;
    depths[4] = monitor_messages.count;
    rtos_critical_exit(saved);
}
