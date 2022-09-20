

#### FIO参数

| 参数     | 解释                                                         |
| -------- | ------------------------------------------------------------ |
| bs       | 每次请求的块大小. 取值包括4k、8k及16k等.                     |
| ioengine | IO引擎的选择, [sync, psync, libaio...], 默认值是sync表示同步阻塞I/O, libaio是Linux的native异步I/O |
| direct   | 定义是否使用direct I/O, 可选值如下: 值为0, 表示使用buffered I/O值为1, 表示使用direct I/O (不推荐使用默认值, 因为使用了系统buffer无法测试出磁盘真实性能) |
| rw       | 读写模式. 取值包括顺序读(read), 顺序写(write),随机读(randread), 随机写(randwrite), 混合随机读写(randrw), 和混合顺序读写(rw，readwrite) |
| runtime  | 指定测试时长，即 FIO 运行时长                                |
| name     | 制定当前压测命令的名称                                       |
| size     | I/O 测试的寻址空间                                           |
| iodepth  | 请求的 I/O 队列深度. 此处定义的队列深度是指每个线程的队列深度, 如果有多个线程测试, 意味着每个线程都是此处定义的队列深度. fio总的**I/O并发数**=iodepth * numjobs |
| numjobs  | 定义测试的并发线程数                                         |



##### 4K随机读测试

`fio -bs=4k -ioengine=libaio -direct=1 -rw=randread -runtime=60 -name=fio-randread --size=100G -filename=/data1/fio_test_file -numjobs=40`



##### 4K随机写测试

`fio -bs=4k -ioengine=libaio -direct=1 -rw=randwrite -runtime=60 -name=fio-randwrite --size=100G -filename=/data1/fio_test_file -numjobs=40`



查看几块盘做raid0

cat /proc/mdstat
