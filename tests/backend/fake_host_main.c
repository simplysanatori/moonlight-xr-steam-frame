/* Standalone fake host for the CLI test: prints "PORTS <http> <https>" then waits
 * for the PIN on stdin (one line) and keeps serving until stdin closes. GPLv3. */
#include "fake_host.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);   /* probes may hang up before the answer */
    fake_opts o = {.wait_for_pin = true};
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--gfe")) o.gfe = true;
    fake_host *h = fake_host_start(&o);
    if (!h) return 1;
    printf("PORTS %u %u\n", fake_host_http_port(h), fake_host_https_port(h));
    fflush(stdout);
    char line[64];
    if (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        fake_host_set_pin(h, line);
        printf("PIN set\n");
        fflush(stdout);
    }
    while (fgets(line, sizeof line, stdin)) {}   /* serve until the test closes our stdin */
    fake_host_stop(h);
    return 0;
}
