# lwefence

将每次分配的内存块组织成如下形式：

开辟内存块的大小：
block size = priv data + user data + 2 * pagesize

右边界检测：
```
real addr    user addr
|            |
+---------------------+-------+--+
|      |priv | user   | red   |  |
|      |data | data   | zone  |  |
+---------------------+-------+--+
```

左边界检测：
```
real addr                user addr
|                        |
+------------------------+-------+
|         |priv | red    | user  |
|         |data | zone   | data  |
+------------------------+-------+
```

利用虚拟内存权限，当出现操作red zone区域时，就会出现段错误死在第一现场


PS.
在运行检测工具前，还要执行以下命令：
```
echo 1024000 > /proc/sys/vm/max_map_count
```
因为工具中调用了mprotect会增加1个内核中的vma数据结构，不加大会产生mprotect失败的问题
