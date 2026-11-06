#ifndef EC_LOCK_H
#define EC_LOCK_H

/* Advisory lock shared by "oko-ec daemon" and "oko-ec flash".
 * Returns 0 when the lock is held, -1 when another process owns it. */
int ec_lock_take(int blocking);
void ec_lock_release(void);

#endif /* EC_LOCK_H */
