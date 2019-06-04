rootdir="/Users/${USER}/Library/LaunchAgents"
# 当前目录
path=$(dirname $0)
# 将模板拷贝到目录
targetdir="${rootdir}/com.lx.idb.plist"
cp "${path}/com.lx.idb.plist" $targetdir
# 替换启动路径
launtchpath="${path}/idbserver"
/usr/libexec/PlistBuddy -c "Add :ProgramArguments: string ${launtchpath}" $targetdir


