/*
 * 第六章:网络编程
 *
 * 摘要: 英特网定义了域名集合和IP地址集合之间的映射, 这个映射是通过分布世界范围内的数据库(称为
 *       DNS(Domain Name System, 域名系统))来维护的
 *
 *
 *-----------------------------------------------------------------------------------------------
 * 练习题11.1:
 *
 * 完成下表:
 *
 *     十六进制地址              点分十进制地址
 *        0x0                        (0.0.0.0)
 *        0xffffffff                 (255.255.255.255)
 *        0x7f000001                 (127.0.0.1)
 *        (0xcdbca079)               105.188.160.121
 *        (0x400c950d)               64.12.149.13
 *        (0xcdbc9217)               205.188.146.23
 *
 *
 *-----------------------------------------------------------------------------------------------
 * 练习题11.2:
 *
 * 编写程序hex2dd.c, 它将十六进制参数转换为点分十进制串并打印出结果:
 *
 * #include "../csapp.h"
 *
 * int main(int argc, char** argv) {
 *   struct in_addr inaddr;
 *   unsigned int addr;
 *
 *   if (argc != 2) {
 *     fprintf(stderr, "usage: %s <hex number>\n", argv[0]);
 *     exit(0);
 *   }
 *   sscanf(argv[1], "%x", &addr);
 *   inaddr.s_addr = htonl(addr);
 *   printf("%s\n", inet_ntoa(inaddr));
 *   exit(0);
 * }
 *
 *-----------------------------------------------------------------------------------------------
 * 练习题11.3:
 *
 * 编写程序dd2hex.c, 它将它的点分十进制参数转换成十六进制数并打印出结果:
 *
 * #include "../csapp.h"
 * int main(int argc, char** argv) {
 *   struct in_addr inaddr;
 *   unsigned int addr;
 *
 *   if (argc != 2) {
 *     fprintf(stderr, "usage: %s <dotted-decimal>\n", argv[0]);
 *     exit(0);
 *   }
 *   inet_aton(argv[1], &inaddr);
 *   addr = ntohl(inaddr.s_addr);
 *   printf("0x%x\n", addr);
 *   exit(0);
 * }
 *-----------------------------------------------------------------------------------------------
 * 练习题11.4:
 *
 * 编译图中11-12中的HOSTINFO程序, 然后再你的系统上连续运行hostinfo google.com三次, 在三个主机条目
 * 的顺序中, 你注意到了什么，这种顺序有何作用?
 *
 * 每次我们请求google.com的主机条目时, 相应的英特网地址列表以一种不同的，轮转的顺序返回, 返回地址
 * 的不同顺序称为DNS轮转(DNS round-robin), 它可以用来对一个大量使用的域名的请求做负载均衡
 *
 */
