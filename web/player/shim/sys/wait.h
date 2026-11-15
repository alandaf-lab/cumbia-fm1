/* (el navegador no tiene procesos: lo que usa hostsim.c para sus pruebas, sin efecto) */
#pragma once
#include <sys/types.h>
static inline pid_t fork(void) { return -1; }
static inline pid_t waitpid(pid_t p, int *s, int o) { (void)p; (void)o; if (s) *s = 0; return -1; }
static inline int pipe(int f[2]) { f[0] = f[1] = -1; return -1; }
#define WIFEXITED(s) 1
#define WEXITSTATUS(s) 0
#define WIFSIGNALED(s) 0
#define WTERMSIG(s) 0
