# rcmd
记录使用频率低的命令，整体结构改自redis初版源码。适用于记录一些重要的配置命令，或者具有很多参数的命令，可以记录不同的命令的执行效果，基本上实现了script命令，记录了屏幕的所有输入输出。

安装： make

使用： 首先执行执行服务端命令 ./rcmd-server &

客户端命令有五个：
打开新终端执行以下命令

1. sadd, ./rcmd sadd ls -al，将命令ls -al及输出保存起来；在所要执行的命令前添加./rcmd sadd,即可将命令行的参数以及命令执行的输出全部保存在内存，可随时查阅
2. sget, ./rcmd sget ls， 将输出所有之前保存的ls命令
3. save, ./rcmd save, 将之前保存的命令及输出写入rcmd.db文件
4. sdel, ./rcmd sdel ls 0, sget命令输出结果中有所记录的命令的index，可以将对应index的内容删除, 如果不加最后的index参数，则默认删除最后添加的数据。
5. allcmd, ./rcmd allcmd, 输出所有已经记录的命令。

在记录多个命令顺序执行时，可以开启一个新的bash
./rcmd sadd bash
之后可以运行任何代码，均会保存，当执行结束后要使用exit命令退出bash