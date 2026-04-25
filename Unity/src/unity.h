/*
 * unity.h  –  Unity test framework 
 *
 * 
 *   UNITY_BEGIN / UNITY_END 
 *   RUN_TEST
 *   TEST_ASSERT_TRUE / FALSE
 *   TEST_ASSERT_EQUAL_INT / UINT8 / UINT16 / UINT32 / EQUAL
 *   TEST_ASSERT_GREATER_OR_EQUAL / LESS_OR_EQUAL
 *   TEST_ASSERT_GREATER_THAN
 *   TEST_ASSERT_FLOAT_WITHIN
 *   TEST_PASS / TEST_FAIL
 */
#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>   


static int unity_tests_run    = 0;
static int unity_tests_passed = 0;
static int unity_tests_failed = 0;
static const char *unity_current_test = "";



#define UNITY_BEGIN() \
    do { unity_tests_run=0; unity_tests_passed=0; unity_tests_failed=0; } while(0)


static inline int _unity_end(void) {
    printf("\n------- %d Tests  %d Failures  0 Ignored\n",
           unity_tests_run, unity_tests_failed);
    if (unity_tests_failed == 0) printf("OK\n"); else printf("FAIL\n");
    return unity_tests_failed;
}
#define UNITY_END() _unity_end()


#define RUN_TEST(fn) \
    do { \
        unity_tests_run++; \
        unity_current_test = #fn; \
        fn(); \
    } while(0)


#define _UNITY_FAIL_MSG(msg) \
    do { \
        printf("FAIL: %s\n  -> %s (line %d)\n", unity_current_test, (msg), __LINE__); \
        unity_tests_failed++; \
        return; \
    } while(0)

#define _UNITY_PASS() \
    do { unity_tests_passed++; } while(0)

#define TEST_ASSERT_TRUE(cond) \
    do { if (!(cond)) _UNITY_FAIL_MSG("Expected TRUE"); else _UNITY_PASS(); } while(0)

#define TEST_ASSERT_FALSE(cond) \
    do { if ((cond)) _UNITY_FAIL_MSG("Expected FALSE"); else _UNITY_PASS(); } while(0)

#define TEST_ASSERT_EQUAL_INT(exp, act) \
    do { \
        if ((int)(exp) != (int)(act)) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), "Expected %d  Got %d", (int)(exp), (int)(act)); \
            _UNITY_FAIL_MSG(_buf); \
        } else _UNITY_PASS(); \
    } while(0)

#define TEST_ASSERT_EQUAL(exp, act)        TEST_ASSERT_EQUAL_INT((exp), (act))
#define TEST_ASSERT_EQUAL_UINT8(exp, act)  TEST_ASSERT_EQUAL_INT((int)(uint8_t)(exp),  (int)(uint8_t)(act))
#define TEST_ASSERT_EQUAL_UINT16(exp, act) TEST_ASSERT_EQUAL_INT((int)(uint16_t)(exp), (int)(uint16_t)(act))
#define TEST_ASSERT_EQUAL_UINT32(exp, act) TEST_ASSERT_EQUAL_INT((int)(uint32_t)(exp), (int)(uint32_t)(act))

#define TEST_ASSERT_GREATER_OR_EQUAL(exp, act) \
    do { \
        if (!((act) >= (exp))) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), "Expected >= %d  Got %d", (int)(exp), (int)(act)); \
            _UNITY_FAIL_MSG(_buf); \
        } else _UNITY_PASS(); \
    } while(0)

#define TEST_ASSERT_LESS_OR_EQUAL(exp, act) \
    do { \
        if (!((act) <= (exp))) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), "Expected <= %d  Got %d", (int)(exp), (int)(act)); \
            _UNITY_FAIL_MSG(_buf); \
        } else _UNITY_PASS(); \
    } while(0)

#define TEST_ASSERT_GREATER_THAN(threshold, act) \
    do { \
        if (!((act) > (threshold))) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), "Expected > %d  Got %d", (int)(threshold), (int)(act)); \
            _UNITY_FAIL_MSG(_buf); \
        } else _UNITY_PASS(); \
    } while(0)

#define TEST_ASSERT_GREATER_THAN_UINT(threshold, act) \
    TEST_ASSERT_GREATER_THAN((threshold), (act))

                      */
#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) \
    do { \
        float _diff = fabsf((float)(actual) - (float)(expected)); \
        if (_diff > (float)(delta)) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), \
                     "Float diff %.6f > delta %.6f", (double)_diff, (double)(delta)); \
            _UNITY_FAIL_MSG(_buf); \
        } else _UNITY_PASS(); \
    } while(0)

#define TEST_PASS() _UNITY_PASS()
#define TEST_FAIL() _UNITY_FAIL_MSG("TEST_FAIL called explicitly")
#define TEST_FAIL_MESSAGE(msg) _UNITY_FAIL_MSG(msg)

#endif /* UNITY_H */
