# rcmd
记录使用频率低的命令，整体结构改自redis初版源码

安装： make

使用： 执行服务端命令 ./rcmd-server

客户端命令有五个：

1. sadd, ./rcmd-cli sadd ls -al，将命令ls -al及输出保存起来
2. sget, ./rcmd-cli sget ls， 将输出所有之前保存的ls命令
3. save, ./rcmd-cli save, 将之前保存的命令及输出写入rcmd.db文件
4. sdel, ./rcmd-cli sdel ls 0, sget命令输出结果中有所记录的命令的index，可以将对应index的内容删除, 如果不加最后的index参数，则默认删除最后添加的数据。
5. allcmd, ./rcmd-cli allcmd, 输出所有已经记录的命令。
