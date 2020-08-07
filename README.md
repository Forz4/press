# PRESS

一款基于TCP单工异步长连接的压力测试工具

[TOC]

## 特点

1. 组包配置灵活
2. TPS和发送时间实时调整
3. 外卡模式支持
4. 报文匹配和响应时间分析

## 编译

### linux

cd src && make clean all

### aix

需修改makefile中的CCFLAG和CC变量

## 配置

### 组包配置

#### cfg/pack.cfg

组包配置文件

通过load命令进行加载，也可以自定义配置文件名，通过**load 文件名**方式加载。

    #   组包进程配置文件
    #   组包进程以模板文件中的报文为基础，依据替换规则进行替换后发送
    #   单个组包进程的最大TPS受到机器性能的影响
    #   
    #   第1域 :  模板文件(路径前缀为$PRESS_HOME/data/tpl)
    #   第2域 :  替换规则文件(路径前缀为$PRESS_HOME/data/rule)
    #   第3域 :  初始TPS
    #   第4域 :  发送时长
    [1000.tpl][1000.rule][1000][100]

#### data/tpl/*.tpl

报文模板文件

对应组包配置文件cfg/pack.cfg中的第1域，文件仅包含1行，报文格式为**“4位长度+报文体"**，以下为一个合法的报文模板。

```
0020abcdefghijklmnopqrst
```

#### data/rule/*.rule

替换规则文件

对应组包配置文件cfg/pack.cfg中的第2域，规定了报文域替换方式，以下是一个合法的替换规则举例:

```
# 替换规则文件
# 第1域 :    起始位置
# 第2域 :    替换长度
# 第3域 :    补位方式 
#               1 左补0
#               2 右补空格
# 第4域 :    替换种类
#               1. FILE:文件名   使用对应文件内容替换
#               2. RAND          使用随机数替换
#               3. F7            填充8583报文的7域，格式为MMDDhhmmss，0502182425          
# 从第一位开始替换2字节长度，使用1000.rep文件内容进行替换
[1][2][1][FILE:1000.rep]
# 从第三位开始替换3字节长度，使用随机数替换
[3][3][1][RAND]
# 从第7位开始替换10字节长度，按照7域MMDDhhmmss格式替换
#[7][10][1][F7]
```

#### data/rep/*.rep

域替换文件

对应替换规则文件data/rule/*.rule中的第四域中的文件名，表示将用rep文件的内容逐行替换模板报文对应的域。

### 通讯配置

通讯配置文件为${PRESS_HOME}/cfg/conn.cfg，通过init命令进行加载和启动。

    cfg/conn.cfg
    #   通讯进程配置文件
    #   每一行表示一个通讯进程
    #   第1域: 进程类型 
    #          S: 通讯发送进程
    #          R: 通讯接收进程
    #          J: 外卡通讯进程
    #   第2域: IP地址
    #   第3域: 端口
    #   第4域: 持久化开关
    #          1: 打开报文持久化
    #          0: 关闭报文持久化
    [S][127.0.0.1][4566][1]
    [S][127.0.0.1][4567][1]
    [S][127.0.0.1][4568][1]
    [S][127.0.0.1][4569][1]
    [R][127.0.0.1][4566][1]
    [R][127.0.0.1][4567][1]
    [R][127.0.0.1][4568][1]
    [R][127.0.0.1][4569][1]
    #[J][127.0.0.1][4569][1]

### 主控配置

守护主控配置文件为${PRESS_HOME}/cfg/press.cfg

    cfg/press.cfg
    # 命令消息队列key
    MSGKEY_CMD=18000
    # 报文发送消息队列key
    MSGKEY_OUT=18001
    # 报文持久化消息队列key
    MSGKEY_IN=18002
    # 心跳报文间隔,0表示无心跳报文
    HEART_INTERVAL=0
    # 日志级别 0-LOGNON , 1-LOGFAT , 2-LOGERR , 3-LOGWAN , 4-LOGINF , 5-LOGADT , 6-LOGDBG
    LOGLEVEL=4
    # 持久化报文的编码格式 , A表示Ascii , H表示十六进制
    ENCODING=A
    # 持久化进程个数
    CATCHER_NUM=1

## 运行

    1. 修改env/press.env中PRESS_HOME环境变量值为当前press工具的安装目录
    2. 执行`. press.env`加载环境变量
    3. 在终端执行`presscmd`命令启动控制台程序

控制台命令说明

```
<deam>    启动PRESS守护进程
<kill>    停止PRESS守护进程
<init>    启动通讯模块
<stop>    停止通讯模块
<send>    启动组包和持久化进程
<shut>    停止组包和持久化进程
<stat>    查看状态
<load>    加载组包配置文件
          <load [文件名]>
<moni>    开启监视器模式
<tps>     设置实时TPS值,命令格式:
          <tps [+-]调整值[%] [序号]>
           tps +100		    将所有组包进程tps增加100
           tps -10%		    将所有组包进程tps减少10%
           tps +100% 0		将序号为0的组包进程tps增加100%
           tps 10		      将所有的组包进程tps设置为10
           tps 10 1		    将序号为1的组包进程tps设置为10
<tps>     设置发送时间,命令格式:
          <time [+-]调整值[%] [序号]>
          举例参考tps命令
<exit>    退出客户端
<help>    打印帮助
```

