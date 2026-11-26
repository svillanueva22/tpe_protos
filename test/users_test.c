#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "users.h"

#define CHECK_USER(u, p) users_check((u), strlen(u), (p), strlen(p))

START_TEST (test_add_and_check) {
    users_init();
    ck_assert_int_eq(users_add("alice", "secret"), USERS_OK);
    ck_assert(CHECK_USER("alice", "secret"));
    ck_assert(!CHECK_USER("alice", "Secret"));   // la contraseña distingue mayúsculas
    ck_assert(!CHECK_USER("alice", "secre"));    // prefijo
    ck_assert(!CHECK_USER("alice", "secretX"));  // más largo
    ck_assert(!CHECK_USER("bob", "secret"));     // usuario inexistente
    ck_assert(!CHECK_USER("alic", "secret"));    // prefijo del usuario
}
END_TEST

START_TEST (test_check_with_embedded_nul) {
    users_init();
    ck_assert_int_eq(users_add("alice", "secret"), USERS_OK);
    // "alice\0x" con ulen=7 no puede matchear al usuario "alice"
    ck_assert(!users_check("alice\0x", 7, "secret", 6));
    ck_assert(!users_check("alice", 5, "secret\0x", 8));
    ck_assert(users_check("alice", 5, "secret", 6));
}
END_TEST

START_TEST (test_check_rejects_bad_lengths) {
    users_init();
    ck_assert_int_eq(users_add("alice", "secret"), USERS_OK);
    ck_assert(!users_check("alice", 0, "secret", 6));
    ck_assert(!users_check("alice", 5, "secret", 0));
    ck_assert(!users_check("alice", USERS_CRED_MAX + 1, "secret", 6));
    ck_assert(!users_check(NULL, 5, "secret", 6));
    ck_assert(!users_check("alice", 5, NULL, 6));
}
END_TEST

START_TEST (test_add_errors) {
    users_init();
    ck_assert_int_eq(users_add("alice", "secret"), USERS_OK);
    ck_assert_int_eq(users_add("alice", "otra"), USERS_ERR_EXISTS);
    ck_assert(CHECK_USER("alice", "secret"));  // el intento fallido no pisó nada
    ck_assert_int_eq(users_add("", "x"), USERS_ERR_INVALID);
    ck_assert_int_eq(users_add("x", ""), USERS_ERR_INVALID);
    ck_assert_int_eq(users_add(NULL, "x"), USERS_ERR_INVALID);
    char big[USERS_CRED_MAX + 2];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    ck_assert_int_eq(users_add(big, "x"), USERS_ERR_INVALID);
    big[USERS_CRED_MAX] = '\0';  // exactamente 255: es válido
    ck_assert_int_eq(users_add(big, big), USERS_OK);
    ck_assert(users_check(big, USERS_CRED_MAX, big, USERS_CRED_MAX));
}
END_TEST

START_TEST (test_full) {
    users_init();
    char name[16];
    for (int i = 0; i < USERS_MAX; i++) {
        snprintf(name, sizeof(name), "u%d", i);
        ck_assert_int_eq(users_add(name, "p"), USERS_OK);
    }
    ck_assert_int_eq(users_add("uno_mas", "p"), USERS_ERR_FULL);
    ck_assert_int_eq(users_del("u0"), USERS_OK);
    ck_assert_int_eq(users_add("uno_mas", "p"), USERS_OK);  // se liberó un lugar
}
END_TEST

START_TEST (test_del_and_set_pass) {
    users_init();
    ck_assert_int_eq(users_add("alice", "a"), USERS_OK);
    ck_assert_int_eq(users_add("bob", "b"), USERS_OK);
    ck_assert_int_eq(users_set_pass("alice", "nueva"), USERS_OK);
    ck_assert(!CHECK_USER("alice", "a"));
    ck_assert(CHECK_USER("alice", "nueva"));
    ck_assert_int_eq(users_set_pass("nadie", "x"), USERS_ERR_NOT_FOUND);
    ck_assert_int_eq(users_set_pass("alice", ""), USERS_ERR_INVALID);

    ck_assert_int_eq(users_del("alice"), USERS_OK);
    ck_assert(!CHECK_USER("alice", "nueva"));
    ck_assert(CHECK_USER("bob", "b"));  // el resto sigue intacto
    ck_assert_int_eq(users_del("alice"), USERS_ERR_NOT_FOUND);
    ck_assert_int_eq(users_count(), 1);
}
END_TEST

START_TEST (test_list) {
    users_init();
    ck_assert_int_eq(users_count(), 0);
    ck_assert_ptr_eq(users_name_at(0), NULL);
    users_add("alice", "a");
    users_add("bob", "b");
    users_add("carol", "c");
    ck_assert_int_eq(users_count(), 3);
    ck_assert_str_eq(users_name_at(0), "alice");
    ck_assert_str_eq(users_name_at(2), "carol");
    ck_assert_ptr_eq(users_name_at(3), NULL);

    users_del("alice");  // la baja compacta: no quedan huecos
    ck_assert_int_eq(users_count(), 2);
    ck_assert_str_eq(users_name_at(0), "bob");
    ck_assert_str_eq(users_name_at(1), "carol");
    ck_assert_ptr_eq(users_name_at(2), NULL);
}
END_TEST

static Suite *
users_suite(void) {
    Suite *s = suite_create("users");
    TCase *tc = tcase_create("users");
    tcase_add_test(tc, test_add_and_check);
    tcase_add_test(tc, test_check_with_embedded_nul);
    tcase_add_test(tc, test_check_rejects_bad_lengths);
    tcase_add_test(tc, test_add_errors);
    tcase_add_test(tc, test_full);
    tcase_add_test(tc, test_del_and_set_pass);
    tcase_add_test(tc, test_list);
    suite_add_tcase(s, tc);
    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(users_suite());
    srunner_run_all(sr, CK_NORMAL);
    const int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
