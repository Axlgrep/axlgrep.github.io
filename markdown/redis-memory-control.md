## 滥用Lua导致Redis内存无法被限制
### 问题描述
最近发现线上某个Redis实例内存占用达到了17.21G, 但是该实例中实际的用户数据并不是很多(大概200Mb的样子), 此外`mem_fragmentation_ratio`达到了5.10, 也侧面印证了有大比例的内存并没有用于存储用户数据(`server.cron_malloc_stats.process_rss/server.cron_malloc_stats.zmalloc_used`, 前者是从系统中获取的Redis进程的常驻内存(Resident Set Size), 后者是Redis层面通过调用zmalloc等函数分配的内存总量), 还是十分诡异的.

### Redis内存占用介绍
我们知道, Redis的内存占用主要包括自身内存 + 对象内存 + 缓冲内存 + 内存碎片

* 自身内存占用包括Redis进程初始化时创建的一些共享对象, 以及为事件驱动创建的aeEventLoop, 还有为保证服务正常运行所创建的一些数据结构.
* 对象内存理论上应该是占Redis总内存的最大一块, 里面存储着用户的所有数据, 而用户数据的表现形式是RedisObject, 这个在之前的博客[Redis中的对象](https://axlgrep.github.io/tech/redis-object.html)中有介绍过.
* 缓冲内存包括了客户端连接的缓冲区, 复制积压缓冲区, 还有AOF缓冲区等等
* 内存碎片实际上就是在分配内存时需要考虑边界对齐所额外分配的内存, 以及由于释放了某些内存块但是又不能被分配器重新使用而造成的消耗

下面引用CSAPP中虚拟存储器章节的一个段落对碎片进行介绍:

>造成堆利用率很低的主要原因是一种称为碎片(fragmentation)的现象, 当虽然有未使用的存储器但不能用来满足分配请求时, 就会发生这种现象, 有两种形式的碎片: 内部碎片(internal fragmentation)和外部碎片(external fragmentation).

>* 内部碎片是在一个已分配块比有效荷载大时发生的. 很多原因都可能造成这个问题. 例如, 一个分配器的实现可能对已分配块强加一个最小的最大值, 而这个大小要比某个请求的有效载荷大.
>* 外部碎片是当空闲存储器合计起来足够满足一个分配请求, 但是没有一个单独的空闲块足够大可以来处理这个请求时发生的, 例如现在有四个分散的, 大小都为2 Bytes的空闲块, 而现在有一个请求要求8 Bytes, 空闲块的总体积确实是8 Bytes, 但是由于它们并不连续, 所以不能满足请求. 

>外部碎片比内部碎片的量化要困难得多, 因为它不仅仅取决于以前请求的模式和分配器的实现方式, 还取决于将来请求的模式. 例如, 假设在k个请求之后, 所有空闲块的大小都恰好是4个字, 这个堆会有外部碎片吗? 答案取决于将来的请求模式. 如果将来所有的分配请求都要求小于或者等于4个字的块, 那么就不会有外部碎片. 另一方面, 如果有一个或者多个请求要求比4个字大的块, 那么这个堆就会有外部碎片.

### 分析
先从`info memory`中看几项关键的指标：

```cpp
used_memory_human:3.38G
used_memory_rss_human:17.21G
used_memory_dataset:428029006
used_memory_lua_human:8.75G
used_memory_scripts_human:2.98G
number_of_cached_scripts:9142591
mem_fragmentation_ratio:5.09
```

`used_memory_human`是Redis层面调用zmalloc等函数所分配的内存总量, 也就是`zmalloc_used_memory()`, 占用内存空间3.38G, 而`used_memory_rss_human`是从进程层面来看, Redis进程的常驻内存达到了17.21G.

`used_memory_dataset`在Redis里是通过`zmalloc_used_memory()`减去各种缓冲以及各个字典元数据还有一些和用户数据无关的其他数据结构所占用的内存得来的, Redis把它简单看成用户数据集的大小(这里实际上是不准确的, 后面会说), 占用大概408MB的样子.

`used_memory_lua_human`是在Redis的Lua三方库中分配的内存, 由于其内部走的是malloc()/free()的形式控制内存, 所以并没有统计在`used_memory`当中, 占用达到了8.75G.

我们知道, Redis支持两种方式调用Lua脚本, 一种是通过`EVAL script numkeys key [key ...] arg [arg ...]`在命令中直接将Lua脚本当做参数专递给Redis执行

但是由于考虑到Lua脚本本身可能体积较大, 如果每次调用同一个Lua脚本都要重新将该脚本原封不动的传递给Redis一次, 不仅给网络带宽带来了一定的开销, 也会影响Redis的性能, Redis支持另外一种使用Lua的方法, 先调用`SCRIPT LOAD script`将Lua脚本加载到Redis服务内部, 并且会返回给客户端一个跟该Lua向关联的Sha1码, 下次调用该Lua脚本的时候, 只需通过`EVALSHA sha1 numkeys key [key ...] arg [arg ...]`命令, 将Sha1码当做参数进行传递即可.

我们使用EVALSHA命令直接通过sha1调用相应Lua脚本的前提是我们必须将Lua脚本缓存在Redis服务内部, Redis使用`f-sha1`作为键, Lua脚本作为值, 将其存放在`server.lua_scripts`字典内部, 方便客户直接使用sha1进行查找,  下面是将一个描述Lua脚本的键值对添加到`server.lua_scripts`的代码片段.

```cpp
    /* We also save a SHA1 -> Original script map in a dictionary
     * so that we can replicate / write in the AOF all the
     * EVALSHA commands as EVAL using the original script. */
    int retval = dictAdd(server.lua_scripts,sha,body);
    serverAssertWithInfo(c ? c : server.lua_client,NULL,retval == DICT_OK);
    server.lua_scripts_mem += sdsZmallocSize(sha) + getStringObjectSdsUsedMemory(body);
```

`used_memory_scripts_human`实际上就是Redis缓存Lua脚本所占用的内存, 达到了2.98G, 需要注意的是, 由于缓存Lua脚本创建对象都是调用的Redis层面的ZMalloc等函数, 所以这部分的内存消耗实际上是包含在`used_memory`内部的.

`number_of_cached_scripts`就很好理解了, 就是Redis中缓存Lua脚本的个数, 实际上就是`server.lua_scripts`的大小, 可以看出来, 当前我们一共缓存了9142591个Lua脚本.

`mem_fragmentation_ratio`是将(Redis进程常驻内存/Redis通过Zmalloc等函数分配内存)得到的一个比值(`process_rss/zmalloc_used`), 作者应该是想通过它表示用户实际数据占用内存相对于进程常驻内存的一个占比, 但是自己觉得不是特别准确.

### 分析
实际上通过上面一些指标的分析, Redis用户数据少, 但是占用内存高的问题基本上已经有了一个结论了, 实际上大多数内存都是被Lua占用掉了,  而Lua占用内存又细分为两类, 一类是Redis层面为了缓存Lua脚本, 将其存放在`server.lua_scripts`字典中所占用的内存, 这部分大概是2.98GB(还有一些其他的数据结构), 另外一类是底层Lua heap所占用的内存, 达到了8.75G.

此时我们发现了两个问题:

Redis层面缓存Lua脚本(`used_memory_scripts_human`)占用的内存虽然是通过ZMalloc进行分配的, 但是由于可以触发缓存Lua脚本的`EVAL`命令和`SCRIPT LOAD`命令并没有带上`use-memory`的falgs, 在执行这两个命令之前并不会对内存进行判断, 所以并不能受到`maxmemory`限制, 此外底层Lua heap占用的内存(`used_memory_lua_human`)是通过系统的malloc()/free()进行分配的, 也不受到`maxmemory`的控制. 换句话说, 如果用户滥用Lua脚本, 可能会造成Redis的内存无法限制的问题.

Redis的常驻内存达到了17.21G, 但是我们已知的内存占用只有Redis层面通过ZMalloc等函数分配的3.38G(`used_memory_human`)和底层Lua库通过Malloc()函数分配的8.75G(`used_memory_lua_human`), 还差了`17.21 - (3.38 + 8.75) = 5.08GB`,  实际上这些多余的内存占用就是由于碎片造成的, 这里的碎片包含了内部碎片和外部碎片, 下面分别分析:

#### 内部碎片
>  CPU一次性能读取数据的二进制位数称为字长，也就是我们通常所说的32位系统（字长4个字节）、64位系统（字长8个字节）的由来。所谓的8字节对齐，就是指变量的起始地址是8的倍数。比如程序运行时（CPU）在读取long型数据的时候，只需要一个总线周期，时间更短，如果不是8字节对齐的则需要两个总线周期才能读完数据。

下面是Redis调用ZMalloc函数分配完内存的统计函数, 可以看到Redis在更新内存使用的时候是有考虑到字节对齐的,  所以`used_memory_human`的值是相对可信的, 已经包含了内部碎片所占用的内存空间.

```cpp
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    atomicIncr(used_memory,__n); \
} while(0)
```

反之我们再看一下Lua内存分配之后的统计函数, 直接是将应用层分配的字节数做了一个累加, 并没有考虑由于字节对齐所占用的额外空间, 所以`used_memory_lua_human`的值并不能真实的反应出底层Lua heap实际占用的内存空间, 如果是分配一整个大块内存还好说, 如果是分配很多的小内存块, 那么内部碎片所占的比例将会是非常高的.

```cpp
/*
** generic allocation routine.
*/
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  global_State *g = G(L);
  lua_assert((osize == 0) == (block == NULL));
  block = (*g->frealloc)(g->ud, block, osize, nsize);
  if (block == NULL && nsize > 0)
    luaD_throw(L, LUA_ERRMEM);
  lua_assert((nsize == 0) == (block == NULL));
  g->totalbytes = (g->totalbytes - osize) + nsize;
  return block;
}
```

#### 外部碎片
外部碎片跟请求的模式和分配器的实现方式有关系, 我们无法控制用户的请求模式, 只能从分配器上做文章, Redis层面分配内存, 我们是使用`JeMalloc`进行分配的,  `JeMalloc`相对于`Libc`原生的内存分配器优势在于多线程情况下的高性能以及内存碎片的减少, 于是我们修改了底层Lua库的源码, 让它也用Redis层面的ZMalloc进行内存的分配(使用`JeMalloc`), 下面贴出了更换内存分配器之前和之后, 加载相同RDB, Redis内存的使用情况.

底层Lua使用`Libc`进行内存分配(调用Malloc函数)

```cpp
used_memory_human:3.38G
used_memory_rss_human:17.21G
used_memory_dataset:428045568
used_memory_lua_human:8.75G
used_memory_scripts_human:2.98G
number_of_cached_scripts:9142591
```

底层Lua使用`JeMalloc`进行内存分配(调用Redis层面的的ZMalloc函数)

```cpp
used_memory_human:12.52G
used_memory_rss_human:13.36G
used_memory_dataset:849460936
used_memory_lua_human:8.75G
used_memory_scripts_human:2.98G
number_of_cached_scripts:9142591
```

加载的这个RDB文件, 大部分都是Lua脚本, 用户数据量非常少, 所以将底层Lua库的内存分配器更改为`JeMalloc`之后, 效果十分明显, Redis进程的常驻内存从之前的17.21GB下降到了13.36G, 内存碎片率得到了有效的控制.

此外由于底层Lua库也是通过Redis层面的ZMalloc进行内存的分配, 所以底层Lua heap占用的内存也被统计到了`zmalloc_used_memory()`里面, 这会受到Redis的`maxmemory`配置项控制, 使得用户在滥用Lua的场景下也可以有效的控制Redis占用内存的上限.

这时候我们发现了另外一个问题, 同一份RDB文件, 用户数据量肯定是一样的, 为什么改内存分配器之前`used_memory_dataset`占用408MB, 但是改完之后变成了810MB? 

实际上前面有提到过`used_memory_dataset`是通过`zmalloc_used_memory()`减去各种缓冲以及各个字典元数据还有一些和用户数据无关的其他数据结构所占用的内存得来的, 但是我们调用ZMalloc分配内存的时候, 实际上是有可能产生内部碎片的, `zmalloc_used_memory()`包含了我们在申请内存时产生的所有内部碎片, 由于修改代码之后, 9142591个Lua脚本所需的对象都是通过ZMalloc进行分配了, 可想而知产生了多少内部碎片,  带来了`used_memory_dataset`数值的上涨.  这也是前面提到的`used_memory_dataset`并不能准确反映用户实际数据占用内存的原因, 因为里面可能会包含大量的跟用户实际数据无关的内部碎片.

### 结论
目前Redis层面缓存Lua脚本的`server.lua_scripts`字典和底层Lua heap占用的内存都不受到`maxmemory`配置项的限制, 这实际上是十分危险的, 尤其是对于云厂商, 线上机器部署Redis实例会参考每个进程的`maxmemory`, 但是如果用户滥用Lua, 可能会使单个Redis进程的内存不受`maxmemory`的控制从而持续走高, 最终导致耗尽整机内存, 引发整机故障. 此外, 经过测试对比发现`Libc`在对内存碎片的控制上确实不是特别理想, 不能对内存空间进行高效的利用, 浪费了内存资源. 针对以上两点, 自己已经向社区提交了[PR](https://github.com/redis/redis/pull/7814), 希望Redis社区能够早日采纳.

