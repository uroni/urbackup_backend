/** This standalone test harness tests that the asserts themselves function properly */

#define AWS_TESTING_REPORT_FD g_test_filedes
#include <stdio.h>
#include <string.h>

FILE *g_test_filedes;

#include <aws/testing/aws_test_harness.h>

#define NO_MORE_TESTS 12345
#define BAILED_OUT 98765

#ifdef _MSC_VER
/* disable warning about fopen() this is just a test */
#    pragma warning(disable : 4996)
/* disable warning about unreferenced formal parameter */
#    pragma warning(disable : 4100)
#endif

const char *g_test_filename;
int g_cur_line;
int g_expected_return;
int g_bail_out;

#define TEST_SUCCESS(name)                                                                                             \
    if (g_bail_out)                                                                                                    \
        return BAILED_OUT;                                                                                             \
    if (begin_test(index, #name, __FILE__, __LINE__, 0))

#define TEST_FAILURE(name)                                                                                             \
    if (g_bail_out)                                                                                                    \
        return BAILED_OUT;                                                                                             \
    if (begin_test(index, #name, __FILE__, __LINE__, -1))

const char *g_cur_testname, *g_cur_file;

int begin_test(int *index, const char *testname, const char *file, int line, int expected) {
    if (*index <= line) {
        *index = line;
        g_cur_testname = testname;
        g_cur_file = file;
        g_cur_line = line;
        g_expected_return = expected == 0 ? BAILED_OUT : expected;
        g_bail_out = 1;
        return 1;
    }

    return 0;
}

static int side_effect_ctr = 0;

int side_effect(void) {
    if (side_effect_ctr++) {
        fprintf(
            stderr,
            "***FAILURE*** Side effects triggered multiple times, after %s:%d (%s)",
            g_cur_file,
            g_cur_line,
            g_cur_testname);
        abort();
    }

    return 0;
}

/* NOLINTNEXTLINE(readability-function-size) */
int test_asserts(int *index) {
    TEST_SUCCESS(null_test) {}
    TEST_FAILURE(null_failure_test) {
        fprintf(AWS_TESTING_REPORT_FD, "***FAILURE*** test\n");
        return FAILURE;
    }

    TEST_FAILURE(basic_fail_1) { FAIL("Failed: %d", 42); }
    TEST_FAILURE(assert_bool) { ASSERT_TRUE(0); }
    TEST_FAILURE(assert_bool) { ASSERT_TRUE(0, "foo %d", 42); }
    TEST_FAILURE(assert_bool) { ASSERT_FALSE(1); }
    TEST_FAILURE(assert_bool) { ASSERT_FALSE(1, "foo %d", 42); }
    TEST_SUCCESS(assert_bool) { ASSERT_TRUE(1); }
    TEST_SUCCESS(assert_bool) { ASSERT_TRUE(2); }
    TEST_SUCCESS(assert_bool) { ASSERT_FALSE(0); }
    TEST_SUCCESS(assert_success) { ASSERT_SUCCESS(AWS_OP_SUCCESS); }
    TEST_SUCCESS(assert_success) { ASSERT_SUCCESS(AWS_OP_SUCCESS, "foo"); }
    TEST_FAILURE(assert_success) { ASSERT_SUCCESS(aws_raise_error(AWS_ERROR_OOM), "foo"); }

    TEST_SUCCESS(assert_fails) { ASSERT_FAILS(aws_raise_error(AWS_ERROR_OOM)); }
    TEST_SUCCESS(assert_fails) { ASSERT_FAILS(aws_raise_error(AWS_ERROR_OOM), "foo"); }
    TEST_FAILURE(assert_fails) { ASSERT_FAILS(AWS_OP_SUCCESS, "foo"); }

    TEST_SUCCESS(assert_error) { ASSERT_ERROR(AWS_ERROR_OOM, aws_raise_error(AWS_ERROR_OOM)); }
    TEST_SUCCESS(assert_error_side_effect) {
        ASSERT_ERROR((side_effect(), AWS_ERROR_OOM), aws_raise_error(AWS_ERROR_OOM));
    }
    TEST_SUCCESS(assert_error_side_effect) {
        ASSERT_ERROR(AWS_ERROR_OOM, (side_effect(), aws_raise_error(AWS_ERROR_OOM)));
    }
    TEST_SUCCESS(assert_error) { ASSERT_ERROR(AWS_ERROR_OOM, aws_raise_error(AWS_ERROR_OOM), "foo"); }
    TEST_FAILURE(assert_error) { ASSERT_ERROR(AWS_ERROR_CLOCK_FAILURE, aws_raise_error(AWS_ERROR_OOM), "foo"); }
    aws_raise_error(AWS_ERROR_CLOCK_FAILURE); // set last error
    TEST_FAILURE(assert_error) { ASSERT_ERROR(AWS_ERROR_CLOCK_FAILURE, AWS_OP_SUCCESS, "foo"); }

    TEST_SUCCESS(assert_null) { ASSERT_NULL(NULL); }
    {
        const struct forward_decl *nullp2 = NULL;
        TEST_SUCCESS(assert_null) {
            void *nullp = NULL;
            ASSERT_NULL(nullp);
        }
        TEST_SUCCESS(assert_null) { ASSERT_NULL(nullp2); }
        TEST_SUCCESS(assert_null_sideeffects) { ASSERT_NULL((side_effect(), nullp2)); }
    }
    TEST_SUCCESS(assert_null) { ASSERT_NULL(0, "foo"); }
    TEST_FAILURE(assert_null) { ASSERT_NULL("hello world", "foo"); }

    TEST_SUCCESS(inteq) { ASSERT_INT_EQUALS(4321, 4321); }
    TEST_SUCCESS(inteq) { ASSERT_INT_EQUALS(4321, 4321, "foo"); }
    TEST_SUCCESS(inteq_side_effects) {
        int increment = 4321;
        ASSERT_INT_EQUALS(4321, increment++, "foo");
        ASSERT_INT_EQUALS(4322, increment++, "foo");
    }
    TEST_FAILURE(inteq_difference) { ASSERT_INT_EQUALS(0, 1, "foo"); }

    // UINT/PTR/BYTE_HEX/HEX are the same backend, so just test that the format string doesn't break
    TEST_FAILURE(uinteq) { ASSERT_UINT_EQUALS(0, 1, "Foo"); }
    TEST_FAILURE(ptreq) { ASSERT_PTR_EQUALS("x", "y", "Foo"); }
    TEST_FAILURE(bytehex) { ASSERT_BYTE_HEX_EQUALS('a', 'b'); }
    TEST_FAILURE(hex) { ASSERT_HEX_EQUALS((uint64_t)-1, 0); }

    TEST_SUCCESS(streq) { ASSERT_STR_EQUALS((side_effect(), "x"), "x"); }
    TEST_SUCCESS(streq) {
        char str_x[2] = "x";

        ASSERT_STR_EQUALS("x", (side_effect(), str_x), "foo");
    }
    TEST_FAILURE(streq) { ASSERT_STR_EQUALS("x", "xy", "bar"); }
    TEST_FAILURE(streq) { ASSERT_STR_EQUALS("xy", "x"); }

    uint8_t bin1[] = {0, 1, 2};
    uint8_t bin2[] = {0, 1, 2};

    TEST_SUCCESS(bineq) {
        ASSERT_BIN_ARRAYS_EQUALS((side_effect(), bin1), 3, bin2, 3);
        side_effect_ctr = 0;
        ASSERT_BIN_ARRAYS_EQUALS(bin1, (side_effect(), 3), bin2, 3);
        side_effect_ctr = 0;
        ASSERT_BIN_ARRAYS_EQUALS(bin1, 3, (side_effect(), bin2), 3);
        side_effect_ctr = 0;
        ASSERT_BIN_ARRAYS_EQUALS(bin1, 3, bin2, (side_effect(), 3));
    }
    TEST_FAILURE(bineq_samesize) {
        uint8_t bin3[] = {0, 1, 3};
        ASSERT_BIN_ARRAYS_EQUALS(bin1, 3, bin3, 3, "foo");
    }
    TEST_FAILURE(bineq_diffsize) { ASSERT_BIN_ARRAYS_EQUALS(bin1, 3, bin2, 2); }
    TEST_FAILURE(bineq_diffsize) { ASSERT_BIN_ARRAYS_EQUALS(bin1, 2, bin2, 3); }
    TEST_SUCCESS(bineq_empty) { ASSERT_BIN_ARRAYS_EQUALS(bin1, 0, bin2, 0, "foo"); }
    TEST_SUCCESS(bineq_same) { ASSERT_BIN_ARRAYS_EQUALS(bin1, 3, bin1, 3); }

    return NO_MORE_TESTS;
}

void reset(void) {
    g_cur_testname = "UNKNOWN";
    g_cur_file = "UNKNOWN";
    g_bail_out = 0;

    if (g_test_filedes) {
        fclose(g_test_filedes);
    }

    g_test_filedes = fopen(g_test_filename, "w");
    if (!g_test_filedes) {
        perror("***INTERNAL ERROR*** Failed to open temporary file");
        abort();
    }

    side_effect_ctr = 0;
}

int check_failure_output(const char *expected) {
    fclose(g_test_filedes);
    g_test_filedes = NULL;

    FILE *readfd = fopen(g_test_filename, "r");

    static char tmpbuf[256];
    char *rv = fgets(tmpbuf, sizeof(tmpbuf), readfd);
    fclose(readfd);

    if (!expected) {
        return rv == NULL;
    }
    if (!rv) {
        return 0;
    }
    return !strncmp(tmpbuf, expected, strlen(expected));
}

int main(int argc, char **argv) {
    int index = 0;

    if (argc < 2) {
        return 1;
    }

    g_test_filename = argv[1];

    // Suppress unused function warnings
    (void)s_aws_run_test_case;

    // Sanity checks for our own test macros

    reset();
    if (test_asserts(&index) != BAILED_OUT) {
        fprintf(
            stderr,
            "***FAILURE*** Initial case did not succeed; stopped at %s:%d (%s)\n",
            g_cur_file,
            index,
            g_cur_testname);
        return 1;
    }

    index++;
    reset();
    if (test_asserts(&index) != FAILURE) {
        fprintf(
            stderr,
            "***FAILURE*** Second case did not fail; stopped at %s:%d (%s)\n",
            g_cur_file,
            index,
            g_cur_testname);
        return 1;
    }

    index = 0;
    for (;;) {
        reset();
        int rv = test_asserts(&index);
        if (rv == NO_MORE_TESTS) {
            break;
        }
        if (rv != g_expected_return) {
            fprintf(
                stderr,
                "***FAILURE*** Wrong result (%d expected, %d got) after %s:%d (%s)\n",
                g_expected_return,
                rv,
                g_cur_file,
                index,
                g_cur_testname);
            return 1;
        }
        if (g_expected_return == FAILURE) {
            if (!check_failure_output("***FAILURE*** ")) {
                fprintf(
                    stderr,
                    "***FAILURE*** Output did not start with ***FAILURE*** after %s:%d (%s)\n",
                    g_cur_file,
                    index,
                    g_cur_testname);
                return 1;
            }
        } else {
            if (!check_failure_output(NULL)) {
                fprintf(
                    stderr, "***FAILURE*** Output was not empty after %s:%d (%s)\n", g_cur_file, index, g_cur_testname);
                return 1;
            }
        }

        index++;
    }

    return 0;
}
