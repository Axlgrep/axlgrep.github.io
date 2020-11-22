/*
 * 第八章: 异常控制流
 *
 * 摘要:
 *
 *
 *
 *
 *
 *-----------------------------------------------------------------------------------------------
 * 练习题8.1:
 * 考虑三个具有下述起始和结束时间的进程:
 *
 * 进程           起始时间         结束时间
 * A               0               2
 * B               1               4
 * C               3               5
 *
 * 对于对进程, 指出它们是否是并发运行的:
 *
 * 进程对          并发的?
 * AB              Y
 * AC              N
 * BC              Y
 *-----------------------------------------------------------------------------------------------
 * 练习题8.2:
 * 考虑下面程序:
 *
 * #include "csapp.h"
 * int main() {
 *   int x = 1;
 *   if (Fork() == 0) {
 *     printf("printf1: x = %d\n", ++x);
 *   }
 *   printf("printf2: x = %d\n", --x);
 *   exit(0);
 * }
 *
 * A. 子进程的输出是什么?
 *    子进程输出:
 *        printf1: x = 2
 *        printf2: x = 1
 * B. 父进程的输出是什么?
 *        printf2: x = 0
 *-----------------------------------------------------------------------------------------------
 * 练习题8.3:
 * 列出下面程序所有可能的输出序列:
 * int main() {
 *  if (Fork() == 0) {
 *    printf("a");
 *  } else {
 *    printf("b");
 *    waitpid(-1, NULL, 0);
 *  }
 *  printf("c");
 *  exit(0);
 * }
 *
 * acbc
 * bacc
 * abcc
 *-----------------------------------------------------------------------------------------------
 * 练习题8.4:
 * 考虑下面程序:
 * int main() {
 *   int status;
 *   pid_t pid;
 *   printf("Hello\n");
 *   pid = Fork();
 *   printf("%d\n", !pid);
 *   if (pid != 0) {
 *     if (waitpid(-1, &status, 0) > 0) {
 *       if (WIFEXITED(status) != 0) {
 *         printf("%d\n", WEXITSTATUS(status));
 *       }
 *     }
 *   }
 *   printf("Bye\n");
 *   exit(2);
 * }
 * A. 这个程序会产生多少行输出
 *    6行
 *
 * B. 这些输出行的一种可能的顺序是什么?
 *    Hello
 *    1
 *    Bye
 *    0
 *    2
 *    Bye
 *-----------------------------------------------------------------------------------------------
 * 练习题8.5:
 * 编写一个sleep的包装函数, 叫做snooze, 带有下面的接口:
 * unsigned int snooze(unsigned int secs);
 * 除了snooze函数会打印出一条信息来描述进程实际休眠了多长时间外, 它和sleep函数的行为完全一样:
 * Slept for 4 of 5 secs.
 *
 * unsigned int snooze(unsigned int secs) {
 *   int ret = sleep(secs);
 *   printf("Slept for %d of %d secs.\n", secs - ret, secs);
 *   return ret;
 * }
 *-----------------------------------------------------------------------------------------------
 * 练习题8.6:
 * 编写一个叫做myecho的程序, 它打印出它的命令行参数和环境变量. 例如:
 * unix> ./myecho arg1 arg2
 * Command line arguments:
 *   argv[ 0]: myecho
 *   argv[ 1]: arg1
 *   argv[ 2]: arg2
 *
 * Environment variables:
 *   envp[ 0]: PWD=/usr0/droh/ics/code/ecf
 *   envp[ 1]: TERM=emacs
 *   ...
 *   envp[25]: USER=droh
 *   envp[26]: SHELL=/usr/local/bin/tcsh/
 *   envp[27]: HOME=/usr0/droh
 *
 * int main(int argc, char *argv[], char *envp[]) {
 *   int i;
 *   printf("Command line arguments:\n");
 *   for (i = 0; argv[i] != NULL; i++) {
 *     printf("   argv[%2d]: %s\n", i, argv[i]);
 *   }
 *   printf("\n");
 *   printf("Enviroment variables:\n");
 *   for (i = 0; envp[i] != NULL; i++) {
 *     printf("   envp[%2d]: %s\n", i, envp[i]);
 *   }
 *   return 0;
 * }
 *-----------------------------------------------------------------------------------------------
 * 练习题8.7:
 * 编写一个叫做snooze的程序, 有一个命令行参数, 用这个参数调用练习题8.5中的snooze函数, 然后终止. 编
 * 写程序, 使得用户可以通过在键盘上输入ctrl-c中断snooze函数. 比如:
 * unix> ./snooze 5
 * Slept for 3 of 5 secs.   User hits crtl-c after 3 seconds
 * unix>
 *
 * #include "../csapp.h"
 * // SIGINT handler
 * void handler(int sig) {
 *   return;  // Catch the signal and return
 * }
 *
 * unsigned int snooze(unsigned int secs) {
 *   unsigned int ret = sleep(secs);
 *   printf("Slept for %d of %d secs.\n", secs - ret, secs);
 *   return ret;
 * }
 *
 * int main(int argc, char *argv[]) {
 *   if (argc != 2) {
 *     fprintf(stderr, "usage: %s<secs>\n", argv[0]);
 *     exit(0);
 *   }
 *
 *   if (signal(SIGINT, handler) == SIG_ERR) { // Install SIGINT handler
 *     unix_error("signal error\n");
 *   }
 *   snooze(atoi(argv[1]));
 *   exit(0);
 * }
 */

