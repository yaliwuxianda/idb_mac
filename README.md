# idb_mac
基于libimobiledevice增加的mac版本idb，可以通过命令行操作沙盒

第一步

先cd到目录，运行 

./autogen.sh --disable-openssl


第二步

执行 

make

第三步

执行

make install


第四步，使用方法

idb devices  获取设备列表

idb shell -appid boundidentify -u 设备编号        进入到idb 的 shell

    push 本机路径 设备路径      将本机文件推送到设备沙盒目录
    cd 设备路径                将当前目录设定为指定路径
    ls                        列出设备路径
    mkdir 设备路径             创建目录
    del  设备路径              删除指定路径



