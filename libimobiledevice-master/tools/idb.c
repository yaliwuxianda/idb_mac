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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
void getAFCClient(char *udid, char *appid, afc_client_t *afc_client)
{
    int *error = malloc(sizeof(int));

    idevice_t device = NULL;
    idevice_new(&device, udid);
    if (!device)
    {
        *error = -10;
        goto l_device;
    }

    lockdownd_client_t lockdownd_client = NULL;
    lockdownd_error_t lockdownd_err = LOCKDOWN_E_UNKNOWN_ERROR;

    lockdownd_err = lockdownd_client_new_with_handshake(device, &lockdownd_client, "handshake");
    if (lockdownd_err != LOCKDOWN_E_SUCCESS)
    {
        *error = -10;
        goto l_device;
    }
    //    lockdownd_client_free(lockdownd_client);

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
//    free(status);
l_list:
    printf("");
//    plist_free(dict);
l_house:
    printf("");
//    house_arrest_client_free(client);
l_device:
    printf("");
    //    idevice_free(device);
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
afc_error_t copyFileToDevice(afc_client_t client, char *sourceFilePath, char *targetDir)
{
    afc_error_t error = 0;

    FILE *infile;
    infile = fopen(sourceFilePath, "rb");
    unsigned char *bs = malloc(sizeof(unsigned char *) * 102400);
    if (infile == NULL)
        error = 1;
    int filelen = fread(bs, sizeof(unsigned char), 102400, infile);
    fclose(infile);

    error = 0;

    long filehandle = 0;

    if (error == 0)
    {
        printf("%s\n", sourceFilePath);
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
