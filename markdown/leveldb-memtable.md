## **LevelDB中的Memtable**

### 介绍

LevelDB中的数据是存储在磁盘上的，采用了[LSM-Tree](http://ov6v82oa9.bkt.clouddn.com/download.pdf)实现，LSM-Tree将磁盘的随机写转换成顺序写，大大提高了写入速度(但是正因为如此LevelDB随机读的性能一般，也就是说LevelDB适用于查询较少而写较多的场景)，LSM-Tree将索引树结构拆分成一大一小两颗树， 较小的一颗常驻在内存当中， 较大的一颗持久化到磁盘，在内存树大小到达一定的上限之后会和磁盘树发生归并操作，而归本操作本身也是顺序写的过程，本篇将会介绍常驻在内存中的树，在LevelDB中也就是Memtable.

### Comparator

在介绍里已经提到过了LevelDB中的数据都是有序存放的， 那么想要知道数据是按照什么规则进行排序的， 我们需要先了解一下LevelDB中三种比较器， 下面贴出每种比较器所对应的Key的格式以及比较器的具体流程.

* UserKeyComparator: 这个比较器是对用户自己的Key进行排序的，用户可以自己指定这个Compacter，让这个比较器按照用户自己定义的规则进行排序，如果没有指定，默认是BytewiseComparator(), 比较的规则是字典序小的在前面.
* InternalKeyComparator: 在用户传入UserKey之后, LevelDB会将当前的操作类型(kTypeDeletion = 0x0 或者kTypeValue = 0x1), 还有当前操作的Sequence以及一起Encode成一个8 Bytes的数字追加到UserKey后面，这就形成了InternalKey，InternalKeyComparator内部会持有上面的UserKeyComparator，比较的规则是先取出UserKey进行比较，UserKey小的在前，大的在后，如果UserKey相同，则取InternalKey最后8位进行比较(实际上就是比较Sequence的大小，因为不同的记录Sequence肯定是不一样的，所以这里不会存在相同的场景)，Sequence大的在前，小的在后(UserKey相同的情况下，Sequence大的那个记录较新)
* KeyComparator:这个比较器就是Memtable持有的比较器了，在KeyComparator内部会持有上面的InternalKeyComparator, 在KeyComparator内部的operator()接口传入两个Memtable Key，内部调用GetLengthPrefixedSlice()方法从两个Memtable Key中获取到两个Internal Key, 最后就会调用InterKeyComparator的compact方法来比较两个Internal Key的大小.
 
```cpp
/*
 * ******************************** Buf Format ********************************
 *
 *   User Key
 *   |     <Key>      |
 *     Key Size Bytes
 *   
 *   Internal Key
 *   |     <Key>      | <SequenceNumber + ValueType> |
 *     Key Size Bytes              8 Bytes
 *   
 *   Memtable Key
 *  |  <Internal Key Size>  |      <Key>      | <SequenceNumber + ValueType> |   <Value Size>   |      <Value>      |
 *          1 ~ 5 Bytes        Key Size Bytes              8 Bytes               1 ~ 5 Bytes       Value Size Bytes
 *
 */
 
// 不同的UserKey, 字典序小的在前面
// 相同的UserKey, sequence number大的在前面
// 由于不同的record, sequence number肯定不一样, 所以虽然逻辑是
// type大的在前面(kTypeDeletion = 0x0, kTypeValue = 0x1),但是并
// 不会比较到这一层
int InternalKeyComparator::Compare(const Slice& akey, const Slice& bkey) const {
  // Order by:
  //    increasing user key (according to user-supplied comparator)
  //    decreasing sequence number
  //    decreasing type (though sequence# should be enough to disambiguate)
  int r = user_comparator_->Compare(ExtractUserKey(akey), ExtractUserKey(bkey));
  if (r == 0) {
    const uint64_t anum = DecodeFixed64(akey.data() + akey.size() - 8);
    const uint64_t bnum = DecodeFixed64(bkey.data() + bkey.size() - 8);
    if (anum > bnum) {
      r = -1;
    } else if (anum < bnum) {
      r = +1;
    }
  }
  return r;
}

 int MemTable::KeyComparator::operator()(const char* aptr, const char* bptr)
    const {
  // Internal keys are encoded as length-prefixed strings.
  Slice a = GetLengthPrefixedSlice(aptr);
  Slice b = GetLengthPrefixedSlice(bptr);
  return comparator.Compare(a, b);
}
 
```

### MemTableInserter

在外层会对WriteBatch中的rep_进行解析，通过每一条记录的第一个字节进行判断当前操作的行为，如果是kTypeValue则调用下面的Put方法，反之是kTypeDeletion则调用Delete方法，在这里可以看到就算我们当前是Delete操作，实际上也是调用memtable中的Add方法向其中添加一条记录，只不过这个记录带上了kTypeDeletion标记而已, 所以当我们调用LevelDB的Delete方法删除数据的时候，短时间内db体积不但不会立即减小还可能会有所上升，因为删除操作也是转换成一条记录写入了DB.

```cpp
class MemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  MemTable* mem_;

  virtual void Put(const Slice& key, const Slice& value) {
    mem_->Add(sequence_, kTypeValue, key, value);
    sequence_++;
  }
  virtual void Delete(const Slice& key) {
    mem_->Add(sequence_, kTypeDeletion, key, Slice());
    sequence_++;
  }
};
```

### Memtable::Add()

通过调用MemTableInserter的Put()/Delete()方法，我们已经可以用SequenceNumber，ValueType，key以及value来表示一个添加或者删除操作了，接下来我们需要将这些数据编码成一条Memtable Key然后将其插入到Memtable当中，而Memtable::Add()方法正是干这个事的，下面贴出代码并且给出编码后一条完整记录的格式.

```cpp
/*
 * 这个方法实际上是将SequenceNumber, ValueType, Key, Value这些数据合并压缩到一个buf里面去，并且将其插入到SkipList
 * 当中，下面我们来看看这个buf的表现形式
 *
 * ******************************** Buf Format ********************************
 *
 * | <Internal Key Size> |      <Key>      | <SequenceNumber + ValueType> |   <Value Size>   |      <Value>      |
 *       1 ~ 5 Bytes        Key Size Bytes              8 Bytes               1 ~ 5 Bytes       Value Size Bytes
 *
 * Internal Key Size ： 这个存储的是Key的长度 + 存储SequenceNumber和ValueType所需的空间(8 Bytes)
 * 至于一个uint32_t类型的数据为什么要1 ~ 5 Bytes进行存储， 后面会细说
 * Key : 存储了Key的内容
 * SequenceNumber + ValueType: 这个用一个int64_t类型数字的0 ~ 7 Bits存储ValueType,
 * 用8 ~ 63 Bits存储SequenceNumber
 * Value Size : 存储的是Value的长度
 * Value :存储了Value的内容
 *
 * Q: 为什么一个uint32_t类型的数据要用1 ~ 5 Bytes进行存储, 而不是4 Bytes?
 * A: 看了leveldb的EncodeVarint32()方法之后对其有一个了解, 实际上Varint是
 * 一紧凑的数字表示方法, 它用一个或者多个Bytes来存储数字, 数值越小的数字可
 * 以用越少的Btyes进行存储, 这样能减少表示数字的字节数, 但是这种方法也有弊
 * 端, 如果一个比较大的数字可能需要5 Bytes才能进行存储.
 *
 * 原理: 一个Byte有8个字节, 在这种表示方法中最高位字节是一个状态位，而其余
 * 的7个字节则用于存储数据, 如果该Byte最高位字节为1, 则表示当前数字还没有
 * 表示完毕, 还需要下一个Byte参与解析, 如果该Byte最高位字节为0, 则表示数字
 * 部分已经解析完毕
 *
 * e.g..
 * 用varint数字表示方法来存储数字104
 * 104的二进制表现形式:  01101000
 * 使用varint数字表现形式只需1 Byte进行存储: 01101000
 * 这个Byte的最高位为0, 表示表示当前Byte已经是当前解析数字的末尾，
 * 剩余7位1101000是真实数据, 暂时保留
 * 将获取到的真实数据进行拼接 1101000 = 01101000
 * 01101000就是104的二进制表现形式
 *
 * 用varint数字表示方法来存储数字11880
 * 11880的二进制表现形式:  00101110 01101000
 * 使用varint数字表现形式需要2 Byte进行存储: 11101000 01011100
 * 其中第一个Byte中的最高位为1, 表示当前数字还没解析完毕还需后面的
 * Bytes参与解析, 剩余7位1101000是真实数据, 暂时保留
 * 第二个Byte中最高位为0, 表示当前Byte已经是当前解析数字的末尾,
 * 剩余7位1011100是真实数据, 暂时保留
 * 将两次获取的真实数据拼接起来 1011100 + 1101000 = 00101110 01101000
 * 00101110 01101000就是11880的二进制表现形式
 */
void MemTable::Add(SequenceNumber s, ValueType type,
                   const Slice& key,
                   const Slice& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 8;
  const size_t encoded_len =
      VarintLength(internal_key_size) + internal_key_size +
      VarintLength(val_size) + val_size;
  char* buf = arena_.Allocate(encoded_len);
  char* p = EncodeVarint32(buf, internal_key_size);
  memcpy(p, key.data(), key_size);
  p += key_size;
  EncodeFixed64(p, (s << 8) | type);
  p += 8;
  p = EncodeVarint32(p, val_size);
  memcpy(p, value.data(), val_size);
  assert((p + val_size) - buf == encoded_len);
  table_.Insert(buf);
}
```

### SkipList<Key,Comparator>::Insert(const Key& key)

在MemTable::Add()中将数据编码成一条记录之后我们可以看到末尾调用了table_.Insert()方法，这个table\_的类型实际上是一个[SkipList](https://en.wikipedia.org/wiki/Skip_list)，所以这个操作就是在SkipList中添加一个结点，LevelDB之所以选择采用跳表而不是B树，红黑树，平衡二叉树等数据结构，个人认为可能是跳表实现简单，数据有序，高度随机生成，并且由于最终LevelDB需要将SkipList中的数据Flush到sst文件当中，SkipList遍历起来比各种树更加高效.  

```cpp
/*
 *  ——
 * |11|
 * |10|
 * | 9|
 * | 8|
 * | 7|
 * | 6|
 * | 5|
 * | 4|
 * | 3|
 * | 2| ---------------------\   |2| --------> NULL
 * | 1| --------\  |1|--------\  |1| --------> NULL
 *  --       -      -      -      -      -
 * head_ -> "a" -> "b" -> "c" -> "e" -> "f" -> NULL
 *
 *  leveldb中的skiplist最高层数为12(kMaxHeight为12), 上图所示的
 *  skiplist的高度为3(因为除head_以外最高的节点"d"高度为3), 我们
 *  可以把leveldb中的skiplist看成一个简单的单链表，但是这个链表
 *  上的每个结点不单单只包含指向下一个结点的指针，可能包含很多个
 *  指向后续结点的指针，这样就可以快速的跳过一些不必要的结点，从
 *  而加快查找速度，对于链表内的结点拥有1 ~ kMaxHeight个指针，一
 *  结点拥有多少个指向后续元素的指针，这个过程是根据一个随机函数
 *  生成的
 *
 *  往skiplist中插入一个结点的大体步骤有三步
 *  我们假设当前skiplist的高度为cur_height
 *
 *  1. 在各层中(1 ~ cur_height)找到该结点插入位置的前一个结点的指针, 用一个
 *  零时的指针数组prev[kMaxHeight]进行记录(这步执行完毕之后prev[0 ~ cur_height-1]
 *  应该是指向各层级当中该结点插入位置的前一个结点的指针, 然后找到指向该结点插入位
 *  置后一个结点的指针(x), 如果插入位置后面没有结点了，那么这个指针指向的就是NULL.
 *
 *  2. 利用一个随机函数生成当前结点的高度random_height(这个高度不得高于kMaxHeight),
 *  此时如果random_height > cur_height, 那么令prev[cur_height ~ random_height - 1]
 *  分别指向head_.next_[cur_height ~ random_height - 1], (刚到达一个新的高度h时，该
 *  层肯定是没有任何数据结点的，所以前置结点肯定是head_.next[h], 而后置结点就是NULL)
 *  并且更新当前skiplist的高度cur_height = random_height(链表中最高的那个结点的高度, 就是skiplist的高度)
 *  此时如果random_height < cur_height，便什么也不用做, 因为prev[0 ~ random_height - 1]
 *  都已经被赋值了，也就是说各层中该结点插入位置的前置结点的位置都已经找到.
 *
 *  3. 生成高度为random_height的新结点(new_node), 然后修改指针指向就可以了，这里
 *  以第一层为例.
 *
 *  e.g..
 *    new_node指的是我们新生成的结点
 *    new_node.next_[0]指的是新节点第1层所指向的后继结点
 *    prev[0].next_[0]指的是当前新插入结点的前一个结点原先指向的后继结点
 *
 *    第一步我们要做得是令新结点的第一层的后继结点指针指向前一个结点原先指向的后继结点
 *    new_node.next_[0] = prev[0].next_[0]
 *    然后我们要做的就是令新节点作为前一个结点的后继结点
 *    prev[0].next_[0] = &new_node
 *
 */

template<typename Key, class Comparator>
void SkipList<Key,Comparator>::Insert(const Key& key) {
  // TODO(opt): We can use a barrier-free variant of FindGreaterOrEqual()
  // here since Insert() is externally synchronized.
  Node* prev[kMaxHeight];
  // x指向当前插入节点的后一个节点的地址，
  // 如果当前插入节点插入后已经是跳表最后一个节点，则x值为NULL
  Node* x = FindGreaterOrEqual(key, prev);

  // Our data structure does not allow duplicate insertion
  assert(x == NULL || !Equal(key, x->key));

  int height = RandomHeight();
  if (height > GetMaxHeight()) {
    for (int i = GetMaxHeight(); i < height; i++) {
      prev[i] = head_;
    }
    //fprintf(stderr, "Change height from %d to %d\n", max_height_, height);

    // It is ok to mutate max_height_ without any synchronization
    // with concurrent readers.  A concurrent reader that observes
    // the new value of max_height_ will see either the old value of
    // new level pointers from head_ (NULL), or a new value set in
    // the loop below.  In the former case the reader will
    // immediately drop to the next level since NULL sorts after all
    // keys.  In the latter case the reader will use the new node.
    max_height_.NoBarrier_Store(reinterpret_cast<void*>(height));
  }

  x = NewNode(key, height);
  for (int i = 0; i < height; i++) {
    // NoBarrier_SetNext() suffices since we will add a barrier when
    // we publish a pointer to "x" in prev[i].
    x->NoBarrier_SetNext(i, prev[i]->NoBarrier_Next(i));
    prev[i]->SetNext(i, x);
  }
}
```

### MemTable::Get(const LookupKey& key, std::string* value, Status* s)

通过前半部分文章我们已经了解了数据在Memtable中是如何排序的，并且也梳理了数据插入到Memtable的流程，接下来给出一个Key，我们如何在Memtable中进行查找就很简单了，先看一下MemTable::Get()方法的传参，发现传入了一个LookupKey, 下面贴出了LookupKey的构造函数，可以看到构造函数实际上就是将传入的user\_key和SequenceNumber(还有kValueTypeForSeek)进行了编码，然后存入自己分配的内存空间当中.  

```cpp
/*
 * 这个方法实际上就是根据user_key 和 SequenceNumber拼接出来一个LookupKey,
 * 这个LookupKey的格式和当初插入到memtable中的字符串的前缀是一样的(这样才
 * 便于字典序的查找)
 * *************************** Buf Format ***************************
 *
 * | <Internal Key Size> |      <Key>      | <SequenceNumber + ValueType> |
 *       1 ~ 5 Bytes        Key Size Bytes              8 Bytes
 */
LookupKey::LookupKey(const Slice& user_key, SequenceNumber s) {
  size_t usize = user_key.size();
  size_t needed = usize + 13;  // A conservative estimate
  char* dst;
  if (needed <= sizeof(space_)) {
    dst = space_;
  } else {
    dst = new char[needed];
  }
  start_ = dst;
  dst = EncodeVarint32(dst, usize + 8);
  kstart_ = dst;
  memcpy(dst, user_key.data(), usize);
  dst += usize;
  EncodeFixed64(dst, PackSequenceAndType(s, kValueTypeForSeek));
  dst += 8;
  end_ = dst;
}
```

在前一篇文章[LevelDB写入与删除记录](https://axlgrep.github.io/tech/leveldb-write-data-process.html)有提到过LevelDB的快照的概念就是基于Sequence实现的，在LevelDB中不管是执行Put()还是Delete()操作实际上就是向LevelDB中增加记录(每条记录有自己唯一的Sequence,并且这个Sequence是递增的)，在一定的时间内并不会删除数据, 我们在查询数据的时候除了传入user_key之外还会传入ReadOptions，ReadOptions::snapshot不为NULL时表示读取数据库的某一个特定版本。如果Snapshot为NULL，则读取数据库的当前版本，在查找数据的过程当中我们只认为操作号小于快照号的操作是有效操作.

可以看到在MemTable::Get()方法中，迭代器首先是Seek()到了第一个大于等于我们传入的InternalKey的Node, 然后将这个Node其中存储的user_key和我们当前查找的user_key做匹配，若相同，那么这个节点肯定是user_key最新的操作节点，直接做解析返回结果就行了，若不同，则这个MemTable中便没有我们要查找的数据了，直接返回false.


```cpp
/*
 *  eg1..
 *  SET b v1;
 *  SET b v2;
 *  GET b (snapshot = NULL) : Status = ok(), key = b, value = v2;
 *  head_ -> (set a v) -> (set b v2) -> (set b v1) -> (set c v) -> NULL
 *            seq = 1      seq = 4        seq = 3      seq = 2
 *                            ^
 *                           iter
 *
 *  eg2..
 *  SET b v;
 *  DELETE b;
 *  GET b (snapshot = NULL) : Status = NotFound();
 *  head_ -> (set a v) -> (delete  b) -> (set b v) -> (set c v) -> NULL
 *            seq = 1       seq = 4       seq = 3      seq = 2
 *                             ^
 *                            iter
 *  eg3..
 *  SET b v1;
 *  DELETE b;
 *  SET b v2;
 *  Get b (snapshot = NULL) : Status = ok(), key = b, value = v2;
 *  head_ ->  (set a v) -> (set b v2) -> (delete  b) -> (set b v1) -> (set c v) -> NULL
 *             seq = 1       seq = 5       seq = 4       seq = 3       seq = 2
 *                              ^
 *                             iter
 *
 *  eg4..
 *  SET b v1;
 *  DELETE b;
 *  SET b v2;
 *  Get b (snapshot.number = 4) : Status = NotFound();
 *  head_ ->  (set a v) -> (set b v2) -> (delete  b) -> (set b v1) -> (set c v) -> NULL
 *             seq = 1       seq = 5       seq = 4       seq = 3       seq = 2
 *                                            ^
 *                                           iter
 */
bool MemTable::Get(const LookupKey& key, std::string* value, Status* s) {
  Slice memkey = key.memtable_key();
  // 这个Iterator的实现在skiplist.h里面
  Table::Iterator iter(&table_);
  // 在SkipList中找到第一个值大于等于memkey的结点
  iter.Seek(memkey.data());
  if (iter.Valid()) {
    // entry format is:
    //    klength  varint32
    //    userkey  char[klength]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    const char* entry = iter.key();
    uint32_t key_length;
    // 在这里判断iter指向结点的user_key是否和传入的相同，如果相同才有必要
    // 进行判断, 判断当前结点操作的类型，如果是kTypeValue, 则解析出对应的
    // value进行返回，如果是kTypeDeletion则直接返回NotFound(), 需要注意的
    // 是当前iter指向的结点肯定是对应user_key最新的操作结点，具体原因可以
    // 查看LevelDB的三种比较器
    const char* key_ptr = GetVarint32Ptr(entry, entry+5, &key_length);
    if (comparator_.comparator.user_comparator()->Compare(
            Slice(key_ptr, key_length - 8),
            key.user_key()) == 0) {
      // Correct user key
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
      switch (static_cast<ValueType>(tag & 0xff)) {
        case kTypeValue: {
          Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
          value->assign(v.data(), v.size());
          return true;
        }
        case kTypeDeletion:
          *s = Status::NotFound(Slice());
          return true;
      }
    }
  }
  return false;
}
```

### 总结

Memtable在LevelDB中所起到的作用是在内存层面提供了一个随机写的容器，相当于一层LevelDB对外的屏障，肩负着最大的写入压力， 当Memtable的体积达到一个上限，Memtable就会转换成Immutable Memtable，然后紧接着放到后台执行Compact操作，转换成Level 0层的sst文件，这样就使Immutable Memtable中的数据落地了，实际上Memtable和Immutable Memtable的结构完全相同，只不过前者是可读可写，后者只是可读的而已.
