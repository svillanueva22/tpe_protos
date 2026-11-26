#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <check.h>
#include <arpa/inet.h>

#include "request.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

static enum request_state
feed_one_by_one(struct request_parser *p, const uint8_t *msg, size_t n, bool *errored) {
    uint8_t raw[1];
    buffer b;
    enum request_state st = p->state;
    for(size_t i = 0; i < n && !request_is_done(st, errored); i++) {
        buffer_init(&b, N(raw), raw);
        buffer_write(&b, msg[i]);
        st = request_consume(&b, p, errored);
    }
    return st;
}

static enum request_state
parse(struct request *r, const uint8_t *msg, size_t n, bool *errored) {
    struct request_parser p = { .request = r };
    request_parser_init(&p);
    return feed_one_by_one(&p, msg, n, errored);
}

START_TEST (test_ipv4) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert_uint_eq(request_done, st);
    ck_assert(!errored);
    ck_assert_uint_eq(SOCKS_CMD_CONNECT, r.cmd);
    ck_assert_uint_eq(SOCKS_ATYP_IPV4, r.atyp);
    ck_assert_uint_eq(htonl(0x7F000001), r.dest.ipv4.s_addr);
    ck_assert_uint_eq(80, ntohs(r.dest_port));
}
END_TEST

START_TEST (test_ipv6) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x04,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
                            0x1F, 0x90 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert_uint_eq(request_done, st);
    ck_assert_uint_eq(SOCKS_ATYP_IPV6, r.atyp);
    ck_assert(0 == memcmp(&in6addr_loopback, &r.dest.ipv6, sizeof(r.dest.ipv6)));
    ck_assert_uint_eq(8080, ntohs(r.dest_port));
}
END_TEST

START_TEST (test_fqdn) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x03, 11,
                            'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm',
                            0x01, 0xBB };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert_uint_eq(request_done, st);
    ck_assert_uint_eq(SOCKS_ATYP_DOMAIN, r.atyp);
    ck_assert_str_eq("example.com", r.dest.fqdn);
    ck_assert_uint_eq(443, ntohs(r.dest_port));
}
END_TEST

START_TEST (test_fqdn_max_len) {
    uint8_t msg[5 + 255 + 2];
    memcpy(msg, (uint8_t[]){ 0x05, 0x01, 0x00, 0x03, 255 }, 5);
    memset(msg + 5, 'a', 255);
    msg[5 + 255]     = 0x00;
    msg[5 + 255 + 1] = 0x50;
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert_uint_eq(request_done, st);
    ck_assert_uint_eq(255, strlen(r.dest.fqdn));
}
END_TEST

START_TEST (test_incomplete) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert(!request_is_done(st, &errored));
}
END_TEST

START_TEST (test_bind_is_parsed) {
    const uint8_t msg[] = { 0x05, 0x02, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert_uint_eq(request_done, st);
    ck_assert_uint_eq(SOCKS_CMD_BIND, r.cmd);
}
END_TEST

START_TEST (test_bad_atyp) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x02, 127, 0, 0, 1, 0x00, 0x50 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(request_error_unsupported_atyp, st);
}
END_TEST

START_TEST (test_bad_version) {
    const uint8_t msg[] = { 0x04, 0x01, 0x00, 0x01 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(request_error_unsupported_version, st);
}
END_TEST

START_TEST (test_empty_fqdn) {
    const uint8_t msg[] = { 0x05, 0x01, 0x00, 0x03, 0x00, 0x00, 0x50 };
    struct request r;
    bool errored = false;

    const enum request_state st = parse(&r, msg, N(msg), &errored);
    ck_assert(errored);
    ck_assert_uint_eq(request_error_invalid_fqdn, st);
}
END_TEST

START_TEST (test_marshall_ipv4) {
    uint8_t raw[10];
    buffer b;
    buffer_init(&b, N(raw), raw);
    struct sockaddr_in in = {
        .sin_family = AF_INET,
        .sin_port   = htons(1234),
        .sin_addr   = { .s_addr = htonl(0x0A000001) },
    };

    ck_assert_int_eq(10, request_marshall(&b, SOCKS_REPLY_SUCCEEDED, (struct sockaddr *) &in));
    const uint8_t expected[] = { 0x05, 0x00, 0x00, 0x01, 10, 0, 0, 1, 0x04, 0xD2 };
    ck_assert(0 == memcmp(expected, raw, N(expected)));
}
END_TEST

START_TEST (test_marshall_ipv6) {
    uint8_t raw[22];
    buffer b;
    buffer_init(&b, N(raw), raw);
    struct sockaddr_in6 in6 = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(80),
        .sin6_addr   = in6addr_loopback,
    };

    ck_assert_int_eq(22, request_marshall(&b, SOCKS_REPLY_SUCCEEDED, (struct sockaddr *) &in6));
    ck_assert_uint_eq(SOCKS_ATYP_IPV6, raw[3]);
    ck_assert_uint_eq(1, raw[19]);
    ck_assert_uint_eq(80, raw[21]);
}
END_TEST

START_TEST (test_marshall_error) {
    uint8_t raw[10];
    buffer b;
    buffer_init(&b, N(raw), raw);

    ck_assert_int_eq(10, request_marshall(&b, SOCKS_REPLY_CONNECTION_REFUSED, NULL));
    const uint8_t expected[] = { 0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
    ck_assert(0 == memcmp(expected, raw, N(expected)));

    buffer_init(&b, 9, raw);
    ck_assert_int_eq(-1, request_marshall(&b, SOCKS_REPLY_GENERAL_FAILURE, NULL));
}
END_TEST

Suite *
suite(void) {
    Suite *s  = suite_create("request");
    TCase *tc = tcase_create("request");

    tcase_add_test(tc, test_ipv4);
    tcase_add_test(tc, test_ipv6);
    tcase_add_test(tc, test_fqdn);
    tcase_add_test(tc, test_fqdn_max_len);
    tcase_add_test(tc, test_incomplete);
    tcase_add_test(tc, test_bind_is_parsed);
    tcase_add_test(tc, test_bad_atyp);
    tcase_add_test(tc, test_bad_version);
    tcase_add_test(tc, test_empty_fqdn);
    tcase_add_test(tc, test_marshall_ipv4);
    tcase_add_test(tc, test_marshall_ipv6);
    tcase_add_test(tc, test_marshall_error);
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
