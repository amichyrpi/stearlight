#define _GNU_SOURCE
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>

extern char *crypt(const char *, const char *);

static int reserve_desktop(void) {
    const char *enabled = getenv("STEARLIGHT_DESKTOP_PAM_RESERVE");
    if (!enabled || strcmp(enabled, "1")) return 1;
    FILE *file = fopen("/home/stearlight/.local/state/stearlight-desktop/api-token", "r");
    char token[80];
    if (!file) return 0;
    int read = fgets(token, sizeof(token), file) != NULL;
    fclose(file);
    if (!read) return 0;
    token[strcspn(token, "\r\n")] = 0;
    if (strlen(token) != 64 || strspn(token, "0123456789abcdef") != 64) return 0;
    int connection = socket(AF_INET, SOCK_STREAM, 0);
    if (connection < 0) return 0;
    struct timeval timeout = {20, 0};
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = {.sin_family=AF_INET, .sin_port=htons(27771),
                                  .sin_addr={.s_addr=htonl(0x7f000001)}};
    int result = 0;
    if (!connect(connection, (struct sockaddr *)&address, sizeof(address))) {
        const char *body = "{\"operation\":\"remote-reserve\"}";
        char request[512], response[2048];
        int length = snprintf(request, sizeof(request),
            "POST / HTTP/1.0\r\nHost: 127.0.0.1\r\nAuthorization: Bearer %s\r\n"
            "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", token, strlen(body), body);
        if (send(connection, request, length, MSG_NOSIGNAL) == length) {
            size_t used = 0;
            ssize_t received;
            while (used < sizeof(response)-1 &&
                   (received=recv(connection, response+used, sizeof(response)-1-used, 0)) > 0) used += received;
            response[used] = 0;
            result = strstr(response, " 200 ") && strstr(response, "\"ok\": true");
        }
    }
    close(connection);
    return result;
}

// Android cannot run PAM's setuid shadow helper. The app owns this private
// rootfs and its shadow file; authenticate only its own Linux account here.
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pam, int flags, int argc,
                                  const char **argv) {
    (void)flags; (void)argc; (void)argv;
    const char *name = NULL, *password = NULL;
    int result = pam_get_user(pam, &name, NULL);
    if (result != PAM_SUCCESS) return result;
    if (!name || strcmp(name, "stearlight")) return PAM_AUTH_ERR;
    struct passwd *user = getpwnam(name);
    if (!user || user->pw_uid != geteuid()) return PAM_AUTH_ERR;
    result = pam_get_authtok(pam, PAM_AUTHTOK, &password, "Password: ");
    if (result != PAM_SUCCESS) return result;
    if (!password || !*password) return PAM_AUTH_ERR;
    struct spwd *shadow = getspnam(name);
    if (!shadow || !shadow->sp_pwdp || shadow->sp_pwdp[0] != '$') return PAM_AUTH_ERR;
    char *actual = crypt(password, shadow->sp_pwdp);
    if (!actual || strlen(actual) != strlen(shadow->sp_pwdp)) return PAM_AUTH_ERR;
    unsigned difference = 0;
    for (size_t index = 0; actual[index]; ++index)
        difference |= (unsigned char)actual[index] ^ (unsigned char)shadow->sp_pwdp[index];
    if (difference) return PAM_AUTH_ERR;
    return reserve_desktop() ? PAM_SUCCESS : PAM_SYSTEM_ERR;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pam, int flags, int argc,
                            const char **argv) {
    (void)pam; (void)flags; (void)argc; (void)argv;
    return PAM_SUCCESS;
}
