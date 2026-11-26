#include <check.h>
#include <stdlib.h>

#include "config.h"

START_TEST (test_defaults) {
    config_init(true);
    const struct config *c = config_get();
    ck_assert_uint_eq(c->buffer_size, CONFIG_DEFAULT_BUFFER_SIZE);
    ck_assert_uint_eq(c->connect_timeout_s, CONFIG_DEFAULT_CONNECT_TIMEOUT_S);
    ck_assert_uint_eq(c->idle_timeout_s, CONFIG_DEFAULT_IDLE_TIMEOUT_S);
    ck_assert(c->sniffer_enabled);
}
END_TEST

START_TEST (test_sniffer_follows_command_line) {
    config_init(false);  // -N
    ck_assert(!config_get()->sniffer_enabled);
    config_set_sniffer(true);
    ck_assert(config_get()->sniffer_enabled);
}
END_TEST

START_TEST (test_buffer_size_range) {
    config_init(true);
    ck_assert(config_set_buffer_size(CONFIG_BUFFER_SIZE_MIN));
    ck_assert(config_set_buffer_size(CONFIG_BUFFER_SIZE_MAX));
    ck_assert(!config_set_buffer_size(CONFIG_BUFFER_SIZE_MIN - 1));
    ck_assert(!config_set_buffer_size(CONFIG_BUFFER_SIZE_MAX + 1));
    ck_assert(!config_set_buffer_size(0));
    // un valor rechazado no cambia lo que había
    ck_assert_uint_eq(config_get()->buffer_size, CONFIG_BUFFER_SIZE_MAX);
}
END_TEST

START_TEST (test_timeouts_range) {
    config_init(true);
    ck_assert(config_set_connect_timeout(30));
    ck_assert_uint_eq(config_get()->connect_timeout_s, 30);
    ck_assert(!config_set_connect_timeout(0));   // un connect sin tiempo no tiene sentido
    ck_assert(!config_set_connect_timeout(CONFIG_CONNECT_TIMEOUT_MAX_S + 1));
    ck_assert_uint_eq(config_get()->connect_timeout_s, 30);

    ck_assert(config_set_idle_timeout(0));       // 0 = sin timeout de inactividad
    ck_assert_uint_eq(config_get()->idle_timeout_s, 0);
    ck_assert(!config_set_idle_timeout(CONFIG_IDLE_TIMEOUT_MAX_S + 1));
    ck_assert_uint_eq(config_get()->idle_timeout_s, 0);
}
END_TEST

START_TEST (test_log_level) {
    config_init(true);
    ck_assert(config_set_log_level(ERROR));
    ck_assert_int_eq(config_get()->log_level, ERROR);
    ck_assert_int_eq(current_level, ERROR);  // cambia el nivel global del logger
    ck_assert(!config_set_log_level((LOG_LEVEL) 99));
    ck_assert_int_eq(current_level, ERROR);
}
END_TEST

static Suite *
config_suite(void) {
    Suite *s = suite_create("config");
    TCase *tc = tcase_create("config");
    tcase_add_test(tc, test_defaults);
    tcase_add_test(tc, test_sniffer_follows_command_line);
    tcase_add_test(tc, test_buffer_size_range);
    tcase_add_test(tc, test_timeouts_range);
    tcase_add_test(tc, test_log_level);
    suite_add_tcase(s, tc);
    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(config_suite());
    srunner_run_all(sr, CK_NORMAL);
    const int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
