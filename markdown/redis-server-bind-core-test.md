###测试环境
* Disk：3T
* Cpu: Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz
* Memory:  320G DDR4
* Network Card: 10-Gigabit X540-AT2

1. 同一种场景压测三次取结果.
2. 避免redis-benchmark和redis-server抢占CPU，分别部署在同机房的两台不同机器上, 此外压测期间压力足够，保证redis-server的CPU负载100%.

### 独占物理核和独占虚拟核场景
---
* Set压测命令: `./redis-benchmark -h host -p port -t set -n 10000000 -c 200 -r 1000 -d 33`(小红书实例value基本为33 Bytes)

* 一个redis-server独占一个物理核(CPU负载100%)

```cpp
Summary:
  throughput summary: 178521.29 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.927     0.216     1.951     2.639     2.855     6.295
```

* 两个redis-server占用同一个物理核的两个逻辑核(同时压两个redis-server, 保证各自的逻辑核负载100%, 下面列出单个redis-server的压测结果)

```cpp
Summary:
  throughput summary: 115792.34 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        3.377     0.336     3.335     4.199     4.351     8.175
```
---

* Get压测命令: `./redis-benchmark -h host -p port -t get -n 10000000 -c 200 -r 1000`

* 一个redis-server独占一个物理核(CPU负载100%)

```cpp
Summary:
  throughput summary: 184866.21 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.767     0.280     1.775     2.511     2.727     6.391
```

* 两个redis-server占用同一个物理核的两个逻辑核(同时压两个redis-server, 保证各自的逻辑核负载100%, 下面列出单个redis-server的压测结果)

```cpp
Summary:
  throughput summary: 120866.37 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        3.212     0.504     3.191     3.967     4.111     6.439
```

##### 结论:
在redis-server高负载情况下，独占一个物理核相对于独占一个逻辑核(该逻辑核对应的物理核可能被其他逻辑核共享)性能提升明显, 根据压测结果显示在高负载场景下，Get/Set请求延迟，独占物理核请求平均延迟比独占逻辑核低43% ~ 45%, 同时QPS提升54%(绑定单个物理核单个redis-server QPS为Set 178521.29w/Get 184866.21w，分别绑定同一个物理核的两逻辑核的两个redis-server QPS和为Set 115792.34w/Get 115792.34w)

但是在多进程场景下逻辑核技术可以提升机器核整体的吞吐量(绑定单个物理核单个redis-server QPS为Set 178521.29w/Get 184866.21w, 分别绑定同一个物理核的两逻辑核的两个redis-server QPS总和为Set 231584.68w/Get 241732.74w)


### 独占物理核和在运行时在逻辑核之间漂移场景

---
* Set压测命令: `./redis-benchmark -h host -p port -t set -n 10000000 -c 200 -r 1000 -d 33`(小红书实例value基本为33 Bytes)

* 一个redis-server独占一个物理核(CPU负载100%)

```cpp
Summary:
  throughput summary: 179734.82 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.917     0.392     1.951     2.615     2.839     5.831
```

* 一个redis-server在运行期间每10us切换一个逻辑核

```cpp
Summary:
  throughput summary: 178004.85 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.987     0.264     1.991     2.735     3.055     6.911
```
---

* Get压测命令: `./redis-benchmark -h host -p port -t get -n 10000000 -c 200 -r 1000`

* 一个redis-server独占一个物理核(CPU负载100%)

```cpp
Summary:
  throughput summary: 188585.14 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.729     0.424     1.743     2.431     2.631     5.423
```

* 一个redis-server在运行期间每10us切换一个逻辑核

```cpp
Summary:
  throughput summary: 185069.23 requests per second
  latency summary (msec):
          avg       min       p50       p95       p99       max
        1.912     0.240     1.919     2.631     2.951     8.255
```

##### 结论：
在整机负载不高的场景下，独占一个物理核的redis-server和运行期间在不同逻辑核之间飘逸的redis-server性能差距不明显， 两者QPS基本持平，平均延迟以及最大延迟后者相对于前者要略微高一点.