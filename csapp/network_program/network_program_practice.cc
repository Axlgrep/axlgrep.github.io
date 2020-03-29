/*
 * 第六章:网络编程
 *
 * 摘要: 英特网定义了域名集合和IP地址集合之间的映射, 这个映射是通过分布世界范围内的数据库(称为
 *       DNS(Domain Name System, 域名系统))来维护的
 *
 *       一个IP地址就是一个32位无符号整数, 网络程序将IP地址存放在如下的IP地址结构中
 *       // Internet address structure
 *       struct in_addr {
 *         unsigned int s_addr;         // Network byte order (big-endian)
 *       }
 *
 *       因特网主机可以有不同的主机字节顺序, TCP/IP为任意整数数据项定义了统一的网络字节顺序
 *       (network byte order)(大端字节顺序), 例如IP地址, 它放在包头中跨过网络被携带,在IP地址
 *       结构(in_addr)中存放的地址总是以(大端法)网络字节顺序存放的, 即使主机字节顺序(host byte order)
 *       是小端法. Unix提供了下面这样的函数在网络和主机字节顺序间实现转换:
 *
 *       unsigned long int htonl(unsigned long int hostlong);
 *       unsigned short int htons(unsigned short int hostshort);
 *
 *       unsigned long int ntohl(unsigned long int netlong);
 *       unsigned short int ntohs(unsigned short int netshort);
 *
 *
 *       因特网使用inet_aton和inet_ntoa函数来实现IP地址和点分十进制串之间的转换:
 *       // 返回: 若成功则为1， 若出错则为0
 *       int inet_aton(const char *cp, struct in_addr *inp);
 *       // 返回: 指向点分十进制字符串的指针
 *       char *inet_ntoa(struct in_addr in);
 *
 *       DNS数据库由上百万的主机条目结构(host entry structure)组成, 其中每条定义了一组域名(一
 *       个官方名字和一组别名)和一组IP地址之间的映射, 因特网应用程序通过调用gethostbyname和
 *       gethostbyaddr函数，从DNS数据库中检索任意的主机条目
 *       // DNS host entry structure
 *       struct hostent {
 *         char *h_name;                // Official domain name of host
 *         char **h_aliases;            // Null-terminated array of domain names
 *         int  h_addrtype;             // Host address type (AF_INET)
 *         int  h_length;               // Length of an address, in bytes
 *         char **h_addr_list;          // Null-terminated array of in_addr structs
 *       }
 *
 *      // Generic socket address structure (for connect, bind, and accept)
 *       struct sockaddr {
 *         unsigned short sa_family;    // Protocol family
 *         char           sa_data[14];  // Address data
 *       }
 *       // Internet-style socket address structure
 *       struct sockaddr_in {
 *         unsigned short sin_family;   // Address family (always AF_INET)
 *         unsigned short sin_port;     // Port number in network byte order
 *         struct in_addr sin_addr;     // IP address in network byte order
 *         unsigned char sin_zero[8];   // Pad to sizeof(struct sockaddr)
 *       }
 *
 *       Client: socket -> connect
 *       Server: socket -> bind -> listen -> accept
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
