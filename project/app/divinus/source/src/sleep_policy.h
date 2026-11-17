/* sleep_policy.h — сон камеры по отсутствию движения (OKO, [sleep]).
 *
 * Политика живёт в divinus и использует механику, проверенную на стенде
 * (2026-11-16): после простоя приложение полностью разбирает медиа-пайплайн,
 * программирует PMU timeout (0xff300000 + 0x048, 32 тика в секунду) и уводит
 * AP в deep suspend.  PMU будит AP, после resume процесс перезапускается
 * (execvp), поднимает пайплайн и в течение wake_check_s ждёт движение; если
 * его нет — снова засыпает.
 */

#ifndef SLEEP_POLICY_H
#define SLEEP_POLICY_H

#include <stddef.h>

/* Прочитать [sleep], проверить готовность платформы и env OKO_SLEEP_WAKE.
 * Вызывать после app_config_parse().  Не запускает поток. */
void sleep_policy_init(void);

/* Запустить фоновый поток политики (после старта media/IVS/YOLO). */
void sleep_policy_start(void);

/* Остановить поток; вызывать в начале shutdown, до остановки подсистем. */
void sleep_policy_stop(void);

/* Движение от IVS (принятое, с учётом min_area) или YOLO. */
void sleep_policy_report_motion(void);

/* main loop: пора ли разбирать пайплайн и уходить в suspend. */
int sleep_policy_should_suspend(void);

/* Выполнить suspend.  Блокируется до resume; вызывать после полного
 * teardown.  Возвращает 0, если AP проснулся, -1 — если suspend не удался
 * (тогда main должен просто перезапустить процесс). */
int sleep_policy_suspend(void);

/* main: отметить, что следующий экземпляр должен начать с окна проверки. */
void sleep_policy_mark_wake(void);

/* JSON-фрагмент без ведущей запятой: "sleep":{"enabled":...}. */
void sleep_policy_status_json(char *out, size_t size);

/* Плоский JSON для GET/POST /api/sleep: настройки + живой статус. */
void sleep_policy_api_json(char *out, size_t size);

/* 1, если ядро и DTS предоставляют AOV-прокси (не зависит от [sleep]). */
int sleep_policy_aov_available(void);

#endif /* SLEEP_POLICY_H */