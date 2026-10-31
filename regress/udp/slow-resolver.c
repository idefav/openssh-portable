/* Test-only LD_PRELOAD fixture. The production build never links this file. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
int getaddrinfo(const char *host, const char *service,
    const struct addrinfo *hints, struct addrinfo **result)
{
    static int (*real_resolver)(const char *, const char *,
        const struct addrinfo *, struct addrinfo **);
    if (host != NULL && strcmp(host, "slow.test") == 0) {
        sleep(10);
        return EAI_AGAIN;
    }
    if (real_resolver == NULL)
        real_resolver = dlsym(RTLD_NEXT, "getaddrinfo");
    return real_resolver(host, service, hints, result);
}
