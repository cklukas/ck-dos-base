#ifndef _CKCL_PROCESS_H
#define _CKCL_PROCESS_H

#define P_WAIT    0
#define P_NOWAIT  1
#define P_OVERLAY 2

int spawnl(int mode, const char *path, const char *arg0, ...);
int spawnlp(int mode, const char *file, const char *arg0, ...);
int spawnle(int mode, const char *path, const char *arg0, ...);
int spawnv(int mode, const char *path, const char *const *argv);
int spawnvp(int mode, const char *file, const char *const *argv);
int spawnve(int mode, const char *path, const char *const *argv, const char *const *envp);
int system(const char *command);
void exit(int status);
void _exit(int status);
void abort(void);
int getpid(void);

#endif
