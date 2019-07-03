
#include <pthread.h>
#include <libimobiledevice/house_arrest.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/libimobiledevice.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <signal.h>
#include <stdlib.h>

idevice_t device = NULL;
afc_client_t afcclient = NULL;
char *appid = NULL;
char *udid = NULL;
//读取文件内容
char * readFileContent(char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return NULL;
    }
    char s[2];
    int num = 0;
    char *currcmd;
    char *lastcmd;
    while ((fgets(s, 2, file)) != NULL)
    {
        num++;
        if (!lastcmd)
        {
            currcmd = malloc((num + 1) * sizeof(char *));
            sprintf(currcmd, "%s", s);
            lastcmd = malloc((num + 1) * sizeof(char *));
            strcpy(lastcmd, currcmd);
        }
        else
        {
            free(currcmd);
            currcmd = malloc((num + 1) * sizeof(char *));
            sprintf(currcmd, "%s%s", lastcmd, s);
            free(lastcmd);
            lastcmd = malloc(2 * num * sizeof(char *));
            strcpy(lastcmd, currcmd);
        }
    }
    free(lastcmd);
    fclose(file);

    return currcmd;
}
void writeFileContent(char *filepath, char *content)
{
    FILE *file = fopen(filepath, "w");
    if (file == NULL)
    {
        return;
    }
    fwrite(content, strlen(content) + 1, 1, file);
    fclose(file);
}
//切割字符串
void split(char *src, const char *separator, char **dest, int *num)
{
    char *pNext;
    int count = 0;
    if (src == NULL || strlen(src) == 0)
    {
        return;
    }
    if (separator == NULL || strlen(separator) == 0)
    {
        return;
    }
    pNext = strtok(src, separator);
    while (pNext)
    {
        *dest++ = pNext;
        ++count;
        pNext = strtok(NULL, separator);
    }
    *num = count;
}
void saveAu()
{
    char *content = malloc((strlen(appid) + strlen(udid) + 1) * sizeof(char *));
    sprintf(content, "%s\n%s", appid, udid);
    writeFileContent("au.txt", content);
    free(content);
}
void readAu()
{
    char *content=readFileContent("au.txt");
    if (content != NULL)
    {
        char *arr[3];
        int num = 0;
        split(content, "\n", arr, &num);
        if (num >= 2)
        {
            appid = arr[0];
            udid = arr[1];
        }
    }
}
void getAFCClient(char *udid, char *appid, afc_client_t *afc_client)
{
    int *error = malloc(sizeof(int));
    if (device == NULL)
    {
        idevice_new(&device, udid);
        if (!device)
        {
            *error = -10;
            goto l_device;
        }
    }

    lockdownd_client_t lockdownd_client = NULL;
    lockdownd_error_t lockdownd_err = LOCKDOWN_E_UNKNOWN_ERROR;

    lockdownd_err = lockdownd_client_new_with_handshake(device, &lockdownd_client, "handshake");
    if (lockdownd_err != LOCKDOWN_E_SUCCESS)
    {
        *error = -10;
        goto l_device;
    }

    //lockdownd_client_free(lockdownd_client);

    house_arrest_error_t err = 0;
    house_arrest_client_t client = NULL;

    err = house_arrest_client_start_service(device, &client, NULL);
    if (err != HOUSE_ARREST_E_SUCCESS)
    {
        *error = err;
        goto l_device;
    }

    err = house_arrest_send_command(client, "VendContainer", appid);
    if (err != HOUSE_ARREST_E_SUCCESS)
    {
        *error = err;
        goto l_house;
    }

    plist_t dict = NULL;

    err = house_arrest_get_result(client, &dict);

    if (err != HOUSE_ARREST_E_SUCCESS)
    {
        printf("house_arrest_get_result  :%d\n", err);
        *error = err;
        goto l_house;
    }
    char *xml = NULL;
    uint32_t length = 0;
    plist_to_xml(dict, &xml, &length);

    if (xml)
        free(xml);

    plist_t node = plist_dict_get_item(dict, "Status");
    if (!node)
    {
        *error = -4;
        goto l_list;
    }
    char *status = NULL;
    plist_get_string_val(node, &status);
    if (!status)
    {
        *error = -4;
        goto l_list;
    }

    if (strcmp(status, "Complete") != 0)
    {
        *error = -4;
        goto l_status;
    }

    afc_error_t afc_err = afc_client_new_from_house_arrest_client(client, afc_client);

    if (afc_err != AFC_E_SUCCESS)
    {
        *error = afc_err;
        goto l_status;
    }

l_status:
    printf("");
    free(status);
l_list:
    printf("");
    plist_free(dict);
l_house:
    printf("");
l_device:
    printf("");
}
//获取设备列表
void getDeviceList(char ***dev_list)
{
    int i;
    if (idevice_get_device_list(dev_list, &i) < 0)
    {
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
    }
}
//释放设备列表
void freeDeviceList(char **dev_list)
{
    idevice_device_list_free(dev_list);
}
//获取设备名称
void getDeviceName(char *udid, char **devicename)
{
    idevice_t device = NULL;
    if (idevice_new(&device, udid) != IDEVICE_E_SUCCESS)
    {
        fprintf(stderr, "ERROR: Could not connect to device\n");
        return;
    }
    lockdownd_client_t lockdown = NULL;
    lockdownd_error_t lerr = lockdownd_client_new_with_handshake(device, &lockdown, "idevicename");
    if (lerr != LOCKDOWN_E_SUCCESS)
    {
        idevice_free(device);
        fprintf(stderr, "ERROR: Could not connect to lockdownd, error code %d\n", lerr);
        return;
    }
    int res;
    lerr = lockdownd_get_device_name(lockdown, devicename);
    if (*devicename)
    {
        res = 0;
    }
    else
    {
        fprintf(stderr, "ERROR: Could not get device name, lockdown error %d\n", lerr);
    }

    lockdownd_client_free(lockdown);
    idevice_free(device);
}
int dirIsExists(afc_client_t client, char *currdir, char *todir)
{
    int isexists = 1;
    if (strcmp(todir, "/") == 0)
    {
    }
    else
    {
        char *todirs[10];
        int num = 0;
        split(todir, "/", todirs, &num);

        if (num > 0)
        {
            char *tempdir = malloc(sizeof(char *) * 200);
            strcpy(tempdir, currdir);
            for (int i = 0; i < num; i++)
            {
                char *ts = malloc(sizeof(char *) * 200);
                strcpy(ts, todirs[i]);

                char **dirlist2 = NULL;

                afc_error_t afc_err = afc_read_directory(client, tempdir, &dirlist2);

                if (afc_err == AFC_E_SUCCESS)
                {
                    int isexists2 = 0;
                    for (int j = 0; dirlist2[j] != NULL; j++)
                    {
                        char *dir = dirlist2[j];
                        if (strcasecmp(dir, ts) == 0)
                        {
                            isexists2 = 1;
                            break;
                        }
                    }
                    if (isexists2 == 1)
                    {
                        sprintf(tempdir, "%s%s/", tempdir, ts);
                        continue;
                    }
                    else
                    {
                        isexists = 0;
                        break;
                    }
                }
                else
                {
                    isexists = 0;
                    break;
                }
            }
        }
    }
    return isexists;
}
/*
把电脑上的文件推送到设备
*/
afc_error_t copyFileToDevice(afc_client_t client, char *sourceFilePath, char *targetDir)
{
    afc_error_t error = 0;
    //获取文件大小
    int bysize = getFileSize(sourceFilePath);

    FILE *infile;
    infile = fopen(sourceFilePath, "rb");
    unsigned char *bs = malloc(bysize);
    if (infile == NULL)
        error = 1;
    int filelen = fread(bs, 1, bysize, infile);
    fclose(infile);

    error = 0;

    long filehandle = 0;

    if (error == 0)
    {
        char *ks[10];
        int num;
        split(sourceFilePath, "/", ks, &num);
        char *targetFilePath = malloc(sizeof(char *) * 200);
        if (targetDir[strlen(targetDir) - 1] == '/')
        {
            char *tempdir = malloc(sizeof(char *) * 200);
            sprintf(tempdir, "%s%s", targetDir, ks[num - 1]);
            strcpy(targetFilePath, tempdir);
            free(tempdir);
        }
        else
        {
            targetFilePath = targetDir;
        }
        error = afc_file_open(client, targetFilePath, AFC_FOPEN_WRONLY, &filehandle);
        if (error == 0)
        {
            int bytesWritten = 0;
            error = afc_file_write(client, filehandle, bs, filelen, &bytesWritten);
            if (error == 0)
            {
                error = afc_file_close(client, filehandle);
            }
            else
            {
                error = afc_file_close(client, filehandle);
            }
        }
    }
    free(bs);
    return error;
}
afc_error_t copyFileFromDevice(afc_client_t client, char *sourceFilePath, char *targetDir)
{
    afc_error_t error = 0;
    //每次计划读取的字节数
    int filelen = 102400;
    unsigned char *bs = malloc(filelen);

    long filehandle = 0;

    error = afc_file_open(client, sourceFilePath, AFC_FOPEN_RDONLY, &filehandle);
    if (error == 0)
    {
        //实际读取的字节数量
        int bytesReaden = 0;

        error = afc_file_read(client, filehandle, bs, filelen, &bytesReaden);

        if (error == 0)
        {
            printf("%s\n", targetDir);
            //开始写入到输出文件
            FILE *outfile = fopen(targetDir, "w+");
            if (outfile)
            {
                fwrite(bs, bytesReaden, 1, outfile);
                free(bs);
                while (bytesReaden > 0)
                {
                    printf("%d\n", bytesReaden);
                    bs = malloc(filelen);
                    error = afc_file_read(client, filehandle, bs, filelen, &bytesReaden);
                    if (error == 0)
                    {
                        fwrite(bs, bytesReaden, 1, outfile);
                    }
                    free(bs);
                }
                //关闭文件读写
                fclose(outfile);
            }
        }
        if (error == 0)
            error = afc_file_close(client, filehandle);
        else
            error = afc_file_close(client, filehandle);
    }

    return error;
}
int getFileSize(char *filename)
{
    struct stat statbuf;
    stat(filename, &statbuf);
    int size = statbuf.st_size;
    return size;
}
char *execCommand(char *cmd)
{
    char *ks[10];
    int num = 0;
    split(cmd, " ", ks, &num);
    if (num > 0)
    {
        char *command = ks[0];
        if (strcmp(command, "connect") == 0)
        {
            appid = ks[2];
            udid = ks[4];

            printf("%s----%s\n", appid, udid);
            if (udid != NULL && appid != NULL)
            {
                getAFCClient(udid, appid, &afcclient);
            }
            if (afcclient == NULL)
            {
                return "No Devices\n";
            }
            else
            {
                saveAu();
                return "0";
            }
        }
        else if (strcmp(command, "mkdir") == 0)
        {
            char *todir = malloc(sizeof(char *) * 200);
            strcpy(todir, ks[1]);

            char tempstr[1];
            strncpy(tempstr, todir, 1);
            afc_error_t afc_err = afc_make_directory(afcclient, todir);
            if (afc_err == 0)
                return "0";
            else
                return "Fail";
        }
        else if (strcmp(command, "push") == 0)
        {
            char *pdir = ks[1];
            char *tdir = ks[2];

            afc_error_t afc_err = copyFileToDevice(afcclient, pdir, tdir);

            printf("%d\n", afc_err);

            if (afc_err == 0)
                return "0";
            else if (afc_err == 8)
            {
                //应用已经重新安装了
                return "reboot";
            }
            else
                return "Fail\n";
        }
        else if (strcmp(command, "pull") == 0)
        {
            char *pdir = ks[1];
            char *tdir = ks[2];

            afc_error_t afc_err = copyFileFromDevice(afcclient, pdir, tdir);

            if (afc_err == 0)
                return "0";
            else
                return "Failed\n";
        }
    }
    return "Failed\n";
}
//异常退出
void signal_crash_handler(int sig)
{
    //server_backtrace(sig);
    exit(-1);
}
//正常退出
void signal_exit_handler(int sig)
{
    exit(0);
}
/************************************************************************************************************************
1、int socket(int family,int type,int protocol)
family:
    指定使用的协议簇:AF_INET（IPv4） AF_INET6（IPv6） AF_LOCAL（UNIX协议） AF_ROUTE（路由套接字） AF_KEY（秘钥套接字）
type:
    指定使用的套接字的类型:SOCK_STREAM（字节流套接字） SOCK_DGRAM
protocol:
    如果套接字类型不是原始套接字，那么这个参数就为0
2、int bind(int sockfd, struct sockaddr *myaddr, int addrlen)
sockfd:
    socket函数返回的套接字描述符
myaddr:
    是指向本地IP地址的结构体指针
myaddrlen:
    结构长度
struct sockaddr{
    unsigned short sa_family; //通信协议类型族AF_xx
    char sa_data[14];  //14字节协议地址，包含该socket的IP地址和端口号
};
struct sockaddr_in{
    short int sin_family; //通信协议类型族
    unsigned short int sin_port; //端口号
    struct in_addr sin_addr; //IP地址
    unsigned char si_zero[8];  //填充0以保持与sockaddr结构的长度相同
};
3、int connect(int sockfd,const struct sockaddr *serv_addr,socklen_t addrlen)
sockfd:
    socket函数返回套接字描述符
serv_addr:
    服务器IP地址结构指针
addrlen:
    结构体指针的长度
4、int listen(int sockfd, int backlog)
sockfd:
    socket函数绑定bind后套接字描述符
backlog:
    设置可连接客户端的最大连接个数，当有多个客户端向服务器请求时，收到此值的影响。默认值20
5、int accept(int sockfd,struct sockaddr *cliaddr,socklen_t *addrlen)
sockfd:
    socket函数经过listen后套接字描述符
cliaddr:
    客户端套接字接口地址结构
addrlen:
    客户端地址结构长度
6、int send(int sockfd, const void *msg,int len,int flags)
7、int recv(int sockfd, void *buf,int len,unsigned int flags)
sockfd:
    socket函数的套接字描述符
msg:
    发送数据的指针
buf:
    存放接收数据的缓冲区
len:
    数据的长度，把flags设置为0
*************************************************************************************************************************/
int main(int argc, char *argv[])
{
    //读取当前设备
    readAu();
    if (udid != NULL && appid != NULL)
    {
        getAFCClient(udid, appid, &afcclient);
    }
    //执行本地命令
    if(afcclient!=NULL)
    {
        printf("connect success\n");
        char *cmd=readFileContent("cmd.txt");
        if(cmd)
        {
            execCommand(cmd);
            printf("%s\n",cmd);
            //删除cmd.txt
            remove("cmd.txt");
        }
        else
        {
            printf("No Command\n");
        }
    }
    else
    {
        remove("au.txt");
    }

    atexit(signal_exit_handler);
    signal(SIGTERM, signal_exit_handler);
    signal(SIGINT, signal_exit_handler);

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    signal(SIGBUS, signal_crash_handler);  // 总线错误
    signal(SIGSEGV, signal_crash_handler); // SIGSEGV，非法内存访问
    signal(SIGFPE, signal_crash_handler);  // SIGFPE，数学相关的异常，如被0除，浮点溢出，等等
    signal(SIGABRT, signal_crash_handler); // SIGABRT，由调用abort函数产生，进程非正常退出

    int fd, numbytes, i;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    char buff[BUFSIZ];

    bzero(&(server_addr), sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    server_addr.sin_port=htons(8080);
    int len=sizeof(server_addr);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    int result=0;
    while((result=bind(fd, (struct sockaddr *)&server_addr, len))==-1);
    printf("1010101010==$$$%d\n",result);
    
    int exit = 0;
    while (1)
    {
        printf("%d\n",numbytes);
        while ((numbytes = recvfrom(fd,buff,BUFSIZ,0,(struct sockaddr *)&server_addr,&len)) > 0)
        {
            buff[numbytes] = '\0';
            printf("command：%s\n", buff);

            //先将命令转换为char *
            int len = numbytes;
            char *cmd = malloc(len * sizeof(char *));
            for (int i = 0; i < len; i++)
            {
                char c = buff[i];
                sprintf(cmd, "%s%c", cmd, c);
            }
            //转换完毕
            char *result = execCommand(buff);
            if (strcmp(result, "reboot") == 0)
            {
                writeFileContent("cmd.txt", cmd);
                free(cmd);
                exit = 1;
                break;
            }
            free(cmd);
            if (sendto(fd,result,strlen(result) * sizeof(result),0,(struct sockaddr *)&server_addr,sizeof(server_addr)) < 0)
            {
                exit = 1;
                break;
            }
        }
        if (exit == 1)
        {
            break;
        }
        sleep(1);
    }
    close(fd);
    if (exit != 0)
        return exit;
    return 0;
}
