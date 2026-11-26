#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "mgmt_parse.h"

START_TEST(test_feed_byte_by_byte) {
    struct mgmt_line_buffer lb;
    mgmt_line_buffer_reset(&lb);
    const char *input = "AUTH admin secret\n";
    char out[MGMT_LINE_MAX];
    int result = 0;
    for (const char *p = input; *p != '\0'; p++) {
        result = mgmt_line_feed(&lb, *p, out, sizeof(out));
        if (*p != '\n') ck_assert_int_eq(result, 0);
    }
    ck_assert_int_eq(result, 1);
    ck_assert_str_eq(out, "AUTH admin secret");
}
END_TEST

START_TEST(test_feed_partial_no_newline) {
    struct mgmt_line_buffer lb;
    mgmt_line_buffer_reset(&lb);
    char out[MGMT_LINE_MAX];
    const char *input = "METRICS";
    for (const char *p = input; *p != '\0'; p++) {
        ck_assert_int_eq(mgmt_line_feed(&lb, *p, out, sizeof(out)), 0);
    }
}
END_TEST

START_TEST(test_feed_overflow) {
    struct mgmt_line_buffer lb;
    mgmt_line_buffer_reset(&lb);
    char out[MGMT_LINE_MAX];
    int result = 0;
    for (int i = 0; i < MGMT_LINE_MAX + 10; i++) {
        result = mgmt_line_feed(&lb, 'x', out, sizeof(out));
        if (result == -1) break;
    }
    ck_assert_int_eq(result, -1);
}
END_TEST

START_TEST(test_parse_command_multiple_args) {
    char line[] = "USERS ADD pepe 1234";
    struct mgmt_command cmd;
    int argc = mgmt_parse_command(line, &cmd);
    ck_assert_int_eq(argc, 4);
    ck_assert_str_eq(cmd.argv[0], "USERS");
    ck_assert_str_eq(cmd.argv[1], "ADD");
    ck_assert_str_eq(cmd.argv[2], "pepe");
    ck_assert_str_eq(cmd.argv[3], "1234");
}
END_TEST

START_TEST(test_parse_command_single_token) {
    char line[] = "METRICS";
    struct mgmt_command cmd;
    int argc = mgmt_parse_command(line, &cmd);
    ck_assert_int_eq(argc, 1);
    ck_assert_str_eq(cmd.argv[0], "METRICS");
}
END_TEST

Suite *mgmt_parse_suite(void) {
    Suite *s = suite_create("mgmt_parse");
    TCase *tc = tcase_create("core");
    tcase_add_test(tc, test_feed_byte_by_byte);
    tcase_add_test(tc, test_feed_partial_no_newline);
    tcase_add_test(tc, test_feed_overflow);
    tcase_add_test(tc, test_parse_command_multiple_args);
    tcase_add_test(tc, test_parse_command_single_token);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    SRunner *sr = srunner_create(mgmt_parse_suite());
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}