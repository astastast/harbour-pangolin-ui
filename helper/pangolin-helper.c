/*
 * pangolin-helper: setuid-root launcher for `pangolin up --attach`.
 *
 * Installed by the RPM as /usr/libexec/harbour-pangolin-ui/pangolin-helper,
 * mode 4755 root:root, and run by the systemd user unit
 * harbour-pangolin.service. It replaces `sudo -n` plus a sudoers rule
 * (same idea as harbour-tailscale, which makes its whole GUI setuid; here
 * only this small program is privileged).
 *
 * Restrictions, so the setuid bit only grants "bring the tunnel up":
 *  - the binary is fixed (/usr/bin/pangolin, root-owned, from the RPM);
 *  - only `up --attach [allow-listed flags]` is accepted; flags that take a
 *    path, open a listener or rename network interfaces are rejected;
 *  - the environment is cleared; SUDO_USER makes the CLI use the caller's
 *    ~/.config/pangolin (internal/config/config.go userHomeDir()), and
 *    PANGOLIN_CLI_LOG_FILE overrides `log_file` from that user-writable
 *    config, which root would otherwise mkdir/create/rotate;
 *  - the caller's config dir and files must not be symlinks.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PANGOLIN_BIN "/usr/bin/pangolin"
#define LOG_FILE "/run/harbour-pangolin-ui/client.log"
#define MAX_ARGS 64

/* Flags of `pangolin up` (cmd/up/client/client.go, CLI 0.19.0). */
static const char *const bool_flags[] = {
    "attach", "override-dns", "tunnel-dns", "holepunch", "prefer-local-routes",
    "exit-node-takes-precedence", "disable-relay", NULL
};
static const char *const value_flags[] = {
    "id", "secret", "org", "endpoint", "mtu", "netstack-dns", "log-level",
    "ping-interval", "ping-timeout", "upstream-dns", "match-domains",
    "exit-node", "exit-node-site-ids", "exit-node-resource-id", NULL
};
/* Not allowed: --tls-client-cert (path), --http-addr (root API listener),
 * --interface-name, --subnet-router, --silent (detached mode). */

static int in_list(const char *name, size_t len, const char *const *list)
{
    for (; *list; ++list)
        if (strlen(*list) == len && strncmp(*list, name, len) == 0)
            return 1;
    return 0;
}

static int die(int code, const char *msg, const char *arg)
{
    fprintf(stderr, "pangolin-helper: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    return code;
}

/* Refuses a path that is a symlink. A missing path is fine. */
static int not_symlink(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT;
    return !S_ISLNK(st.st_mode);
}

/* Drops the caller's groups, becomes uid/gid 0 and starts from an empty
 * environment with a fixed PATH. */
static int become_root(void)
{
    if (setgroups(0, NULL) != 0 || setgid(0) != 0 || setuid(0) != 0)
        return -1;
    umask(022);
    if (clearenv() != 0 || setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0)
        return -1;
    return 0;
}

static int run_up(int argc, char *argv[])
{
    char *args[MAX_ARGS + 2];
    int n = 0, attach = 0;

    if (argc > MAX_ARGS)
        return die(2, "too many arguments", NULL);

    args[n++] = PANGOLIN_BIN;
    args[n++] = "up";
    for (int i = 2; i < argc; ++i) {
        const char *a = argv[i];
        if (strncmp(a, "--", 2) != 0)
            return die(2, "unexpected argument", a);
        const char *name = a + 2;
        const char *eq = strchr(name, '=');
        const size_t len = eq ? (size_t)(eq - name) : strlen(name);
        if (in_list(name, len, bool_flags)) {
            if (len == 6 && strncmp(name, "attach", 6) == 0)
                attach = 1;
            args[n++] = argv[i];
        } else if (in_list(name, len, value_flags)) {
            args[n++] = argv[i];
            if (!eq) {
                if (++i >= argc)
                    return die(2, "missing value for", a);
                args[n++] = argv[i];
            }
        } else {
            return die(2, "flag not allowed", a);
        }
    }
    args[n] = NULL;
    if (!attach)
        return die(2, "--attach is required", NULL);

    /* Caller identity, taken from the real uid before elevating. */
    const struct passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_name || !pw->pw_dir)
        return die(2, "cannot look up the calling user", NULL);
    char user[256], dir[1024], path[1200];
    if (snprintf(user, sizeof user, "%s", pw->pw_name) >= (int)sizeof user
        || snprintf(dir, sizeof dir, "%s/.config/pangolin", pw->pw_dir) >= (int)sizeof dir)
        return die(2, "user name or home path too long", NULL);

    /* The CLI writes accounts.json there as root and follows symlinks. */
    static const char *const files[] = { "", "/accounts.json", "/config.json", NULL };
    for (const char *const *f = files; *f; ++f) {
        snprintf(path, sizeof path, "%s%s", dir, *f);
        if (!not_symlink(path))
            return die(2, "refusing symlink", path);
    }

    if (become_root() != 0)
        return die(2, "not running setuid root (check the helper's file mode)", NULL);
    mkdir("/run/harbour-pangolin-ui", 0755);

    if (setenv("HOME", "/root", 1) != 0
        || setenv("SUDO_USER", user, 1) != 0
        || setenv("PANGOLIN_CLI_LOG_FILE", LOG_FILE, 1) != 0
        || setenv("NO_COLOR", "1", 1) != 0
        || setenv("TERM", "dumb", 1) != 0)
        return die(2, "cannot set up the environment", NULL);

    execv(PANGOLIN_BIN, args);
    fprintf(stderr, "pangolin-helper: cannot run %s: %s\n", PANGOLIN_BIN, strerror(errno));
    return 127;
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "up") == 0)
        return run_up(argc, argv);
    return die(2, "usage: pangolin-helper up --attach [flags]", NULL);
}
