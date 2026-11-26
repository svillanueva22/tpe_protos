#include <check.h>
#include <stdlib.h>

#include "metrics.h"

START_TEST (test_init_is_zero) {
    metrics_init();
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.conn_total, 0);
    ck_assert_uint_eq(m.conn_current, 0);
    ck_assert_uint_eq(m.bytes_c2o + m.bytes_o2c, 0);
    ck_assert_uint_eq(m.auth_ok + m.auth_fail, 0);
    for (int i = 0; i <= METRICS_REP_MAX; i++) {
        ck_assert_uint_eq(m.rep[i], 0);
    }
}
END_TEST

START_TEST (test_connections) {
    metrics_init();
    metrics_conn_open();
    metrics_conn_open();
    metrics_conn_open();
    metrics_conn_close();
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.conn_total, 3);    // históricas: no bajan al cerrar
    ck_assert_uint_eq(m.conn_current, 2);
    ck_assert_uint_eq(m.conn_peak, 3);

    metrics_conn_close();
    metrics_conn_close();
    metrics_get(&m);
    ck_assert_uint_eq(m.conn_current, 0);
    ck_assert_uint_eq(m.conn_peak, 3);     // el pico se conserva
}
END_TEST

START_TEST (test_close_without_open_does_not_wrap) {
    metrics_init();
    metrics_conn_close();
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.conn_current, 0);
}
END_TEST

START_TEST (test_bytes) {
    metrics_init();
    metrics_bytes(METRICS_C2O, 100);
    metrics_bytes(METRICS_C2O, 20);
    metrics_bytes(METRICS_O2C, 5);
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.bytes_c2o, 120);
    ck_assert_uint_eq(m.bytes_o2c, 5);
}
END_TEST

START_TEST (test_rep) {
    metrics_init();
    metrics_rep(0x00);
    metrics_rep(0x00);
    metrics_rep(0x05);
    metrics_rep(METRICS_REP_MAX);
    metrics_rep(0x09);  // fuera de RFC 1928: se ignora, no escribe fuera del arreglo
    metrics_rep(0xff);
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.rep[0x00], 2);
    ck_assert_uint_eq(m.rep[0x05], 1);
    ck_assert_uint_eq(m.rep[METRICS_REP_MAX], 1);
    ck_assert_uint_eq(m.rep[0x01], 0);
}
END_TEST

START_TEST (test_auth) {
    metrics_init();
    metrics_auth_ok();
    metrics_auth_fail();
    metrics_auth_fail();
    struct metrics_snapshot m;
    metrics_get(&m);
    ck_assert_uint_eq(m.auth_ok, 1);
    ck_assert_uint_eq(m.auth_fail, 2);
}
END_TEST

static Suite *
metrics_suite(void) {
    Suite *s = suite_create("metrics");
    TCase *tc = tcase_create("metrics");
    tcase_add_test(tc, test_init_is_zero);
    tcase_add_test(tc, test_connections);
    tcase_add_test(tc, test_close_without_open_does_not_wrap);
    tcase_add_test(tc, test_bytes);
    tcase_add_test(tc, test_rep);
    tcase_add_test(tc, test_auth);
    suite_add_tcase(s, tc);
    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(metrics_suite());
    srunner_run_all(sr, CK_NORMAL);
    const int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
