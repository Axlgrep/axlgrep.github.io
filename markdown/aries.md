## ARIES

#### A Transaction Recovery Method Supporting Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging

### 介绍
在这个章节, 首先我们会介绍一些关于恢复, 并发和缓存管理的基本概念, 然后我们会概述本篇论文的其余部分.

#### 1.1 日志, 失败和恢复方式
事务的概念已经耳熟能详了, 它封装了ACID(原子性, 一致性, 隔离性, 持久性)的特性, 事务概念的应用并不局限于数据库领域. 面对多个事务的并发执行和各种故障, 保证事务的原子性和持久性是非常重要的. 虽然过去人们提出了很多方法来解决这个问题. 但是这些方法的前提假设, 性能和复杂性并不是可接受的. 解决这个问题需要判断如下一些指标: 在页面内和跨页面支持的并发程度, 处理逻辑的复杂度, 对于数据和日志在非易失性存储和内存的空间开销, 在数据恢复和普通处理流程时对于同步/异步的IO操作数量的开销, 各种功能的支持(例如部分数据回滚), 在重启恢复期间的处理性能, 以及在重启恢复期间对并发的支持程度, 由于死锁和存储数据限制引发的事务回滚, 新颖的锁模式允许并发执行, 基于交换性和其他性质的操作，如不同事务对相同数据的增/减，等等。

在本篇论文中, 我们介绍一种新的恢复模式, 名为ARIES(`Algorithm for Recovery and Isolation Exploiting Semantics`), 它在所有这些指标上表现都很好, 它提供了极大的灵活性, 利用应用的某些特殊特性来获取更好的性能.

为了保证事务和数据恢复, ARIES将正在处理的事务以及可能造成数据的更改的操作记录在日志当中, 