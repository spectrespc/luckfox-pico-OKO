/* tools/bootloader_tool/i2c_transport.h
 *
 * Абстракция I2C-транспорта хоста: Linux i2c-dev (RV1106/RPi) и
 * USB-I2C мост CP2112 (он же i2c-dev, но с задержками и повторами).
 */

#ifndef I2C_TRANSPORT_H
#define I2C_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Задержка между записью и чтением (CP2112 — медленный USB-I2C мост). */
#define I2C_WRITE_READ_DELAY_US 100000 /* 100 мс */

/* Количество попыток чтения ответа. */
#define I2C_READ_RETRIES 5

/* Количество попыток записи кадра. Первый кадр после перезагрузки
 * хоста — именно запись, поэтому одной попытки недостаточно: шина или
 * EC могут быть ещё не готовы. Повтор идёт только после неудачной
 * записи и предваряется probe, чтобы не дублировать команду на живой
 * шине. */
#define I2C_WRITE_RETRIES 2

/* Молчаливый режим: не печатать ошибки транспорта (демон и scan). */
void i2c_transport_set_verbose(int enable);
int i2c_transport_verbose(void);

/* Открыть I2C-устройство и выставить 7-битный адрес slave.
 * Возврат: fd >= 0 при успехе, -1 при ошибке. */
int i2c_transport_open(const char *device, uint8_t addr);

/* Записать wbuf, выдержать паузу и прочитать ответ в rbuf.
 * Возврат: 0 при успехе, -1 при ошибке. */
int i2c_transport_write_read(int fd, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen);

/* То же, но с явными задержкой и числом попыток.
 * Возврат: количество прочитанных байт (>= 3) при успехе, -1 при ошибке. */
int i2c_transport_write_read_timed(int fd, const uint8_t *wbuf, size_t wlen,
                                   uint8_t *rbuf, size_t rlen,
                                   unsigned delay_us, unsigned retries);

/* Проверить, отвечает ли устройство на адресе.
 * Возврат: 1 — есть ACK, 0 — адрес свободен, -1 — ошибка открытия шины. */
int i2c_transport_probe(const char *device, uint8_t addr);

/* Записать buf без чтения ответа (например, CMD_RESET). */
int i2c_transport_write_only(int fd, const uint8_t *buf, size_t len);

#endif /* I2C_TRANSPORT_H */