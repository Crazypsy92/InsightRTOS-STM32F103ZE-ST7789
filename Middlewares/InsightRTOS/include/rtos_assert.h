#ifndef INSIGHT_RTOS_ASSERT_H
#define INSIGHT_RTOS_ASSERT_H

_Noreturn void rtos_assert_fail(const char *expression, const char *file,
                               unsigned int line);

#define RTOS_ASSERT(expression)                                                \
    do {                                                                       \
        if (!(expression)) {                                                   \
            rtos_assert_fail(#expression, __FILE__, (unsigned int)__LINE__);     \
        }                                                                      \
    } while (0)

#endif
