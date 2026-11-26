#include <stdlib.h>
#include <stdint.h>
#include <check.h>

#include "hello.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

static void
on_method(struct hello_parser *p, const uint8_t method) {
    uint8_t *selected = p->data;
    if(method == SOCKS_HELLO_USERNAME_PASSWORD) {
        *selected = method;
    }
}

static void
init(struct hello_parser *p, uint8_t *selected) {
    *selected                     = SOCKS_HELLO_NO_ACCEPTABLE_METHODS;
    p->data                       = selected;
    p->on_authentication_method   = on_method;
    hello_parser_init(p);
}

/** alimenta el parser de a un byte, como si cada uno llegara en un recv distinto */
static enum hello_state
feed_one_by_one(struct hello_parser *p, const uint8_t *msg, size_t n, bool *errored) {
    uint8_t raw[1];
    buffer b;
    enum hello_state st = p->state;
    for(size_t i = 0; i < n && !hello_is_done(st, errored); i++) {
        buffer_init(&b, N(raw), raw);
        buffer_write(&b, msg[i]);
        st = hello_consume(&b, p, errored);
    }
    return st;
}

START_TEST (test_userpass_one_by_one) {
    const uint8_t msg[] = { 0x05, 0x02, 0x00, 0x02 };
    struct hello_parser p;
    uint8_t selected;
    bool errored = false;
    init(&p, &selected);

    enum hello_state st = feed_one_by_one(&p, msg, N(msg) - 1, &errored);
    ck_assert(!hello_is_done(st, &errored));
    st = feed_one_by_one(&p, msg + N(msg) - 1, 1, &errored);
    ck_assert(hello_is_done(st, &errored));
    ck_assert(!errored);
    ck_assert_uint_eq(SOCKS_HELLO_USERNAME_PASSWORD, selected);
}
END_TEST

START_TEST (test_no_acceptable) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00 };
    struct hello_parser p;
    uint8_t selected;
    bool errored = false;
    init(&p, &selected);

    const enum hello_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(hello_is_done(st, &errored));
    ck_assert(!errored);
    ck_assert_uint_eq(SOCKS_HELLO_NO_ACCEPTABLE_METHODS, selected);
}
END_TEST

START_TEST (test_zero_methods) {
    const uint8_t msg[] = { 0x05, 0x00 };
    struct hello_parser p;
    uint8_t selected;
    bool errored = false;
    init(&p, &selected);

    const enum hello_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(hello_is_done(st, &errored));
    ck_assert(!errored);
    ck_assert_uint_eq(SOCKS_HELLO_NO_ACCEPTABLE_METHODS, selected);
}
END_TEST

START_TEST (test_bad_version) {
    const uint8_t msg[] = { 0x04, 0x01, 0x02 };
    struct hello_parser p;
    uint8_t selected;
    bool errored = false;
    init(&p, &selected);

    const enum hello_state st = feed_one_by_one(&p, msg, N(msg), &errored);
    ck_assert(hello_is_done(st, &errored));
    ck_assert(errored);
    ck_assert_uint_eq(hello_error_unsupported_version, st);
}
END_TEST

START_TEST (test_leaves_trailing_bytes) {
    uint8_t raw[] = { 0x05, 0x01, 0x02, 0x01, 0x04 };
    struct hello_parser p;
    uint8_t selected;
    bool errored = false;
    buffer b;
    init(&p, &selected);
    buffer_init(&b, N(raw), raw);
    buffer_write_adv(&b, N(raw));

    const enum hello_state st = hello_consume(&b, &p, &errored);
    ck_assert(hello_is_done(st, &errored));
    ck_assert_uint_eq(0x01, buffer_read(&b));
    ck_assert_uint_eq(0x04, buffer_read(&b));
    ck_assert(!buffer_can_read(&b));
}
END_TEST

START_TEST (test_marshall) {
    uint8_t raw[2];
    buffer b;
    buffer_init(&b, N(raw), raw);

    ck_assert_int_eq(2, hello_marshall(&b, SOCKS_HELLO_USERNAME_PASSWORD));
    ck_assert_uint_eq(0x05, buffer_read(&b));
    ck_assert_uint_eq(0x02, buffer_read(&b));

    uint8_t small[1];
    buffer_init(&b, N(small), small);
    ck_assert_int_eq(-1, hello_marshall(&b, 0xFF));
}
END_TEST

Suite *
suite(void) {
    Suite *s  = suite_create("hello");
    TCase *tc = tcase_create("hello");

    tcase_add_test(tc, test_userpass_one_by_one);
    tcase_add_test(tc, test_no_acceptable);
    tcase_add_test(tc, test_zero_methods);
    tcase_add_test(tc, test_bad_version);
    tcase_add_test(tc, test_leaves_trailing_bytes);
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
