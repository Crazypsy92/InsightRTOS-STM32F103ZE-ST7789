/**
 * @file    spi_bus.h
 * @brief   SPI 总线抽象接口（MCU 无关）
 * @details 纯接口层：不包含任何 MCU / LL / HAL 头文件。
 *          驱动与移植层只依赖本文件；具体实现（如 spi_stm32_ll.c）负责
 *          产生真实 SPI 时序。换 MCU 时替换实现文件即可。
 *
 *          总线按"物理外设"建模：同一 SPI 外设上的多个设备共享一个 spi_bus_t，
 *          每次传输前通过 spi_bus_acquire() 占用总线（切速率/模式），
 *          事务结束后 spi_bus_release() 释放，避免两个设备互相打断。
 *
 *          并发模型（使用前务必理解）：
 *          1. 独占式：一次 acquire..release 之间，总线只属于一个设备；
 *             期间 transfer() 是阻塞的（DMA 也等 TC/BSY 才返回），
 *             所以同一时刻只有一条 SPI 事务在跑，不存在交叉改写。
 *          2. 中断安全：ISR 内调用 acquire() 会被直接拒绝（实现方通过 in_isr
 *             回调上报中断上下文）。原因：ISR 若插入主循环事务中间，
 *             会改掉 ll->current 的速率/模式并重配 DMA 通道。
 *             若 ISR 确实要访问 SPI，应把请求排入队列，由主循环统一执行。
 *          3. 异步（中断/DMA 完成回调）尚未支持：当前 release 必须在
 *             transfer() 返回后立即执行；改成非阻塞 DMA 时，release 需要
 *             挪到 "传输完成" 回调里，并给 acquire 增加忙等待/超时语义。
 *
 *          状态机（所有状态迁移都封装在本文件的 inline 函数里）：
 *
 *            UNINIT --configure OK--> READY --acquire--> BUSY
 *            BUSY   --release(depth==0)--> READY
 *            BUSY   --transfer 失败--> ERROR
 *            ERROR  --release(depth==0)--> ERROR（保留故障，等待恢复）
 *            ERROR  --recover OK--> READY
 *            ERROR  --下一次 acquire--> 自动 recover，成功后才允许事务
 *
 *          错误码统一使用 spi_bus_status_t，实现方必须返回同一套枚举，
 *          避免上层把 -2 之类魔数硬解释成"超时"。
 */
#ifndef __SPI_BUS_H__
#define __SPI_BUS_H__

#include <stdint.h>
#include <stddef.h>

/** @brief 统一错误码：0 成功，负值失败 */
typedef enum {
    SPI_BUS_OK             = 0,   /**< 成功 */
    SPI_BUS_ERR_PARAM      = -1,  /**< 参数非法 */
    SPI_BUS_ERR_TIMEOUT    = -2,  /**< 等待超时（TXE/RXNE/BSY/DMA） */
    SPI_BUS_ERR_DMA        = -3,  /**< DMA 传输错误（TEIF） */
    SPI_BUS_ERR_BUSY       = -4,  /**< 外设正忙 / 总线被其它设备占用 */
    SPI_BUS_ERR_RECOVERY   = -5,  /**< 异常恢复失败 */
    SPI_BUS_ERR_ISR        = -6,  /**< 中断上下文中禁止占用总线 */
    SPI_BUS_ERR_STATE      = -7,  /**< 状态机不允许该操作（未占用/顺序错误） */
    SPI_BUS_ERR_CONFIG     = -8,  /**< 嵌套事务的配置不一致 */
    SPI_BUS_ERR_UNSUPPORTED = -9, /**< 实现方未提供该操作（如 recover） */
} spi_bus_status_t;

/** @brief 总线状态机 */
typedef enum {
    SPI_BUS_STATE_UNINIT = 0,  /**< 尚未完成首次 configure */
    SPI_BUS_STATE_READY,       /**< 空闲可用 */
    SPI_BUS_STATE_BUSY,        /**< 已被某个设备占用 */
    SPI_BUS_STATE_ERROR        /**< 上一次事务失败，等待 recover */
} spi_bus_state_t;

/** @brief SPI 时钟模式（极性与相位） */
typedef enum {
    SPI_BUS_MODE_0 = 0,   /* CPOL=0, CPHA=0 */
    SPI_BUS_MODE_1,       /* CPOL=0, CPHA=1 */
    SPI_BUS_MODE_2,       /* CPOL=1, CPHA=0 */
    SPI_BUS_MODE_3        /* CPOL=1, CPHA=1 */
} spi_bus_mode_t;

/** @brief 设备级总线参数 */
typedef struct {
    uint32_t       clock_hz;   /* 目标 SCLK 频率（Hz），实现方可向下取整到可达分频 */
    spi_bus_mode_t mode;       /* 时钟模式 */
    uint8_t        use_dma;    /* 1: 大块传输使用 DMA；0: 全部轮询。实现方对小块仍走轮询 */
} spi_bus_config_t;

/** @brief 总线对象（不透明） */
typedef struct spi_bus spi_bus_t;

/** @brief 总线操作表：由具体实现填充 */
typedef struct {
    /**
     * @brief 切换总线参数
     * @return spi_bus_status_t（SPI_BUS_OK 成功）
     * @note  实现方应缓存当前参数，参数未变化时直接返回，避免重复配置外设
     */
    int (*configure)(spi_bus_t *bus, const spi_bus_config_t *cfg);

    /**
     * @brief 全双工传输 len 字节
     * @param tx  发送缓冲区，不能为 NULL
     * @param rx  接收缓冲区，可为 NULL（只发不收）
     * @return spi_bus_status_t（SPI_BUS_OK 成功）
     * @note  片选由设备层管理，本函数不操作 CS
     */
    int (*transfer)(spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len);

    /**
     * @brief 可选：把 SPI/DMA 从异常态拉回可用态
     * @return spi_bus_status_t
     * @note  NULL 表示实现方不支持恢复，此时 spi_bus_recover() 返回
     *        SPI_BUS_ERR_UNSUPPORTED。恢复过程不得操作任何设备 CS。
     */
    int (*recover)(spi_bus_t *bus);

    /**
     * @brief 可选：返回 1 表示当前处于中断上下文（ISR）
     * @note  NULL 表示不做上下文检查。提供后 ISR 中的 acquire() 返回
     *        SPI_BUS_ERR_ISR，用于防止中断抢占正在进行的 DMA 事务
     */
    int (*in_isr)(spi_bus_t *bus);
} spi_bus_ops_t;

struct spi_bus {
    const spi_bus_ops_t *ops;          /* 操作表 */
    void                *ctx;          /* 实现方私有上下文 */
    const void          *owner;        /* 当前事务占用者（设备传自己的上下文指针），NULL=空闲 */
    uint16_t             depth;        /* 事务嵌套深度：同一 owner 可重入 */
    volatile spi_bus_state_t state;    /* 状态机当前状态 */
    volatile int32_t     last_error;   /* 最近一次失败的错误码，SPI_BUS_OK 表示无故障 */
    volatile uint32_t    rejects;      /* 被拒绝的占用请求计数（ISR / 他设备占用），调试与测试用 */
    spi_bus_config_t     current_cfg;  /* 当前事务生效的设备配置，用于嵌套一致性比较 */
};

/** @brief 查询总线状态 */
static inline spi_bus_state_t spi_bus_get_state(const spi_bus_t *bus)
{
    return (bus == NULL) ? SPI_BUS_STATE_UNINIT : bus->state;
}

/** @brief 查询最近一次错误码（SPI_BUS_OK 表示无故障） */
static inline int spi_bus_get_last_error(const spi_bus_t *bus)
{
    return (bus == NULL) ? SPI_BUS_ERR_PARAM : (int)bus->last_error;
}

/** @brief 清除错误记录（不改变状态机） */
static inline void spi_bus_clear_error(spi_bus_t *bus)
{
    if (bus != NULL) {
        bus->last_error = SPI_BUS_OK;
    }
}

/**
 * @brief 把总线从 ERROR 拉回 READY（空闲时调用）
 * @return SPI_BUS_OK 成功；SPI_BUS_ERR_BUSY 事务尚未释放；
 *         SPI_BUS_ERR_UNSUPPORTED 实现方未提供 recover；其它为实现方错误码
 * @note  成功后会清空 last_error；失败则保持 ERROR 状态。
 */
static inline int spi_bus_recover(spi_bus_t *bus)
{
    if (bus == NULL || bus->ops == NULL) {
        return SPI_BUS_ERR_PARAM;
    }
    if (bus->ops->recover == NULL) {
        return SPI_BUS_ERR_UNSUPPORTED;
    }
    if (bus->owner != NULL || bus->depth != 0U) {
        return SPI_BUS_ERR_BUSY;
    }

    int ret = bus->ops->recover(bus);
    if (ret == SPI_BUS_OK) {
        bus->state      = SPI_BUS_STATE_READY;
        bus->last_error = SPI_BUS_OK;
    } else {
        bus->state      = SPI_BUS_STATE_ERROR;
        bus->last_error = ret;
    }
    return ret;
}

/** @brief 切换总线参数（空指针安全）
 *  @note  仅允许总线空闲（owner==NULL 且非 BUSY）时调用，避免改动正在执行的事务参数；
 *         同 owner 嵌套由 acquire() 内联处理，不走到这里。
 */
static inline int spi_bus_configure(spi_bus_t *bus, const spi_bus_config_t *cfg)
{
    if (bus == NULL || bus->ops == NULL || bus->ops->configure == NULL || cfg == NULL) {
        return SPI_BUS_ERR_PARAM;
    }
    if (bus->owner != NULL || bus->state == SPI_BUS_STATE_BUSY) {
        return SPI_BUS_ERR_BUSY;    /* 总线已被占用，禁止改配置 */
    }
    if (bus->state == SPI_BUS_STATE_ERROR) {
        return SPI_BUS_ERR_STATE;   /* 故障态必须先 recover */
    }

    int ret = bus->ops->configure(bus, cfg);
    if (ret == SPI_BUS_OK) {
        bus->state = SPI_BUS_STATE_READY;
    } else {
        bus->last_error = ret;
    }
    return ret;
}

/** @brief 两个配置是否完全一致（clock/mode/use_dma 逐字段比较） */
static inline int spi_bus_cfg_equal(const spi_bus_config_t *a, const spi_bus_config_t *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return (a->clock_hz == b->clock_hz) &&
           (a->mode     == b->mode) &&
           (a->use_dma  == b->use_dma);
}

/**
 * @brief 占用总线：切到本设备参数并标记占用者
 * @param bus    总线句柄
 * @param owner  设备上下文指针（唯一标识一个设备，不能为 NULL）
 * @param cfg    本设备的速率/模式配置
 * @return SPI_BUS_OK 成功；SPI_BUS_ERR_ISR / SPI_BUS_ERR_BUSY /
 *         SPI_BUS_ERR_STATE / SPI_BUS_ERR_CONFIG / SPI_BUS_ERR_PARAM
 * @note  - 首次占用才 configure；同 owner 嵌套不重配，且 cfg 必须与首次一致。
 *        - 处于 ERROR 时先自动 recover，恢复失败则本次占用失败。
 *        - 片选由设备层管理，本函数不操作 CS。
 */
static inline int spi_bus_acquire(spi_bus_t *bus, const void *owner, const spi_bus_config_t *cfg)
{
    if (bus == NULL || bus->ops == NULL || owner == NULL || cfg == NULL) {
        return SPI_BUS_ERR_PARAM;
    }
    /* 中断上下文不允许占用总线：否则会插进主循环的 DMA 事务中间改配置 */
    if (bus->ops->in_isr != NULL && bus->ops->in_isr(bus) != 0) {
        bus->rejects++;
        return SPI_BUS_ERR_ISR;
    }
    /* 不变式：owner 与 depth 必须同时为零；违反则说明有代码漏掉 release */
    if ((bus->owner == NULL) != (bus->depth == 0U)) {
        bus->state      = SPI_BUS_STATE_ERROR;
        bus->last_error = SPI_BUS_ERR_STATE;
        bus->rejects++;
        return SPI_BUS_ERR_STATE;
    }
    /* 上个事务失败：先尝试把 SPI/DMA 拉回可用态 */
    if (bus->state == SPI_BUS_STATE_ERROR) {
        if (bus->owner != NULL) {
            return SPI_BUS_ERR_STATE;   /* 未释放，不允许在事务中途恢复 */
        }
        int rec = spi_bus_recover(bus);
        if (rec != SPI_BUS_OK) {
            bus->rejects++;
            return rec;
        }
    }
    /* 已被其它设备占用，拒绝抢占 */
    if (bus->owner != NULL && bus->owner != owner) {
        bus->rejects++;
        return SPI_BUS_ERR_BUSY;
    }
    /* depth 溢出保护：达到上限后不再嵌套 */
    if (bus->depth == UINT16_MAX) {
        bus->rejects++;
        return SPI_BUS_ERR_STATE;
    }

    if (bus->owner == NULL) {
        /* 首次占用：切到本设备参数，失败则不占用 */
        int ret = spi_bus_configure(bus, cfg);
        if (ret != SPI_BUS_OK) {
            return ret;
        }
        bus->owner       = owner;
        bus->depth       = 1U;
        bus->state       = SPI_BUS_STATE_BUSY;
        bus->current_cfg = *cfg;
    } else {
        /* 同 owner 嵌套：不重配 SPI；配置必须与首次完全一致，否则可能破坏事务 */
        if (!spi_bus_cfg_equal(&bus->current_cfg, cfg)) {
            bus->rejects++;
            return SPI_BUS_ERR_CONFIG;
        }
        bus->depth++;
    }
    return SPI_BUS_OK;
}

/**
 * @brief 释放总线（与 acquire 成对调用）
 * @return 1 总线已被本次释放完全交还（depth 归零，可拉高 CS）；
 *         0 仍为同 owner 嵌套占用（depth>0，CS 必须保持有效）；
 *        SPI_BUS_ERR_PARAM / SPI_BUS_ERR_STATE 参数或状态错误
 * @note  若事务已进入 ERROR，release 只归还占用权，不改变 ERROR，
 *        以便上层读取错误并在下一次 acquire 时自动恢复。
 */
static inline int spi_bus_release(spi_bus_t *bus, const void *owner)
{
    if (bus == NULL || owner == NULL) {
        return SPI_BUS_ERR_PARAM;
    }
    /* 不变式违反 */
    if ((bus->owner == NULL) != (bus->depth == 0U)) {
        bus->state      = SPI_BUS_STATE_ERROR;
        bus->last_error = SPI_BUS_ERR_STATE;
        return SPI_BUS_ERR_STATE;
    }
    if (bus->owner != owner) {
        return SPI_BUS_ERR_STATE;   /* 非占用者无权释放 */
    }
    if (bus->depth == 0U) {
        return SPI_BUS_ERR_STATE;   /* 没有可释放的嵌套层级 */
    }

    bus->depth--;
    if (bus->depth == 0U) {
        bus->owner = NULL;
        if (bus->state == SPI_BUS_STATE_BUSY) {
            bus->state = SPI_BUS_STATE_READY;
        }
        return 1;           /* 完全释放，port 层可拉高 CS */
    }
    return 0;               /* 仍有嵌套，CS 保持 */
}

/** @brief 查询当前占用者（无占用返回 NULL） */
static inline const void *spi_bus_owner(const spi_bus_t *bus)
{
    return (bus == NULL) ? NULL : bus->owner;
}

/** @brief 查询当前嵌套深度 */
static inline uint16_t spi_bus_depth(const spi_bus_t *bus)
{
    return (bus == NULL) ? 0U : bus->depth;
}

/**
 * @brief 总线传输（空指针安全）
 * @param owner 必须是当前总线占用者，且 depth > 0
 * @return SPI_BUS_OK 成功，其它为错误码
 * @note  必须先 spi_bus_acquire() 取得总线；失败会令状态机进入 ERROR，
 *        下一次 acquire 自动触发 recover。
 */
static inline int spi_bus_transfer(spi_bus_t *bus, const void *owner,
                                   const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (bus == NULL || bus->ops == NULL || bus->ops->transfer == NULL ||
        owner == NULL || tx == NULL || len == 0U) {
        return SPI_BUS_ERR_PARAM;
    }
    /* 只有真正持有总线的设备才能传输，且必须处于某个事务层级中 */
    if (bus->owner != owner || bus->depth == 0U ||
        bus->state != SPI_BUS_STATE_BUSY) {
        bus->rejects++;
        return SPI_BUS_ERR_STATE;
    }

    int ret = bus->ops->transfer(bus, tx, rx, len);
    if (ret != SPI_BUS_OK) {
        bus->state      = SPI_BUS_STATE_ERROR;
        bus->last_error = ret;
    }
    return ret;
}

#endif /* __SPI_BUS_H__ */
