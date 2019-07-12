## **LevelDB写入与删除记录**

### 介绍
LevelDB作为一个数据存储引擎，它提供了Put，Delete和Get方法对数据库进行修改和查询操作， 实际上不管是Put操作还是Delete操作, LevelDB都会将其转换成一条记录，然后以顺序写的方式将这条记录追加到log文件的尾部，因为尽管这是一个磁盘的读写操作，但是文件的顺序追加写入效率是很高的，所以并不会导致写入速度降低，如果log文件写入数据成功，那么将这条记录插入到Memtable当中去，本篇文章会介绍LevelDB的一个删除或者写入操作是如何应用到DB当中.

### WriteBatch
执行Put/Delete操作首先是通过一个WriteBatch来表示的，从下面代码可以看到不管是执行Put还是Delete操作，实际上是在WriteBatch的rep_中修改以及追加一些信息，rep\_最开始8个字节用于存储Sequence，表示当前Writebatch中第一个操作所对应的序列号，接下来4个字节用户存储Count，表示当前WriteBatch有多少个Operation，然后紧接着就是count条Operation记录，每一个operation记录就是一个Put操作或者Delete操作.  

对于Put和Delete，Operation的表现形式不同，表示Put的Operation第一个字节存储kTypeValue，表示当前这个Operation记录的是一个写入操作，然后依次追加key的长度，key的内容以及value的长度和value的内容，表示要添加的key/value，表示Delete的Operation第一字解存储kTypeDeletion，表示当前这个Operation记录的是一个删除操作，然后依次追加key的长度，key的内容，表示要删除的key.

```cpp
/* 
 * ************* WriteBatch rep_ Format **************
 *  | <Sequence> | <Count> | <Operation 1> | <Operation 2> | ... |
 *     8 Bytes    4 Bytes
 *
 *
 *  ************** Delete Operation Format *************
 *  | <kTypeDeletion> |  <key size>  |  <key content> |
 *        1 Byte        1 ~ 5 Bytes    key.size Bytes
 *
 *
 *  ******************************** Put Operation Format ********************************
 *  | <kTypeValue> |  <key size>  |  <key content>  |  <value size>  |  < value content>  |
 *       1 Byte      1 ~ 5 Bytes    key.size Bytes     1 ~  5 Bytes     value.size Bytes
 */
 
void WriteBatch::Put(const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedSlice(&rep_, key);
  PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatch::Delete(const Slice& key) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeDeletion));
  PutLengthPrefixedSlice(&rep_, key);
}
```

### Writer
在真正的写入操作之前会将WriteBatch封装成一个Write对象, 然后扔到一个队列里面等待消费，我们先来看一下Write对象的定义.

```cpp
// Information kept for every waiting writer
struct DBImpl::Writer {
  Status status;      // 本次写入的结果
  WriteBatch* batch;  // 本次写入对应的WriteBatch
  bool sync;          // 本次写入数据对应的log是否立即刷盘
  bool done;          // 本次写入数据是否已经完成
  port::CondVar cv;   // 条件变量, 如果在此之前有其他Write正在写入，则等待

  explicit Writer(port::Mutex* mu) : cv(mu) { }
};
```

### DBImpl::Write()
DBimpl::Write()方法中的流程我们分三个阶段来看:

* 第一阶段，首先抢锁然后将封装好的Write对象push到队列尾部，如果自己不是对头元素或者自己还没有完成则一直阻塞住，等待通知唤醒.

```cpp
  Writer w(&mutex_);
  w.batch = my_batch;
  w.sync = options.sync;
  w.done = false;

  // 在这里首先会抢占锁，所以在很多线程进行写入的时候这里会进行互斥，
  // 保证每个Writer能够安全的放入writers_队列当中
  MutexLock l(&mutex_);
  writers_.push_back(&w);
  // 当前这个Writer并没有完成并且当前这个Writer并不是writers_
  // 队列的第一个(这说明之前还有Writer需要比它先完成)，则等待
  while (!w.done && &w != writers_.front()) {
    w.cv.Wait();
  }
  // 被唤醒之后发现自己已经被前面的WriteBatch打包一起完成了，
  // 则直接返回结果
  if (w.done) {
    return w.status;
  };
```

* 第二阶段， 由于数据首先是写入到Memtable当中，所以在写入之前我们先检查一下当前Memtable的状态，如果Memtable当前已经达到了write_to_buffer的上限，则及时将其转换成Immutable member, 在这个过程中也会检查Level 0层sst文件的数量，如果有必要会进行阻写或者缓写， 接下来从队列的第一个Write开始往后获取若干和Write(last_writer会指向我们当前获取的最后一个Write)， 将其中的WriteBatch打包成一个更大的WriteBatch，并且为这个大的WriteBatch设置Sequence(这个Sequence表示WriteBatch中第一个操作的序列号), 然后将这个WriteBatch中的内容写入到log当中，最后将WriteBatch的操作有序的插入到Memtable当中去并且更新last_sequence.

```cpp
  // May temporarily unlock and wait.
  // 在执行写入之前先检查一下Memtable, 是否还有空间
  Status status = MakeRoomForWrite(my_batch == NULL);
  uint64_t last_sequence = versions_->LastSequence();
  Writer* last_writer = &w;
  if (status.ok() && my_batch != NULL) {  // NULL batch is for compactions
    // 执行完BuildBatchGroup之后db_impl的成员变量tmp_batch_中存储writer_集合
    // 中已经被合并的WriteBatch，而last_writer指向writer_集合中最后一个被合并
    // 的WriteBatch
    WriteBatch* updates = BuildBatchGroup(&last_writer);
    // 为当前已经做了合并操作的WriteBatch设置sequence, 这个sequence
    // 对应于这个WriteBatch中第一个操作的序列号
    WriteBatchInternal::SetSequence(updates, last_sequence + 1);
    // 更新last_sequence, 当前合并的WriteBatch中有多少个操作则加上多少
    last_sequence += WriteBatchInternal::Count(updates);

    // Add to log and apply to memtable.  We can release the lock
    // during this phase since &w is currently responsible for logging
    // and protects against concurrent loggers and concurrent writes
    // into mem_.
    {
      mutex_.Unlock();
      status = log_->AddRecord(WriteBatchInternal::Contents(updates));
      bool sync_error = false;
      if (status.ok() && options.sync) {
        status = logfile_->Sync();
        if (!status.ok()) {
          sync_error = true;
        }
      }
      // 写log成功之后将整个WriteBatch中的所有操作依次插入到memtable当中
      if (status.ok()) {
        status = WriteBatchInternal::InsertInto(updates, mem_);
      }
      mutex_.Lock();
      if (sync_error) {
        // The state of the log file is indeterminate: the log record we
        // just added may or may not show up when the DB is re-opened.
        // So we force the DB into a mode where all future writes fail.
        RecordBackgroundError(status);
      }
    }
    if (updates == tmp_batch_) tmp_batch_->Clear();

    versions_->SetLastSequence(last_sequence);
  }
```

* 第三阶段， 最后一个阶段要做的事情就很简单了，经历了第二阶段，我们已经将队头的若干个Write消费完成了，并且last_writer已经指向了我们最后一个消费的Writer，这时候我们需要将队列中已经消费的Writer弹出，然后为其status,done这个两个成员变量赋值，并且调用Signal()唤醒(因为处理第一Writer，其他被消费的Writer还阻在其他线程当中), 最后如果队列非空，还要唤醒新的对头Writer, 开始下一波消费.


```cpp
  //      w                last_writer
  //   writer1 -> writer2 -> writer3 -> writer4 -> writer5 -> ...
  //    done       done       done         x          x
  //
  // 当当前队头Writer完成自身并且顺带完成了后续的几个Writer之后需要更新状态，
  // 如上图所示， 当前writer1完成时顺带将writer2, writer3完成, 这时候
  // writer2, writer3可能在其他线程里由于不是队头元素而被wait()住了，
  // 这时候我们需要将其status和done进行赋值并且pop掉，并且调用条件变
  // 量的Signal将其唤醒(第1219行), 唤醒之后发现其done成员变量为ture了
  // 这时候就会返回其对应的status了;
  while (true) {
    Writer* ready = writers_.front();
    writers_.pop_front();
    if (ready != &w) {
      ready->status = status;
      ready->done = true;
      ready->cv.Signal();
    }
    // last_writer是本队列中被消费的最后一个条目，也就是上图中的writer3
    if (ready == last_writer) break;
  }

  //
  //   writer4 -> writer5 -> ...
  //      x          x
  //
  // 如果队列不为空，调用唤醒对头元素, 也就是writer4
  // Notify new head of write queue
  if (!writers_.empty()) {
    writers_.front()->cv.Signal();
  }

  return status;
```

### DBImpl::MakeRoomForWrite()
在调用DBImpl::Write()将数据写入到Memtable之前我们会调用MakeRoomForWrite()， 看函数名是提供空间供数据写入，实际上这个函数里面还是做了很多事情，比如在Level 0层的sst文件数量到达软上限或者硬上限的时候分别执行缓写和阻写操作(因为Level0层的sst文件比较特殊，文件和文件之前可能有overlap, 如果文件过多，在迭代器遍历Level0层的时候会有读放大的问题，所以不能放任不管，有必要对其做一个限制)， 还有在Memtable大小已经大于write_to_buffer上限的时候如果能顺利将其转换成Immutable Memtable并且重新创建一个新的Memtable, 如果当前Immutable Memtable还不为空则可能当前正在做Compact， 这时我们就要等待其完成再做转换了.

```cpp
// REQUIRES: mutex_ is held
// REQUIRES: this thread is currently at the front of the writer queue
Status DBImpl::MakeRoomForWrite(bool force) {
  mutex_.AssertHeld();
  assert(!writers_.empty());
  bool allow_delay = !force;
  Status s;
  while (true) {
    if (!bg_error_.ok()) {
      // Yield previous error
      s = bg_error_;
      break;
    } else if (
        allow_delay &&
        versions_->NumLevelFiles(0) >= config::kL0_SlowdownWritesTrigger) {
      // We are getting close to hitting a hard limit on the number of
      // L0 files.  Rather than delaying a single write by several
      // seconds when we hit the hard limit, start delaying each
      // individual write by 1ms to reduce latency variance.  Also,
      // this delay hands over some CPU to the compaction thread in
      // case it is sharing the same core as the writer.
      // 如果当前level0层的sst文件已经到达了'软上限'，这时候我们要'缓写'
      // 所谓的缓写就是sleep 1s之后再写入，第一次allow_delay是true, sleep
      // 之后将allow_delay赋值为false, 下次就不sleep了
      mutex_.Unlock();
      env_->SleepForMicroseconds(1000);
      allow_delay = false;  // Do not delay a single write more than once
      mutex_.Lock();
    } else if (!force &&
               (mem_->ApproximateMemoryUsage() <= options_.write_buffer_size)) {
      // There is room in current memtable
      // 如果当前是写入操作， 并且memtable的内存使用量小于write_buffer_size
      // 直接break
      break;
    } else if (imm_ != NULL) {
      // We have filled up the current memtable, but the previous
      // one is still being compacted, so we wait.
      // 如果我们的memetable大小已经达到了write_buffer_size，但是immutable memtable
      // 目前不为空，表示可能在执行compact，这时候我们wait等待
      Log(options_.info_log, "Current memtable full; waiting...\n");
      bg_cv_.Wait();
    } else if (versions_->NumLevelFiles(0) >= config::kL0_StopWritesTrigger) {
      // There are too many level-0 files.
      // 如果我们的level 0层的sst文件已经到达了硬上限，这时候我们执行阻写操作
      Log(options_.info_log, "Too many L0 files; waiting...\n");
      bg_cv_.Wait();
    } else {
      // Attempt to switch to a new memtable and trigger compaction of old
      assert(versions_->PrevLogNumber() == 0);
      // 在执行compact之前先生成一个新的log文件, 如果还没有执行compact成功
      // 就把db关闭，那么下次启动db的时候，可以从旧的log文件中恢复数据
      uint64_t new_log_number = versions_->NewFileNumber();
      WritableFile* lfile = NULL;
      s = env_->NewWritableFile(LogFileName(dbname_, new_log_number), &lfile);
      if (!s.ok()) {
        // Avoid chewing through file number space in a tight loop.
        versions_->ReuseFileNumber(new_log_number);
        break;
      }
      delete log_;
      delete logfile_;
      logfile_ = lfile;
      logfile_number_ = new_log_number;
      log_ = new log::Writer(lfile);
      // 将原先的memtable 转化为immutable memtable(后续执行compaction就是
      // 使用的这个immutable memtable), 并且重新创建memtable
      imm_ = mem_;
      has_imm_.Release_Store(imm_);
      mem_ = new MemTable(internal_comparator_);
      mem_->Ref();
      force = false;   // Do not force another compaction if have room
      MaybeScheduleCompaction();
    }
  }
  return s;
}
```

### 总结
通过梳理数据写入流程我们会发现LevelDB每一个操作(不管是写入还是删除)都有一个序列号，这个序列号是递增的，序列号越大表示该操作越新，再深入学习LevelDB代码后会发现，其快照的概念也是基于这个序列号实现的.
