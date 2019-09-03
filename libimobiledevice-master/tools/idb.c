//
//  main.m
//  testlibimobiledevice
//
//  Created by lixing on 2018/6/26.
//  Copyright © 2018年 lixing. All rights reserved.
//
#include <pthread.h>
#include <libimobiledevice/house_arrest.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/libimobiledevice.h>

#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/notification_proxy.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

idevice_t device = NULL;
char *curudid = NULL;
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
int command_completed = 0;
char *last_status = NULL;
int err_occurred = 0;
int notified = 0;
char *options = NULL;
int notification_expected = 0;
int wait_for_command_complete = 0;
int is_device_connected = 0;
static void print_apps(plist_t apps)
{
    uint32_t i = 0;
    for (i = 0; i < plist_array_get_size(apps); i++)
    {
        plist_t app = plist_array_get_item(apps, i);
        plist_t p_bundle_identifier = plist_dict_get_item(app, "CFBundleIdentifier");
        char *s_bundle_identifier = NULL;
        char *s_display_name = NULL;
        char *s_version = NULL;
        plist_t display_name = plist_dict_get_item(app, "CFBundleDisplayName");
        plist_t version = plist_dict_get_item(app, "CFBundleVersion");

        if (p_bundle_identifier)
        {
            plist_get_string_val(p_bundle_identifier, &s_bundle_identifier);
        }
        if (!s_bundle_identifier)
        {
            fprintf(stderr, "ERROR: Failed to get APPID!\n");
            break;
        }

        if (version)
        {
            plist_get_string_val(version, &s_version);
        }
        if (display_name)
        {
            plist_get_string_val(display_name, &s_display_name);
        }
        if (!s_display_name)
        {
            s_display_name = strdup(s_bundle_identifier);
        }

        /* output app details */
        printf("%s", s_bundle_identifier);
        if (s_version)
        {
            printf(", \"%s\"", s_version);
            free(s_version);
        }
        printf(", \"%s\"", s_display_name);

        printf("\n");
        free(s_display_name);
        free(s_bundle_identifier);
    }
}
static void status_cb(plist_t command, plist_t status, void *unused)
{
    if (command && status)
    {
        char *command_name = NULL;
        instproxy_command_get_name(command, &command_name);

        /* get status */
        char *status_name = NULL;
        instproxy_status_get_name(status, &status_name);

        if (status_name)
        {
            if (!strcmp(status_name, "Complete"))
            {
                command_completed = 1;
            }
        }

        /* get error if any */
        char *error_name = NULL;
        char *error_description = NULL;
        uint64_t error_code = 0;
        instproxy_status_get_error(status, &error_name, &error_description, &error_code);

        /* output/handling */
        if (!error_name)
        {
            if (!strcmp(command_name, "Browse"))
            {
                uint64_t total = 0;
                uint64_t current_index = 0;
                uint64_t current_amount = 0;
                plist_t current_list = NULL;
                instproxy_status_get_current_list(status, &total, &current_index, &current_amount, &current_list);
                if (current_list)
                {
                    print_apps(current_list);
                    plist_free(current_list);
                }
            }
            else if (status_name)
            {
                /* get progress if any */
                int percent = -1;
                instproxy_status_get_percent_complete(status, &percent);

                if (last_status && (strcmp(last_status, status_name)))
                {
                    printf("\n");
                }

                if (percent >= 0)
                {
                    printf("\r%s: %s (%d%%)", command_name, status_name, percent);
                }
                else
                {
                    printf("\r%s: %s", command_name, status_name);
                }
                if (command_completed)
                {
                    printf("\n");
                }
            }
        }
        else
        {
            /* report error to the user */
            if (error_description)
                fprintf(stderr, "ERROR: %s failed. Got error \"%s\" with code : %s\n", command_name, error_name, error_description ? error_description : "N/A");
            else
                fprintf(stderr, "ERROR: %s failed. Got error \"%s\".\n", command_name, error_name);
            err_occurred = 1;
        }

        /* clean up */
        free(error_name);
        free(error_description);

        free(last_status);
        last_status = status_name;

        free(command_name);
        command_name = NULL;
    }
    else
    {
        fprintf(stderr, "ERROR: %s was called with invalid arguments!\n", __func__);
    }
}
static void notifier(const char *notification, void *unused)
{
    notified = 1;
}
static void idevice_event_callback(const idevice_event_t *event, void *userdata)
{
    if (event->event == IDEVICE_DEVICE_REMOVE)
    {
        if (!strcmp(curudid, event->udid))
        {
            fprintf(stderr, "ideviceinstaller: Device removed\n");
            is_device_connected = 0;
        }
    }
}
static void idevice_wait_for_command_to_complete()
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000;
    is_device_connected = 1;

    /* subscribe to make sure to exit on device removal */
    idevice_event_subscribe(idevice_event_callback, NULL);

    /* wait for command to complete */
    while (wait_for_command_complete && !command_completed && !err_occurred && !notified && is_device_connected)
    {
        nanosleep(&ts, NULL);
    }

    /* wait some time if a notification is expected */
    while (notification_expected && !notified && !err_occurred && is_device_connected)
    {
        nanosleep(&ts, NULL);
    }

    idevice_event_unsubscribe();
}
void getAppList(char *udid)
{
    curudid = udid;
    lockdownd_client_t client = NULL;
    instproxy_client_t ipc = NULL;
    instproxy_error_t err;
    np_client_t np = NULL;
    lockdownd_service_descriptor_t service = NULL;
    int res = 0;
    char *bundleidentifier = NULL;

    if (device == NULL)
    {
        idevice_new(&device, udid);
        if (!device)
        {
            goto leave_cleanup;
        }
    }
    if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake(device, &client, "ideviceinstaller"))
    {
        fprintf(stderr, "Could not connect to lockdownd. Exiting.\n");
        res = -1;
        goto leave_cleanup;
    }

    if ((lockdownd_start_service(client, "com.apple.mobile.notification_proxy",
                                 &service) != LOCKDOWN_E_SUCCESS) ||
        !service)
    {
        fprintf(stderr,
                "Could not start com.apple.mobile.notification_proxy!\n");
        res = -1;
        goto leave_cleanup;
    }

    np_error_t nperr = np_client_new(device, service, &np);

    if (service)
    {
        lockdownd_service_descriptor_free(service);
    }
    service = NULL;

    if (nperr != NP_E_SUCCESS)
    {
        fprintf(stderr, "Could not connect to notification_proxy!\n");
        res = -1;
        goto leave_cleanup;
    }

    np_set_notify_callback(np, notifier, NULL);

    const char *noties[3] = {NP_APP_INSTALLED, NP_APP_UNINSTALLED, NULL};

    np_observe_notifications(np, noties);

    if (service)
    {
        lockdownd_service_descriptor_free(service);
    }
    service = NULL;

    if ((lockdownd_start_service(client, "com.apple.mobile.installation_proxy",
                                 &service) != LOCKDOWN_E_SUCCESS) ||
        !service)
    {
        fprintf(stderr,
                "Could not start com.apple.mobile.installation_proxy!\n");
        res = -1;
        goto leave_cleanup;
    }

    err = instproxy_client_new(device, service, &ipc);

    if (service)
    {
        lockdownd_service_descriptor_free(service);
    }
    service = NULL;

    if (err != INSTPROXY_E_SUCCESS)
    {
        fprintf(stderr, "Could not connect to installation_proxy!\n");
        res = -1;
        goto leave_cleanup;
    }

    setbuf(stdout, NULL);

    free(last_status);
    last_status = NULL;

    notification_expected = 0;

    int xml_mode = 0;
    plist_t client_opts = instproxy_client_options_new();
    instproxy_client_options_add(client_opts, "ApplicationType", "User", NULL);
    plist_t apps = NULL;

    if (!xml_mode)
    {
        instproxy_client_options_set_return_attributes(client_opts,
                                                       "CFBundleIdentifier",
                                                       "CFBundleDisplayName",
                                                       "CFBundleVersion",
                                                       NULL);
    }
    err = instproxy_browse_with_callback(ipc, client_opts, status_cb, NULL);
    if (err == INSTPROXY_E_RECEIVE_TIMEOUT)
    {
        fprintf(stderr, "NOTE: timeout waiting for device to browse apps, trying again...\n");
    }
    instproxy_client_options_free(client_opts);
    if (err != INSTPROXY_E_SUCCESS)
    {
        fprintf(stderr, "ERROR: instproxy_browse returned %d\n", err);
        res = -1;
        goto leave_cleanup;
    }
    wait_for_command_complete = 1;
    notification_expected = 0;
    lockdownd_client_free(client);
    client = NULL;
    idevice_wait_for_command_to_complete();

leave_cleanup:
    np_client_free(np);
    instproxy_client_free(ipc);
    lockdownd_client_free(client);
    idevice_free(device);
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
    lockdownd_client_free(lockdownd_client);

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
    unsigned char *bs = malloc(bysize * sizeof(unsigned char *));
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
// 计时线程，等待超过一定时间推出程序
int timerThreadExit;
void timerThread(void)
{
    sleep(1);
    if (timerThreadExit == 1)
    {
        exit(0);
    }
}
void sendMsg(char *char_send)
{
    
    int clientSocket, numbytes;
    char buf[BUFSIZ];
    struct sockaddr_in their_addr;

    bzero(&(their_addr), sizeof(their_addr));
    their_addr.sin_family = AF_INET;
    their_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    their_addr.sin_port = htons(8020);

label1:

    while ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1);
    
    int len = sizeof(their_addr);

    if(connect(clientSocket, (struct sockaddr *)&their_addr,len) == -1)
    {
       close(clientSocket);
       clientSocket=NULL;
       sleep(1);
       goto label1;
    }

    int datalen = strlen(char_send) * sizeof(char *);


    if((numbytes = send(clientSocket, char_send, datalen, 0))<=0)
    {
        close(clientSocket);
        clientSocket=NULL;
        sleep(1);
        goto label1;
    }

    printf("sending %d\n",numbytes);

    if((numbytes = recv(clientSocket, buf, BUFSIZ, 0))<=0)
    {
        close(clientSocket);
        clientSocket=NULL;
        sleep(1);
        goto label1;
    }

    printf("receiving %d\n",numbytes);

    buf[numbytes] = '\0';
    printf("%s\n", buf);
    if (strcmp(buf,"0")!=0)
    {
        goto label1;
    }

    close(clientSocket);
}
int main(int argc, const char *args[])
{
    if (argc > 1)
    {
        const char *command = args[1];
        if (strcmp(command, "devices") == 0)
        {
            char **dev_list = NULL;
            getDeviceList(&dev_list);
            for (int i = 0; dev_list[i] != NULL; i++)
            {
                printf("%s\n", dev_list[i]);
            }
            freeDeviceList(dev_list);
        }
        else if (strcmp(command, "devicename") == 0)
        {
            if (argc >= 2)
            {
                if (argc >= 3)
                {
                    if (strcmp(args[2], "-u") == 0)
                    {
                        char *udid = (char *)args[3];

                        char **dev_list = NULL;
                        getDeviceList(&dev_list);
                        int res = 0;
                        for (int i = 0; dev_list[i] != NULL; i++)
                        {
                            if (strcmp(dev_list[i], udid) == 0)
                            {
                                char *devicename = NULL;
                                getDeviceName(dev_list[i], &devicename);
                                printf("%s\n", devicename);
                                res = 1;
                                break;
                            }
                        }
                        freeDeviceList(dev_list);
                        if (res == 0)
                        {
                            printf("No Device\n");
                        }
                    }
                }
                else
                {
                    char **dev_list = NULL;
                    getDeviceList(&dev_list);

                    int res = 0;
                    for (int i = 0; dev_list[i] != NULL; i++)
                    {
                        if (i == 0)
                        {
                            char *udid = dev_list[i];
                            char *devicename = NULL;
                            getDeviceName(udid, &devicename);
                            printf("%s\n", devicename);
                            res = 1;
                            break;
                        }
                    }
                    freeDeviceList(dev_list);
                    if (res == 0)
                    {
                        printf("No Device\n");
                    }
                }
            }
        }
        else if (strcmp(command, "connect") == 0)
        {
            char *udid = NULL;
            char *appid = NULL;
            for (int i = 2; i < argc; i++)
            {
                char *arg = (char *)args[i];
                if (strcmp(arg, "-u") == 0)
                {
                    i++;
                    udid = (char *)args[i];
                }
                else if (strcmp(arg, "-appid") == 0)
                {
                    i++;
                    appid = (char *)args[i];
                }
            }
            char *cmd = malloc((strlen(udid) + strlen(appid) + 2 + strlen(command)) * sizeof(char *));
            sprintf(cmd, "%s -appid %s -u %s", command, appid, udid);

            printf("send  %s\n", cmd);

            sendMsg(cmd);
            free(cmd);
        }
        else if (strcmp(command, "push") == 0 ||
                 strcmp(command, "pull") == 0||
                 strcmp(command,"mkdir")==0)
        {
            char *cmd=NULL;
            printf("%d\n",argc);
            if(argc==4)
            {
                char *c1 = args[2];
                char *c2 = args[3];
                cmd = malloc((strlen(c1) + strlen(c2) + 2 + strlen(command)) * sizeof(char *));
                sprintf(cmd, "%s %s %s", command, c1, c2);
            }
            else if(argc==3)
            {
                char *c1 = args[2];
                cmd = malloc((strlen(c1) + 1 + strlen(command)) * sizeof(char *));
                sprintf(cmd, "%s %s", command, c1);
            }

            printf("send  %s\n", cmd);

            sendMsg(cmd);
            free(cmd);
        }
        else if (strcmp(command, "-l") == 0)
        {
            char *udid = NULL;
            for (int i = 2; i < argc; i++)
            {
                char *arg = (char *)args[i];
                if (strcmp(arg, "-u") == 0)
                {
                    i++;
                    udid = (char *)args[i];
                }
            }
            if (udid == NULL)
            {
                char **dev_list = NULL;
                getDeviceList(&dev_list);

                int res = 0;
                for (int i = 0; dev_list[i] != NULL; i++)
                {
                    if (i == 0)
                    {
                        udid = malloc(sizeof(char *) * strlen(dev_list[i]));
                        strcpy(udid, dev_list[i]);
                        break;
                    }
                }
                freeDeviceList(dev_list);
            }
            printf("%s\n", udid);
            getAppList(udid);
        }
        else if (strcmp(command, "shell") == 0)
        {
            char *udid = NULL;
            char *appid = NULL;
            for (int i = 2; i < argc; i++)
            {
                char *arg = (char *)args[i];
                if (strcmp(arg, "-u") == 0)
                {
                    i++;
                    udid = (char *)args[i];
                }
                else if (strcmp(arg, "-appid") == 0)
                {
                    i++;
                    appid = (char *)args[i];
                }
            }
            if (udid == NULL)
            {
                char **dev_list = NULL;
                getDeviceList(&dev_list);

                int res = 0;
                for (int i = 0; dev_list[i] != NULL; i++)
                {
                    if (i == 0)
                    {
                        udid = malloc(sizeof(char *) * strlen(dev_list[i]));
                        strcpy(udid, dev_list[i]);
                        break;
                    }
                }
                freeDeviceList(dev_list);
            }
            afc_client_t client = NULL;
            if (udid != NULL && appid != NULL)
            {
                getAFCClient(udid, appid, &client);
            }
            if (client == NULL)
            {
                printf("No Devices\n");
                return 0;
            }

            char *currdir = malloc(sizeof(char *) * 200);
            strcpy(currdir, "/");

            while (1 == 1)
            {
                printf("%s$", currdir);

                char *ks[10];
                char keyword[1000];
                fgets(keyword, 1000, stdin);
                for (int i = 0; i < 1000; i++)
                {
                    if (keyword[i] == '\n')
                    {
                        keyword[i] = '\0';
                        break;
                    }
                }

                int num = 0;
                split(keyword, " ", ks, &num);
                char *kscommand = ks[0];
                if (strcmp(kscommand, "ls") == 0)
                {
                    char **dirlist1 = NULL;
                    afc_error_t afc_err = afc_read_directory(client, currdir, &dirlist1);
                    if (afc_err == AFC_E_SUCCESS)
                    {
                        for (int i = 0; dirlist1[i] != NULL; i++)
                        {
                            char *dir = dirlist1[i];
                            printf("%s\n", dir);
                        }
                    }
                }
                else if (strcmp(kscommand, "clear") == 0)
                {
                    system("clear");
                }
                else if (strcmp(kscommand, "exit") == 0)
                {
                    break;
                }
                else if (strcmp(kscommand, "del") == 0)
                {
                    char *todir = malloc(sizeof(char *) * 200);
                    strcpy(todir, ks[1]);

                    char tempstr[1];
                    strncpy(tempstr, todir, 1);
                    if (strcmp(tempstr, "/") != 0)
                    {
                        char *tempdir2 = malloc(sizeof(char *) * 200);
                        sprintf(tempdir2, "%s%s", currdir, todir);
                        free(todir);
                        todir = malloc(sizeof(char *) * 200);
                        strcpy(todir, tempdir2);
                        free(tempdir2);
                    }
                    afc_error_t afc_err = afc_remove_path_and_contents(client, todir);
                    if (afc_err == 0)
                        printf("Success\n");
                    else
                        printf("Failed %d\n", afc_err);
                }
                else if (strcmp(kscommand, "mkdir") == 0)
                {
                    char *todir = malloc(sizeof(char *) * 200);
                    strcpy(todir, ks[1]);

                    char tempstr[1];
                    strncpy(tempstr, todir, 1);
                    if (strcmp(tempstr, "/") != 0)
                    {
                        char *tempdir2 = malloc(sizeof(char *) * 200);
                        sprintf(tempdir2, "%s%s", currdir, todir);
                        free(todir);
                        todir = malloc(sizeof(char *) * 200);
                        strcpy(todir, tempdir2);
                        free(tempdir2);
                    }
                    afc_error_t afc_err = afc_make_directory(client, todir);
                    if (afc_err == 0)
                        printf("Success\n");
                    else
                        printf("Failed %d\n", afc_err);
                }
                else if (strcmp(kscommand, "push") == 0)
                {
                    char *pdir = ks[1];
                    char *tdir = ks[2];

                    afc_error_t afc_err = copyFileToDevice(client, pdir, tdir);

                    if (afc_err == 0)
                        printf("Success\n");
                    else if (afc_err == 8)
                    {
                        //应用已经重新安装了
                        client = NULL;
                        getAFCClient(udid, appid, &client);
                        if (client == NULL)
                        {
                            printf("No Devices\n");
                            break;
                        }
                        printf("开始拷贝\n");
                        afc_err = copyFileToDevice(client, pdir, tdir);
                        if (afc_err == 0)
                            printf("Success\n");
                        else
                        {
                            printf("Fail\n");
                            break;
                        }
                    }
                    else
                        printf("Failed %d\n", afc_err);
                }
                else if (strcmp(kscommand, "pull") == 0)
                {
                    char *pdir = ks[1];
                    char *tdir = ks[2];

                    afc_error_t afc_err = copyFileFromDevice(client, pdir, tdir);

                    if (afc_err == 0)
                        printf("Success\n");
                    else
                        printf("Failed %d\n", afc_err);
                }
                else if (strcmp(kscommand, "cd") == 0)
                {
                    char *todir = malloc(sizeof(char *) * 200);
                    strcpy(todir, ks[1]);

                    if (strcmp(todir, "../") == 0)
                    {
                        if (strcmp(currdir, "/") != 0)
                        {
                            char *tempdir = malloc(sizeof(char *) * 200);

                            strcpy(tempdir, "/");

                            char *strs[10];
                            num = 0;

                            split(currdir, "/", strs, &num);

                            for (int i = 0; i < num - 1; i++)
                            {
                                printf("%s\n", strs[i]);
                                char *tempdir2 = malloc(sizeof(char *) * 200);
                                sprintf(tempdir2, "%s%s/", tempdir, strs[i]);
                                free(tempdir);
                                tempdir = malloc(sizeof(char *) * 200);
                                strcpy(tempdir, tempdir2);
                                free(tempdir2);
                            }

                            strcpy(currdir, tempdir);
                            free(tempdir);
                        }
                    }
                    else
                    {
                        if (strcmp(currdir, todir) != 0)
                        {
                            if (dirIsExists(client, currdir, todir))
                            {
                                char tempstr[1];
                                strncpy(tempstr, todir, 1);
                                if (strcmp(tempstr, "/") == 0)
                                {
                                    strcpy(currdir, todir);
                                }
                                else
                                {
                                    char *tempdir = malloc(sizeof(char *) * 200);
                                    sprintf(tempdir, "%s%s/", currdir, todir);
                                    strcpy(currdir, tempdir);
                                    free(tempdir);
                                }
                                free(todir);
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
