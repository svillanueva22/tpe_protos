#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <check.h>

#include "auth.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

static enum auth_state
feed_one_by_one(struct auth_parser *p, const uint8_t *msg, size_t n, bool *errored) {
    uint8_t raw[1];
    buffer b;
    enum auth_state st = p->state;
    for(size_t i = 0; i < n && !auth_is_done(st, errored); i++) {
        buffer_init(&b, N(raw), raw);
        buffer_write(&b, msg[i]);
        st = auth_consume(&b, p, errored);
    }
    return st;
}

START_TEST (test_ok_one_by_one) {
    const uint8_t msg[] = { 0x01, 0x04, 'u', 's', 'e', 'r', 0x03, 'p', 'w', 'd' };
    struct auth_parser p;
    bool errored = false;
    auth_parser_init(&p);

    enum auth_state st = feed_one_by_one(&p, msg, N(msg) - 1, &errored);
    ck_assert(!auth_is_done(st, &errored));
    st = feed_one_by_one(&p, msg + N(msg) - 1, 1, &errored);
    ck_assert(auth_is_done(st, &errored));
    ck_assert(!errored);
    ck_assert_str_eq("user", p.username);
    ck_assert_str_eq("pwd",  p.password);

    auth_parser_close(&p);
    ck_assert_str_eq("", p.password);
}
END_TEST

START_TEST (test_max_lengths) {
    uint8_t msg[1 + 1 + 255 + 1 + 255];
    size_t i = 0;
    msg[i++] = 0x01;
    msg[i++] = 255;
    for(int j = 0; j < 255; j++) {
        msg[i++] = 'a';
    }
    msg[i++] = 255;
    for(int j = 0; j < 255; j++) {
        msg[i++] = 'b';
    }
    struct auth_parser p;
    bool errored = false;
    auth_parser_init(&p);

    const enum auth_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(auth_is_done(st, &errored));
    ck_assert(!errored);
    ck_assert_uint_eq(255, strlen(p.username));
    ck_assert_uint_eq(255, strlen(p.password));
}
END_TEST

START_TEST (test_bad_version) {
    const uint8_t msg[] = { 0x05, 0x01, 'u', 0x01, 'p' };
    struct auth_parser p;
    bool errored = false;
    auth_parser_init(&p);

    const enum auth_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(auth_error_unsupported_version, st);
}
END_TEST

START_TEST (test_empty_username) {
    const uint8_t msg[] = { 0x01, 0x00, 0x01, 'p' };
    struct auth_parser p;
    bool errored = false;
    auth_parser_init(&p);

    const enum auth_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(auth_error_invalid_length, st);
}
END_TEST

START_TEST (test_empty_password) {
    const uint8_t msg[] = { 0x01, 0x01, 'u', 0x00 };
    struct auth_parser p;
    bool errored = false;
    auth_parser_init(&p);

    const enum auth_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(auth_error_invalid_length, st);
}
END_TEST

START_TEST (test_marshall) {
    uint8_t raw[2];
    buffer b;
    buffer_init(&b, N(raw), raw);

    ck_assert_int_eq(2, auth_marshall(&b, AUTH_STATUS_FAIL));
    ck_assert_uint_eq(0x01, buffer_read(&b));
    ck_assert_uint_eq(0x01, buffer_read(&b));
}
END_TEST

Suite *
suite(void) {
    Suite *s  = suite_create("auth");
    TCase *tc = tcase_create("auth");

    tcase_add_test(tc, test_ok_one_by_one);
    tcase_add_test(tc, test_max_lengths);
    tcase_add_test(tc, test_bad_version);
    tcase_add_test(tc, test_empty_username);
    tcase_add_test(tc, test_empty_password);
    tcase_add_test(tc, test_marshall);
    suite_add_tcase(s, tc);

    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(suite());

    srunner_run_all(sr, CK_NORMAL);
    const int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
