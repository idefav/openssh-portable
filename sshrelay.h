/* Placed in the Public Domain. */

#ifndef SSHRELAY_H
#define SSHRELAY_H

#include <stdint.h>

int ssh_relay_connect(const char *, int, int, char **, int *);
int ssh_relay_loop(int, int, uint64_t *, uint64_t *);

#endif
