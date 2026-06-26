# Operating System Project

## 中石大操作系统课程设计学习记录

**仅供参考**

整个project具体内容请参考教材《操作系统实验教材：Web服务器性能优化》

包括从实验一到实验五的相关内容，实验五以后的内容请自己探索吧 (x_x)

运行环境为ubuntu2020

##### 主要文件夹结构和内容分布
``` 
    ├── E1/     # 实验一
    |    ├── webserver.c     # 核心webserver代码
    |    ├── Makefile        # 编译指令
    |    ├── index.html      # 测试用html
    |    ├── nigel.jpg       # 测试html依赖的图片
    |    ├── favicon.ico     # 测试html依赖的图片
    |    └── ans/            # 包含一些论述分析类题目的回答，也会包含能够打印自身相关状态的webserver版本
    |   #剩下所有实验大部分文件结构都与实验一相同，以下只标出当前实验独特的文件
    ├── E2/     # 实验二
 	├── E3/     # 实验三
    ├── E4/     # 实验四
    |    ├── pool.c          # 线程池核心实现代码
    |    └── pool.h          # 线程池头文件，提供线程池接口给webserver
    ├── E5/     # 实验五
    |    └── htmls/          # 包含多个网站文件供测试
    └── test/    # 测试用文件夹，包含github上原版的webserver
```

#### Reference
webserver原版代码：https://github.com/ankushagarwal/nweb

线程池参考：https://github.com/Pithikos/C-Thread-Pool

http_load压测工具：https://www.acme.com/software/http_load/