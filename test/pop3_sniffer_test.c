#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "pop3_sniffer.h"

START_TEST (test_applies_only_for_port_110_and_when_enabled) {
    config_init(true);
    ck_assert(pop3_sniffer_applies(110));
    ck_assert(!pop3_sniffer_applies(80));
    ck_assert(!pop3_sniffer_applies(995));  // POP3S va cifrado: no se disecta

    config_set_sniffer(false);              // toggle de runtime o -N
    ck_assert(!pop3_sniffer_applies(110));

    config_init(false);
    ck_assert(!pop3_sniffer_applies(110));
}
END_TEST

START_TEST (test_init_copies_and_truncates) {
    struct pop3_sniffer s;
    pop3_sniffer_init(&s, "alice", "mail.example.com", 110);
    ck_assert(s.active);
    ck_assert_str_eq(s.proxy_user, "alice");
    ck_assert_str_eq(s.dest, "mail.example.com");
    ck_assert_uint_eq(s.dest_port, 110);

    char long_name[300];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    pop3_sniffer_init(&s, long_name, long_name, 110);
    ck_assert_uint_eq(strlen(s.proxy_user), sizeof(s.proxy_user) - 1);  // truncado, con '\0'
    ck_assert_uint_eq(strlen(s.dest), sizeof(s.dest) - 1);

    pop3_sniffer_init(&s, NULL, "h", 110);  // sin usuario de proxy
    ck_assert_str_eq(s.proxy_user, "");
}
END_TEST

START_TEST (test_feed_does_not_touch_the_data) {
    struct pop3_sniffer s;
    pop3_sniffer_init(&s, "alice", "h", 110);
    uint8_t data[] = "USER bob\r\nPASS 1234\r\n";
    uint8_t copy[sizeof(data)];
    memcpy(copy, data, sizeof(data));
    for (size_t i = 0; i + 1 < sizeof(data); i++) {  // de a un byte
        pop3_sniffer_feed(&s, data + i, 1, POP3_C2S);
    }
    pop3_sniffer_feed(&s, (const uint8_t *) "+OK\r\n", 5, POP3_S2C);
    ck_assert_int_eq(memcmp(data, copy, sizeof(data)), 0);  // el flujo queda igual
}
END_TEST

static Suite *
pop3_suite(void) {
    Suite *s = suite_create("pop3_sniffer");
    TCase *tc = tcase_create("pop3_sniffer");
    tcase_add_test(tc, test_applies_only_for_port_110_and_when_enabled);
    tcase_add_test(tc, test_init_copies_and_truncates);
    tcase_add_test(tc, test_feed_does_not_touch_the_data);
    suite_add_tcase(s, tc);
    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(pop3_suite());
    srunner_run_all(sr, CK_NORMAL);
    const int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
