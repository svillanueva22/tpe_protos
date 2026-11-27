#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "accesslog.h"

// `make test` corre desde la raíz del repo y build/ ya existe.
#define LOG_PATH "build/accesslog_test.log"

static struct sockaddr_in
addr_v4(const char *ip, const unsigned short port) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    return a;
}

static struct sockaddr_in6
addr_v6(const char *ip, const unsigned short port) {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof(a));
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(port);
    inet_pton(AF_INET6, ip, &a.sin6_addr);
    return a;
}

static struct sockaddr_in
client_v4(void) {
    return addr_v4("192.0.2.7", 51234);
}

// Lee la línea `n` (desde 0) del archivo de log; devuelve false si no existe.
static bool
read_line(const int n, char *buf, const size_t len) {
    FILE *f = fopen(LOG_PATH, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = false;
    for (int i = 0; i <= n; i++) {
        ok = fgets(buf, (int) len, f) != NULL;
        if (!ok) {
            break;
        }
    }
    fclose(f);
    return ok;
}

START_TEST (test_line_format) {
    remove(LOG_PATH);
    ck_assert(accesslog_init(LOG_PATH));
    const struct sockaddr_in c = client_v4();
    const struct sockaddr_in origin = addr_v4("93.184.216.34", 80);
    accesslog_write("alice", (const struct sockaddr *) &c, ACCESSLOG_FQDN, "example.com", 80,
                    (const struct sockaddr *) &origin, 0x00);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    // fecha ISO-8601 UTC: 2026-11-26T22:42:31Z
    ck_assert_int_eq(line[4], '-');
    ck_assert_int_eq(line[10], 'T');
    ck_assert_int_eq(line[19], 'Z');
    ck_assert_ptr_ne(strstr(line, " event=request "), NULL);
    ck_assert_ptr_ne(strstr(line, " user=alice "), NULL);
    ck_assert_ptr_ne(strstr(line, " client=192.0.2.7:51234 "), NULL);
    ck_assert_ptr_ne(strstr(line, " atyp=fqdn "), NULL);
    ck_assert_ptr_ne(strstr(line, " dest=example.com:80 "), NULL);
    // el nombre que pidió el cliente y la IP a la que realmente se conectó: se puede
    // hacer grep por cualquiera de los dos
    ck_assert_ptr_ne(strstr(line, " ip=93.184.216.34 "), NULL);
    ck_assert_ptr_ne(strstr(line, " rep=0x00\n"), NULL);
}
END_TEST

START_TEST (test_one_line_per_request_and_append) {
    remove(LOG_PATH);
    const struct sockaddr_in c = client_v4();
    const struct sockaddr_in o4 = addr_v4("198.51.100.1", 443);
    const struct sockaddr_in6 o6 = addr_v6("2001:db8::1", 22);
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_write("alice", (const struct sockaddr *) &c, ACCESSLOG_IPV4, "198.51.100.1", 443,
                    (const struct sockaddr *) &o4, 0x05);
    accesslog_close();
    // reabrir agrega al final, no pisa
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_write("bob", (const struct sockaddr *) &c, ACCESSLOG_IPV6, "2001:db8::1", 22,
                    (const struct sockaddr *) &o6, 0x04);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, "user=alice"), NULL);
    ck_assert_ptr_ne(strstr(line, "atyp=ipv4"), NULL);
    ck_assert_ptr_ne(strstr(line, "rep=0x05"), NULL);
    ck_assert(read_line(1, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, "user=bob"), NULL);
    ck_assert_ptr_ne(strstr(line, "atyp=ipv6"), NULL);
    ck_assert(!read_line(2, line, sizeof(line)));
}
END_TEST

START_TEST (test_ipv6_in_brackets) {
    remove(LOG_PATH);
    const struct sockaddr_in6 c6 = addr_v6("::1", 5000);
    const struct sockaddr_in6 o6 = addr_v6("2001:db8::1", 22);
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_write("alice", (const struct sockaddr *) &c6, ACCESSLOG_IPV6, "2001:db8::1", 22,
                    (const struct sockaddr *) &o6, 0x00);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    // sin corchetes "::1:5000" no se sabría dónde termina la IP y empieza el puerto
    ck_assert_ptr_ne(strstr(line, " client=[::1]:5000 "), NULL);
    ck_assert_ptr_ne(strstr(line, " dest=[2001:db8::1]:22 "), NULL);
    // ip= no lleva puerto, así que va sin corchetes
    ck_assert_ptr_ne(strstr(line, " ip=2001:db8::1 "), NULL);
}
END_TEST

START_TEST (test_no_ip_when_name_did_not_resolve) {
    remove(LOG_PATH);
    const struct sockaddr_in c = client_v4();
    ck_assert(accesslog_init(LOG_PATH));
    // REP 0x04 por getaddrinfo sin resultados: no hubo ninguna IP
    accesslog_write("alice", (const struct sockaddr *) &c, ACCESSLOG_FQDN, "no-existe.example", 80,
                    NULL, 0x04);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, " dest=no-existe.example:80 "), NULL);
    ck_assert_ptr_ne(strstr(line, " ip=- "), NULL);
    ck_assert_ptr_ne(strstr(line, " rep=0x04\n"), NULL);
}
END_TEST

START_TEST (test_flush_writes_the_buffer) {
    remove(LOG_PATH);
    const struct sockaddr_in c = client_v4();
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_write("alice", (const struct sockaddr *) &c, ACCESSLOG_FQDN, "a.com", 1, NULL, 0x00);
    accesslog_flush();  // lo que hace main una vez por vuelta del selector

    // el proceso sigue vivo y el archivo sigue abierto: la línea ya tiene que estar en disco
    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, "user=alice"), NULL);
    accesslog_close();
}
END_TEST

START_TEST (test_flush_without_init_is_noop) {
    accesslog_flush();
}
END_TEST

START_TEST (test_anonymous_and_sanitized) {
    remove(LOG_PATH);
    const struct sockaddr_in c = client_v4();
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_write(NULL, (const struct sockaddr *) &c, ACCESSLOG_FQDN, "a.com", 1, NULL, 0x01);
    // un usuario con espacio o salto de línea no puede partir la línea ni inyectar otra
    accesslog_write("mal user\nrep=0x00", (const struct sockaddr *) &c, ACCESSLOG_FQDN, "b.com", 1, NULL, 0x01);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, " user=- "), NULL);
    ck_assert(read_line(1, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, " user=mal_user_rep=0x00 "), NULL);
    ck_assert(!read_line(2, line, sizeof(line)));  // sigue siendo una sola línea
}
END_TEST

START_TEST (test_auth_fail_line) {
    remove(LOG_PATH);
    const struct sockaddr_in c = addr_v4("203.0.113.5", 40022);
    ck_assert(accesslog_init(LOG_PATH));
    accesslog_auth_fail((const struct sockaddr *) &c, "admin", 5);
    accesslog_close();

    char line[512];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_int_eq(line[19], 'Z');
    ck_assert_ptr_ne(strstr(line, " event=auth_fail "), NULL);
    ck_assert_ptr_ne(strstr(line, " user=admin "), NULL);
    ck_assert_ptr_ne(strstr(line, " client=203.0.113.5:40022\n"), NULL);
    // no es un request: no tiene destino ni REP
    ck_assert_ptr_eq(strstr(line, "dest="), NULL);
    ck_assert_ptr_eq(strstr(line, "rep="), NULL);
    ck_assert(!read_line(1, line, sizeof(line)));
}
END_TEST

START_TEST (test_auth_fail_user_is_sanitized_and_bounded) {
    remove(LOG_PATH);
    const struct sockaddr_in c = client_v4();
    ck_assert(accesslog_init(LOG_PATH));
    // bytes tal como llegan del cliente: con espacio, '\0' embebido y salto de línea
    accesslog_auth_fail((const struct sockaddr *) &c, "a b\0c\nevent=request", 19);
    accesslog_auth_fail((const struct sockaddr *) &c, NULL, 0);
    char big[300];
    memset(big, 'x', sizeof(big));
    accesslog_auth_fail((const struct sockaddr *) &c, big, sizeof(big));  // ULEN no confiable
    accesslog_close();

    char line[1024];
    ck_assert(read_line(0, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, " user=a_b_c_event=request "), NULL);  // no inyecta otro campo
    ck_assert(read_line(1, line, sizeof(line)));
    ck_assert_ptr_ne(strstr(line, " user=- "), NULL);
    ck_assert(read_line(2, line, sizeof(line)));
    ck_assert(strlen(line) < 400);               // el usuario no hace crecer la línea sin tope
    ck_assert(!read_line(3, line, sizeof(line)));  // y siguen siendo 3 líneas
}
END_TEST

START_TEST (test_auth_fail_without_init_is_noop) {
    const struct sockaddr_in c = client_v4();
    accesslog_auth_fail((const struct sockaddr *) &c, "x", 1);
}
END_TEST

START_TEST (test_write_without_init_is_noop) {
    const struct sockaddr_in c = client_v4();
    accesslog_write("alice", (const struct sockaddr *) &c, ACCESSLOG_FQDN, "a.com", 1, NULL, 0);
    accesslog_close();  // cerrar sin abrir tampoco rompe
}
END_TEST

START_TEST (test_init_fails_on_bad_path) {
    ck_assert(!accesslog_init("/no/existe/este/directorio/access.log"));
}
END_TEST

static Suite *
accesslog_suite(void) {
    Suite *s = suite_create("accesslog");
    TCase *tc = tcase_create("accesslog");
    tcase_add_test(tc, test_line_format);
    tcase_add_test(tc, test_one_line_per_request_and_append);
    tcase_add_test(tc, test_ipv6_in_brackets);
    tcase_add_test(tc, test_no_ip_when_name_did_not_resolve);
    tcase_add_test(tc, test_flush_writes_the_buffer);
    tcase_add_test(tc, test_flush_without_init_is_noop);
    tcase_add_test(tc, test_anonymous_and_sanitized);
    tcase_add_test(tc, test_auth_fail_line);
    tcase_add_test(tc, test_auth_fail_user_is_sanitized_and_bounded);
    tcase_add_test(tc, test_auth_fail_without_init_is_noop);
    tcase_add_test(tc, test_write_without_init_is_noop);
    tcase_add_test(tc, test_init_fails_on_bad_path);
    suite_add_tcase(s, tc);
    return s;
}

int
main(void) {
    SRunner *sr = srunner_create(accesslog_suite());
    srunner_run_all(sr, CK_NORMAL);
    const int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
