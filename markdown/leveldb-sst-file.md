## **LevelDB的SST File**
### 介绍
LevelDB作为一个数据存储引擎，存储的数据大部分是在磁盘上的，而磁盘上数据的表现形式就是文件，也就是本章要介绍的SST文件，SSTable 是 Sorted String Table 的简称，SST的生成时机有两个，一是内存中的Immutable Memtable Flush到磁盘上会生成SST文件，二是在Compaction的时候相邻层级的SST文件合并生成新的SST文件，而这两者都是通过TableBuilder来生成SST文件的，本片将会介绍SST文件的生成过程以及文件结构

### SST文件结构
我们先看一下SST文件的整体结构，实际上SST文件是由一系列的Block和末尾的一个Footer组成的，而Block又分为Data Block(用于存储用户实际数据的), Filter Block(实际上就是一个过滤器，可以快速的判断一个Key是否在某个Data Block当中), MetaIndex Block(用于存储一些Meta信息，目前存储的是Filter的名称，以及Filter Block的起始位置和Size)，最后就是Index Block(存储了该SST文件中每个Data Block的索引Key和Block的起始位置还有Size)

```cpp
/*
 *   下图就是整个Table在文件中的物理布局形式, 我们读取到该文件之后首先从最后48字节中
 *   读取到该Table的Footer.
 *   通过Footer中我们可以获取到IndexBlock在文件中的起始位置和该Block的大小, 通过解析
 *   Index Block, 我们可以获取到文件中每个Raw Block在文件中的位置信息和每个Raw Block
 *   的索引Key, 这样就可以快速的定位到我们当前需要查询的Key可能存在于哪个Raw Block当
 *   中.
 *   通过Footer我们还可以获取到MetaIndex Block, MetaIndex Block中含有Filter Block在
 *   文件中的起始位置和该Block的大小, 我们通过解析Filter Block可以获取到对应于每段
 *   Raw Block的布隆过滤器的关键字符串, 通过这个字符串我们可以判断对应Raw Block是否
 *   存在我们需要查询的Key.
 *   至此, 一个Table建立的过程以及最后Table的结构已经完全介绍完毕.
 *
 *            |-------------------|---
 *            |   Data Block 1    |   \
 *            |-------------------|    |
 *            |   Data Block 2    |    |
 *            |-------------------|      --> Data Block中存储了真实的数据以及重启点等信息
 *            |   Data Block 3    |    |     具体可以看本文件中TableBuilder::Add()方法上
 *            |-------------------|    |     方有注释
 *            |        ...        |   /
 *            |-------------------|---
 *            |      result_      |   \
 *            |-------------------|    |
 *  4 Bytes   |    offsets_[0]    |    |
 *            |-------------------|    |
 *  4 Bytes   |    offsets_[1]    |    |
 *            |-------------------|    |
 *  4 Bytes   |    offsets_[2]    |    |
 *            |-------------------|      --> 这部分是Filter Block, 其中result_是由各个Data Block中的Key通过Hash计算出来的特征字符串拼接
 *            |        ...        |    |     来的, 下面有一个offset_数组, offset_[0]记录result_中属于Data Block 1的特征字符串的起始位置,
 *            |-------------------|    |     last_word记录的是result_字符串的大小
 *  4 Bytes   |    last_word      |    |
 *            |-------------------|    |
 *  1 Byte    |  kNoCompression   |    |
 *            |-------------------|    |
 *  4 Bytes   |        CRC        |   /
 *            |-------------------|---
 *            |  MetaIndex Block  |      --> 用于记录Filter的名称，以及上方Filter Block的起始位置和大小(该Block尾部也包含压缩方式和CRC)
 *            |-------------------|---
 *            |  Pending Entry 1  |   \
 *            |-------------------|    |
 *            |  Pending Entry 2  |    |
 *            |-------------------|    |
 *            |  Pending Entry 3  |    |
 *            |-------------------|      --> 这部分是Index Block, 其中的Pending Entry 1中包含Raw Block 1的索引Key, 以及Raw Block 1在文件
 *            |        ...        |    |     中的位置信息, 以及Raw Block 1的大小.
 *            |-------------------|    |
 *  1 Bytes   |  kNoCompression   |    |
 *            |-------------------|    |
 *  4 Bytes   |        CRC        |   /
 *            |-------------------|---
 *  48 Bytes  |       Footer      |      --> 包含MetaIndex Block的Index Block的索引信息(在文件中的位置以及大小), 和魔数, 具体细节可以看format.cc的Footer::EncodeTo();
 *            |-------------------|
 *
 *  其中RawBlock和MetaIndexBlock还有IndexBlock内部的结构都是一样的，将每个KV编码成一个条目之后添加到Block当中
 *  然后如果前一个条目和当前条目有前缀重叠部分, 那么就会通过共享前缀来节约空间, 每隔固定的条目该Block都会强制
 *  加入一个重启点, 该Block的尾部会加入重启点数组, 以及重启点数组的大小
 */
```

### BlockBuilder
通过上面的介绍，我们了解到实际上SST文件大部分是由一个个的Block构成的，而在LevelDB中将数据转换成文件里面的Block就是由BlockBuilder来完成的了， 下面我们先来看一下BlockBuilder的定义.

```cpp
class BlockBuilder {
 public:
  explicit BlockBuilder(const Options* options);
  void Reset();
  void Add(const Slice& key, const Slice& value);
  Slice Finish();
  size_t CurrentSizeEstimate() const;
  bool empty() const {
    return buffer_.empty();
  }

 private:
  const Options*        options_;
  std::string           buffer_;      // 存储当前Block内容的buffer
  std::vector<uint32_t> restarts_;    // 重启点，后面会介绍
  int                   counter_;     // 当前这个Block里面有多少条记录
  bool                  finished_;    // Has Finish() been called?
  std::string           last_key_;    // 记录当前这个Block最后一个key

  // No copying allowed
  BlockBuilder(const BlockBuilder&);
  void operator=(const BlockBuilder&);
};
```

实际上BlockBuilder内部就是维护了一个buffer_用于存储当前Block的内容，另外作为数据存储引擎，存储相同量级的数据，能尽量减少磁盘空间占用是很重要的，通过前面的博客我们了解到LevelDB中的数据都是按照Key有序存储的，LevelDB很好的利用了相邻的Key可能有相同前缀的这个特点来减少数据存储量，实际上就是前缀压缩机制，当前Key如果和前一个Key拥有相同的前缀，那么当前Key只需要记录和前一个Key不同的尾缀部分以及记录一些额外信息，这样就能通过前一个Key和自身的额外信息就能拼凑出当前Key.

这么做能很好的降低了数据的存储， 但是也引入了一个风险，也就是如果最开头的Key被损坏了，后面的所有Key都将无法恢复，为了降低数据损坏的风险，LevelDB在Block中引入的重启点的概念，也就是每隔固定条目的数据会强制插入一个重启点，重启点位置的Key完整的存储自身的数据，而不是需要根据前一个Key才能拼凑出当前Key, 说了这么多，可能有点抽象，下面我们通过BlockBuilder里面最重要的Add()方法来了解一下，重启点是怎么有效的节省空间以及数据是怎么样被添加到Block里的.

```cpp
/*
 * 这个方法的作用是向当前的block中添加一条Entry，由于我们调用这个方法添加key
 * 实际上是字典有序的(从memtable中遍历得到的自然是有序的), 所以前一个添加的key
 * 和后一个key可能会存在"部分前缀的重叠"(abcdd和abccc重叠部分就是abc), 为了节
 * 约空间, 后一个key可以只存储和前一个key不同的部分(这个例子中后一个key只需要
 * 存储cc即可), 这种做法有利有弊，既然只存储了和前一个key不同的部分，那么我们
 * 需要一些额外的空间来存储一些其他的数据，比如说共享长度(shared), 非共享长度
 * (non_shared)等等 这些数据都是用一个int32_t类型的数字进行存储,在memtable.cc
 * 中提到过, 这是一种紧凑型的数字表示法, 下面附上的记录在内存中的表现形式和example.
 *
 * e.g..
 *
 * key: Axl     value: vv
 * key: Axlaa   value: vv
 * key: Axlab   value: vv
 * key: Axlbb   value: vv
 *
 * ******************************** Entry Format ********************************
 * |   <Shared>   |   <Non Shared>   |   <Value Size>   |   <Unprefixed Key>   |     <Value>     |
 *   1 ~ 5 Bytes      1 ~ 5 Bytes        1 ~ 5 Bytes        non_shared Bytes     value_size Bytes
 *
 *       0                3                  2                   Axl                    vv
 *       3                2                  2                   aa                     vv
 *       4                1                  2                   b                      vv
 *       3                2                  2                   bb                     vv
 *
 *
 *  (真实的key, 尾部还会带上SequenceNumber和ValueType，这里为了example更清晰便没有展示出来)
 *
 *
 *  这里还有一重启点(restarts_)的概念, 重启点存在的目的是为了避免最开始的记录损坏,
 *  而其后面的所有数据都无法恢复的情况发生, 为了降低这个风险, leveldb引入了重启点,
 *  也就是每隔固定的条数(block_restart_interval)的Entry, 都强制加入一个重启点, 重
 *  启点指向的Entry会完整的记录自身的key(shared为0, 不再依赖上一条Entry, 前面的Entry
 *  损坏也不会影响重启点指向的Entry的读取)
 *
 *  当前所有的重启点会有序的记录在restarts_集合当中, 最后Flush到文件的时候这个重启点
 *  集合以及集合大小会写在当前block的尾部.
 */

void BlockBuilder::Add(const Slice& key, const Slice& value) {
  Slice last_key_piece(last_key_);
  assert(!finished_);
  assert(counter_ <= options_->block_restart_interval);
  assert(buffer_.empty() // No values yet?
         || options_->comparator->Compare(key, last_key_piece) > 0);
  size_t shared = 0;
  // 如果当前的counter_已经到达了block_restart_interval上限的时候, 那么
  // shared就为0, 下一条recored不再共享前缀
  if (counter_ < options_->block_restart_interval) {
    // See how much sharing to do with previous string
    const size_t min_length = std::min(last_key_piece.size(), key.size());
    while ((shared < min_length) && (last_key_piece[shared] == key[shared])) {
      shared++;
    }
  } else {
    // Restart compression
    restarts_.push_back(buffer_.size());
    counter_ = 0;
  }
  const size_t non_shared = key.size() - shared;

  // Add "<shared><non_shared><value_size>" to buffer_
  PutVarint32(&buffer_, shared);
  PutVarint32(&buffer_, non_shared);
  PutVarint32(&buffer_, value.size());

  // Add string delta to buffer_ followed by value
  buffer_.append(key.data() + shared, non_shared);
  buffer_.append(value.data(), value.size());

  // Update state
  // 这里不将last_key_直接赋值为key的原因是为了减少内存拷贝
  last_key_.resize(shared);
  last_key_.append(key.data() + shared, non_shared);
  assert(Slice(last_key_) == key);
  counter_++;
}
```

通过上面的Add()方法， 我们已经能把key/value有效的，节约空间的存储到BlockBuilder中的buffer_当中去了，并且把当前Block的所有重启点加入到了restarts\_集合当中，由于LevelDB中Block的大小是可配置的(默认是4KB), 所以当一个Block中buffer\_的体积大于等于options.block_size的时候便要调用Finish方法将重启点信息追加到当前Block的末尾， 准备将这个Block写入磁盘了， 下面是BlockBuilder的Finish()方法， 实现很简单就是将所有重启点依次append到buffer_后面，并且在末尾再追加重启点数量方便向前查找， 刚看这段代码的时候疑惑为什么记录数字不用LevelDB的紧凑型数字表示法，后来想明白了.

```cpp
/*
 * 如果在这里采用紧凑类型的数字表示法，那么每个int32_t数据
 * 类型在当前block所占用的空间为1 ~ 5 Bytes大小不等，这样
 * 我们便无法准确获取到存储重启点数据的位置，按照以下方法
 * 就很好找了，我们读取当前block的最后4 Bytes，然后解析为
 * 一个int32_t类型的数字n，这个n就是我们重启点数组的大小,
 * 然后再往前读取4 * n Bytes, 这些就是重启点的数据了
 */
Slice BlockBuilder::Finish() {
  // Append restart array
  for (size_t i = 0; i < restarts_.size(); i++) {
    PutFixed32(&buffer_, restarts_[i]);
  }
  PutFixed32(&buffer_, restarts_.size());
  finished_ = true;
  return Slice(buffer_);
}
```

通过BlockBuilder的Add()以及Finish()方法解读，我们对于SST文件中的Block已经有了一个大概的了解， 下面列出SST文件中Block的结构，让我们有一个更加直观的了解, 可以看到SST文件的Block末尾还加入了1 Byte用于记录这个Block的压缩方式(目前LevelDB只支持不压缩和Snappy压缩), 以及4 Bytes的数据校验码， 这个校验码是根据这个Block的内容生成的uint32_t类型的整数，用于判别数据是否在生成以及传输中出错.

```cpp
/*
 *                 Raw Block
 *
 *           |-------------------|
 *           |      Entry 1      |  Entry1, Entry2... 是按照字典序进
 *           |-------------------|  行排列的, 其中含有shared,
 *           |      Entry 2      |  non_shared, value_size... 等等一
 *           |-------------------|  些字段，实际上存储的就是KV键值对,
 *           |      Entry 3      |  详细可看block_builder.cc的Add()方
 *           |-------------------|  法上有注释.
 *           |      Entry 4      |
 *           |-------------------|
 *           |        ...        |
 *           |-------------------|
 *  4 Bytes  |    restarts[0]    |  leveldb中每隔固定条数的Entry会强制加入一个重启
 *           |-------------------|  这里存储的数组restarts实际上就是指向这些重启点
 *  4 Bytes  |    restarts[1]    |  的(restarts[0]的值永远是0，因为block->Reset()
 *           |-------------------|  的时候First restart point is at offset 0)
 *  4 Bytes  |    restarts[2]    |
 *           |-------------------|
 *  4 Bytes  |         3         |  重启点数组的大小
 *           |-------------------|
 *  1 Byte   |  CompressionType  |  数据的压缩方式, 是kSnappy或者kNo
 *           |-------------------|
 *  4 Bytes  |        CRC        |  根据上方除了CompressionType计算出来的一个校验值
 *           |-------------------|
 *
 *  上图是一个完整的Raw Block.
 *
 *  对于Index Block: 每当写完一个完整的Raw Block都会计算出一个索引key(大
 *  于或者等于当前Block中最大Key的那个Key), 以及当前Data Block在文件中距
 *  离文件起始位置的偏移量以及当前Data Block(在这里Data Block去掉CompressionType
 *  和CRC我们称为Data Block)的大小, 我们会将索引key, 当前data block的偏移量
 *  以及当前data block的大小当做一条Entry写入到Index Block当中.
 */
```

### TableBuilder
文章开头有提到过，LevelDB的SST文件是由TableBuilder生成的，而TableBuilder内部实际上是持有了BlockBuilder(用于写data\_block以及index\_block)和FilterBlockBuilder(用于写filter\_block)，外界通过调用TableBuilder的Add()方法，向TableBuilder的data\_block中添加数据，并且在必要的时候更新Index\_block以及filter_block中的内容

从下面列出的TableBuilder::Add()方法可以看出，每当写完一个data\_block并且刷盘之后，都会计算出这个data\_block的索引key出来，并且将这个索引key以及这个data\_block在SST文件中的offset和size信息添加到index_block当中，后续查找数据我们就可以通过这个index_block快速的定位到我们需要查找的key可能当前SST文件的哪个Block当中了

```cpp
void TableBuilder::Add(const Slice& key, const Slice& value) {
  Rep* r = rep_;
  assert(!r->closed);
  if (!ok()) return;
  if (r->num_entries > 0) {
    assert(r->options.comparator->Compare(key, Slice(r->last_key)) > 0);
  }

  // 当我们写完一个Data Block以后要记录下一些数据
  if (r->pending_index_entry) {
    assert(r->data_block.empty());
    // 计算出大于或者等于当前Block中最大Key的那个Key(我们称为索引Key)
    // 但是计算出来的这个key要比下一个Block中最小的Key要小
    r->options.comparator->FindShortestSeparator(&r->last_key, key);
    std::string handle_encoding;
    // 记录当前Raw Block在文件中的位置距离文件起始位置的偏移量
    // 记录当前block_content的size (注意，这里不是整个Raw Block的
    // 大小而是KV数据和restarts_这些数据的大小, 我们称为Data Block)
    r->pending_handle.EncodeTo(&handle_encoding);
    // 将我们计算出来的当前Block的索引Key, 以及当前Block距离文件起始
    // 位置的偏移量和Block大小添加到index_block当中(实际上index_block
    // 的结构和Data Block是一模一样的, 就是会有重启点, 和共享前缀这些)
    r->index_block.Add(r->last_key, Slice(handle_encoding));
    // 将其置为false, 表示当前Raw Block已经处理完毕
    r->pending_index_entry = false;
  }

  if (r->filter_block != NULL) {
    r->filter_block->AddKey(key);
  }

  r->last_key.assign(key.data(), key.size());
  r->num_entries++;
  r->data_block.Add(key, value);


  // 每个Block中存储了多条record, 以及重启点数组, 还有重启点数组
  // 的大小(uint32_t), 在option中我们配置block_size, 实际上在文件
  // 当中每个block的大小并不是一定是我们的block_size.
  const size_t estimated_block_size = r->data_block.CurrentSizeEstimate();
  if (estimated_block_size >= r->options.block_size) {
    Flush();
  }
}
```

对于TableBuilder::Finish()方法这里就不贴代码了， 由于数据(data\_block)已经写完了，接下来依次在SST文件后面追加filter\_block(用于快速判断一个key是否可能存在于当前的SST文件当中)， metaindex\_block(用于记录当前filter的名称，以及
filter\_block的位置信息offset和size)，index_block(用于记录SST文件中各个data\_block的索引key以及对应的位置信息offset和size)，最后会在SST文件的尾部添加Footer信息，下面来看一下Footer的格式

可以看到Footer里面记录了meta index block的位置信息和index block的位置信息， 所以当我要在SST文件中找一个key的时候首先是通过Footer里面的Index Block offset和Index Block size找到Index Block， 然后Index Block里面就有当前这个SST文件的所有Data Block的索引Key, 以及Block的位置信息，这样就能快速的定位到我们要查找的数据. 

```cpp
/*
 *  用来编码文件的Footer, Footer包含Meta Block
 *  在文件中的位置以及Meta Block的大小以及
 *  Index Block在文件中的位置和Index Block的大
 *  小以及文件尾部的"魔数"
 *
 *  Q: 为什么要添加Padding?
 *  A: 由于这里记录这些Block的位置以及大小是使用
 *     的紧凑型的数字表示法, 所以表示一个int64_t
 *     的数字使用的空间在1 ~ 10个Bytes不等, 为了
 *     我们从尾部能快速的找到Footer的在文件中的起
 *     始位置(8 + 4 * 10)Bytes, 所以我们在这里加
 *     入了Padding进行填充对齐
 *
 *                  |---------------------------|
 *   1 ~ 10 Bytes   |  Meta Index Block offset  |
 *                  |---------------------------|
 *   1 ~ 10 Bytes   |   Meta Index Block size   |
 *                  |---------------------------|
 *   1 ~ 10 Bytes   |    Index Block offset     |
 *                  |---------------------------|
 *   1 ~ 10 Bytes   |     Index Block size      |
 *                  |---------------------------|
 *                  |          Padding          |
 *                  |---------------------------|
 *     8 Bytes      |        MagicNumber        |
 *                  |---------------------------|
 */

void Footer::EncodeTo(std::string* dst) const {
  const size_t original_size = dst->size();
  metaindex_handle_.EncodeTo(dst);
  index_handle_.EncodeTo(dst);
  dst->resize(2 * BlockHandle::kMaxEncodedLength);  // Padding
  PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber & 0xffffffffu));
  PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber >> 32));
  assert(dst->size() == original_size + kEncodedLength);
  (void)original_size;  // Disable unused variable warning.
}
```

### 总结
通过上述介绍，不难发现SST文件的创建过程是非常快速的，因为新添加的数据首先都是先存入一个Block里，然后依次向文件末尾追加Block，是一个顺序写的过程，但是如果是在LevelDB中的sst文件中查找一个key速度就没那么快了，首先要通过Index Block来查找Key可能在哪个Data Block中，然后再在Data Block中进行查找，另外LevelDB的sst文件是有层级结构的，Level0层的sst文件由于是不同时刻的Immutable Memtable直接Flush而成，所以文件和文件之间可能存在overlap，如果需要在Level0层的sst文件中查找某一个Key的数据，可能需要查找这一层的所有sst文件才能确定结果，不过对此LevelDB也引入了一系列策略来提高查询效率，比如布隆过滤器.
