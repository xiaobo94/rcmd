# rcmd
记录使用频率低的命令，整体结构改自redis初版源码。适用于记录一些重要的配置命令，或者具有很多参数的命令，可以记录不同的命令的执行效果。

安装： make

使用： 首先执行执行服务端命令 ./rcmd-server

客户端命令有五个：
打开新终端执行以下命令

1. sadd, ./rcmd-cli sadd ls -al，将命令ls -al及输出保存起来；在所要执行的命令前添加./rcmd-cli sadd,即可将命令行的参数以及命令执行的输出全部保存在内存，可随时查阅
2. sget, ./rcmd-cli sget ls， 将输出所有之前保存的ls命令
3. save, ./rcmd-cli save, 将之前保存的命令及输出写入rcmd.db文件
4. sdel, ./rcmd-cli sdel ls 0, sget命令输出结果中有所记录的命令的index，可以将对应index的内容删除, 如果不加最后的index参数，则默认删除最后添加的数据。
5. allcmd, ./rcmd-cli allcmd, 输出所有已经记录的命令。
