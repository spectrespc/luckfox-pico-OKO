/*
 * boot_i2c.c - polling I2C slave and bootloader command protocol.
 *
 * Boot mode never enables interrupts, so the I2C peripheral is serviced from
 * the main loop.  A write transaction is executed when STOPF is seen; the
 * answer of write-type commands is kept in boot_last_status and returned on
 * the following read transaction.  This matches the host tools, which use a
 * plain write() followed by a separate read().
 */
#include "boot.h"
#include "oko_crc.h"

static volatile uint8_t i2c_rx_buf[BOOT_RX_BUF_SIZE];
static volatile uint8_t i2c_tx_buf[BOOT_TX_BUF_SIZE];
static volatile uint8_t i2c_rx_idx;
static volatile uint8_t i2c_rx_len;
static volatile uint8_t i2c_tx_idx;
static volatile uint8_t i2c_tx_len;
static volatile uint8_t i2c_state;      /* 0 idle, 1 receiver, 2 transmitter */

volatile uint8_t boot_last_status = BOOT_ST_OK;

/* Wire frames carry 32-bit big-endian addresses and lengths; the device only
 * has 8 KiB of program memory, so the high half must be zero. */
#define READ_U32_LOW16(p, out) \
    (((p)[0] != 0 || (p)[1] != 0) ? 0 : \
     (*(out) = oko_get_be16((const uint8_t *)&(p)[2]), 1))

static uint8_t boot_required_len(uint8_t cmd)
{
    switch (cmd)
    {
        case BOOT_CMD_INFO:   return 1;
        case BOOT_CMD_ERASE:  return 9;
        case BOOT_CMD_WRITE:  return 8;
        case BOOT_CMD_FINISH: return 9;
        case BOOT_CMD_RESET:  return 1;
        case OKO_CMD_GET_STATE: return 1;
        default:              return 1;
    }
}

static void boot_prepare_response(void)
{
    uint8_t cmd = i2c_rx_buf[0];
    uint8_t version[3];
    uint16_t length = 0;
    uint32_t crc = 0;

    i2c_tx_idx = 0;
    i2c_tx_len = 0;

    if (i2c_rx_len < boot_required_len(cmd))
    {
        i2c_tx_buf[0] = BOOT_ST_BAD_CMD;
        i2c_tx_len = 1;
        return;
    }

    switch (cmd)
    {
        case OKO_CMD_GET_STATE:
            i2c_tx_buf[0] = OKO_ROLE_BOOT;
            i2c_tx_buf[1] = OKO_BOOT_VERSION_MAJOR;
            i2c_tx_buf[2] = OKO_BOOT_VERSION_MINOR;
            i2c_tx_buf[3] = OKO_BOOT_VERSION_PATCH;
            i2c_tx_len = 4;
            break;
        case BOOT_CMD_INFO:
            boot_header_info(&length, &crc, version);
            i2c_tx_buf[0] = OKO_BOOT_VERSION_MAJOR;
            i2c_tx_buf[1] = OKO_BOOT_VERSION_MINOR;
            i2c_tx_buf[2] = OKO_BOOT_VERSION_PATCH;
            i2c_tx_buf[3] = version[0];
            i2c_tx_buf[4] = version[1];
            i2c_tx_buf[5] = version[2];
            i2c_tx_buf[6] = BOOT_WRITE_MAX;
            i2c_tx_buf[7] = BOOT_STATE_BOOT;
            i2c_tx_len = 8;
            break;
        case BOOT_CMD_ERASE:
        case BOOT_CMD_WRITE:
        case BOOT_CMD_FINISH:
        case BOOT_CMD_RESET:
            i2c_tx_buf[0] = boot_last_status;
            i2c_tx_len = 1;
            break;
        default:
            i2c_tx_buf[0] = BOOT_ST_BAD_CMD;
            i2c_tx_len = 1;
            break;
    }
}

static void boot_process_command(void)
{
    uint8_t cmd = i2c_rx_buf[0];

    if (i2c_rx_len < boot_required_len(cmd))
    {
        boot_last_status = BOOT_ST_BAD_CMD;
        return;
    }

    switch (cmd)
    {
        case BOOT_CMD_ERASE:
        {
            uint16_t start, length;

            if (!READ_U32_LOW16(i2c_rx_buf + 1, &start) ||
                !READ_U32_LOW16(i2c_rx_buf + 5, &length))
            {
                boot_last_status = BOOT_ST_BAD_ADDR;
                break;
            }
            boot_last_status = boot_flash_erase(start, length);
            break;
        }
        case BOOT_CMD_WRITE:
        {
            uint16_t address;
            uint8_t length = i2c_rx_buf[5];
            uint16_t crc_rx, crc_calc;

            if (length > BOOT_WRITE_MAX || i2c_rx_len != (uint8_t)(8 + length) ||
                !READ_U32_LOW16(i2c_rx_buf + 1, &address))
            {
                boot_last_status = (length > BOOT_WRITE_MAX) ?
                    BOOT_ST_BAD_ADDR : BOOT_ST_BAD_CMD;
                break;
            }

            crc_rx = oko_get_be16((const uint8_t *)&i2c_rx_buf[6 + length]);
            crc_calc = oko_crc16((const uint8_t *)&i2c_rx_buf[1],
                                 (uint16_t)(5 + length));
            if (crc_rx != crc_calc)
            {
                boot_last_status = BOOT_ST_BAD_CRC;
                break;
            }

            boot_last_status = boot_flash_write(address,
                (const uint8_t *)&i2c_rx_buf[6], length);
            break;
        }
        case BOOT_CMD_FINISH:
        {
            uint16_t length;
            uint32_t crc = oko_get_be32((const uint8_t *)&i2c_rx_buf[5]);

            if (!READ_U32_LOW16(i2c_rx_buf + 1, &length))
                boot_last_status = BOOT_ST_BAD_ADDR;
            else
                boot_last_status = boot_flash_finish(length, crc);
            break;
        }
        case BOOT_CMD_RESET:
            boot_last_status = BOOT_ST_OK;
            if (boot_app_valid())
                boot_jump_app();
            boot_last_status = BOOT_ST_VERIFY_ERR;
            break;
        case BOOT_CMD_INFO:
        case OKO_CMD_GET_STATE:
            break;
        default:
            boot_last_status = BOOT_ST_BAD_CMD;
            break;
    }
}

void boot_i2c_poll(void)
{
    uint8_t sr1 = I2C->SR1;
    uint8_t sr2 = I2C->SR2;

    /* Address matched. */
    if (sr1 & 0x02)
    {
        uint8_t sr3 = I2C->SR3;

        if (sr3 & 0x04)
        {
            /* Slave transmitter: the master is reading the last response. */
            i2c_rx_len = i2c_rx_idx;   /* combined write+read transaction */
            boot_prepare_response();
            i2c_state = 2;
            i2c_tx_idx = 0;
            I2C->DR = (i2c_tx_len > 0) ? i2c_tx_buf[i2c_tx_idx++] : 0x00;
        }
        else
        {
            uint8_t i;

            i2c_state = 1;
            i2c_rx_idx = 0;
            i2c_rx_len = 0;
            for (i = 0; i < BOOT_RX_BUF_SIZE; i++)
                i2c_rx_buf[i] = 0;
        }
    }

    /* Stop: the write phase is complete, execute write-type commands.  A stop
     * after a read phase must not repeat the command. */
    if (sr1 & 0x10)
    {
        uint8_t was_receiving = (i2c_state == 1);

        I2C->CR2 = I2C->CR2;   /* clears STOPF together with the SR1 read */
        i2c_rx_len = i2c_rx_idx;
        i2c_state = 0;
        if (was_receiving && i2c_rx_len > 0)
            boot_process_command();
    }

    /* Received byte. */
    if (sr1 & 0x40)
    {
        uint8_t d = I2C->DR;
        if (i2c_rx_idx < BOOT_RX_BUF_SIZE)
            i2c_rx_buf[i2c_rx_idx++] = d;
    }

    /* Transmit empty. */
    if (sr1 & 0x80)
    {
        if (i2c_state == 2 && i2c_tx_idx < i2c_tx_len)
            I2C->DR = i2c_tx_buf[i2c_tx_idx++];
        else
            I2C->DR = 0x00;
    }

    /* Bus errors: clear them; re-arm the peripheral only on BERR/ARLO/OVR. */
    if (sr2 & 0x0F)
    {
        uint8_t fatal = sr2 & 0x0B;
        I2C->SR2 = (uint8_t)~(uint8_t)0x0F;
        if (fatal)
        {
            I2C->CR1 &= (uint8_t)~0x01;
            I2C->CR1 |= 0x01;
        }
        i2c_state = 0;
    }
}