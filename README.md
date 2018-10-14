# lwefence

将每次分配的内存块组织成如下形式：

开辟内存块的大小：
block size = priv data + user data + 2 * pagesize

右边界检测：
```
real addr    user addr
|            |
+------+-----+--------+-------+--+
|      |priv | user   | red   |  |
|      |data | data   | zone  |  |
+------+-----+--------+-------+--+
                      |
                      pagesize align
```

左边界检测：
```
real addr             user addr
|                     |
+------+-----+--------+-------+--+
|      |priv | red    | user  |  |
|      |data | zone   | data  |  |
+------+-----+--------+-------+--+
             |
             pagesize align
```

利用虚拟内存权限，当出现操作red zone区域时，就会出现段错误死在第一现场


注意：
在运行检测工具前，还要执行以下命令：
```
echo 1024000 > /proc/sys/vm/max_map_count
```
因为工具中调用了mprotect会增加1个内核中的vma数据结构，不加大会产生mprotect失败的问题


如果使用memalign和valloc申请的缓冲区大小不是指定的对齐长度，这时溢出是无法被检测的
```
real addr    user addr
|            |
+------+-----+------+---+-------+--+
|      |priv | user |   | red   |  |
|      |data | data |   | zone  |  |
+------+-----+------+---+-------+--+
             |          |
             user align pagesize align
```




