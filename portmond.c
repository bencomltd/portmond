/********************************************************************
* Description:
* Author: Peter Bennett <bencomltd16@>
* Created at: Sat Jul 27 16:01:02 NZST 2019
* Computer: raspberrypi
* System: Linux 4.19.57-v7+ on armv7l
*
* Copyright (c) 2019 root  All rights reserved.
*
********************************************************************/
/*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/* Daemonize code base */
/*
MIT/Expat License

Copyright (c) 2016-2018 Artem Boldariev <artem.boldarev@gmail.com>

See the LICENSE.txt for details about the terms of use.

Upstream URL: https://bitbucket.org/arbv/daemonize
*/

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <syslog.h>
#include <zconf.h>

#include "daemonize.h"

#include <pigpio.h>

#include <curl/curl.h>

#define MAX_GPIOS 32
#define OPT_S_DEF 5

char fullUri[100]={0};
// Full path http://host.name.ip.addr/input/year-month-day/hour:minute:second/port/level/tick
char uri[100];
char conf_ports;
char *ports_split;
int logit;
int ports_count = 0;
int ports[MAX_GPIOS] = {255};
//"http://192.168.1.3/input";
static char *conf_file_name = "/etc/portmond/portmon.conf";
/**
 * Read configuration file
 */
int read_conf_file(int reload)
{
    FILE *conf_file = NULL;
    int ret = -1;
    conf_file = fopen(conf_file_name, "r");
    if (conf_file == NULL) {
        syslog(LOG_ERR, "Can not open config file: %s, error: %s",
        conf_file_name, strerror(errno));
        return -1;
    }
    ret = fscanf(conf_file, "%s", uri);
    if (ret > 0) {
        if (reload == 1) {
            syslog(LOG_INFO, "Reloaded configuration file %s", conf_file_name);
        } else {
            syslog(LOG_INFO, "Configuration read from file %s",  conf_file_name);
        }
    }
    int i = 0;
    ret = fscanf(conf_file, "%s", &conf_ports);
    ports_split = strtok (&conf_ports,",:");
    while (ports_split != NULL) {
        ports[i] = atoi(ports_split);
        i++;
        ports_split = strtok (NULL, ",:");
    }
    ports_count = i;
    ret = fscanf(conf_file, "%d", &logit);
    fclose(conf_file);
    return ret;
}
// Web get URI
int curl_get(char *uri) {
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, uri);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK)
            syslog(LOG_ERR, "Portmond daemon curl get failed %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }
    return 0;
}
// Gpio monitoring callback
void monitor(int gpio, int level, uint32_t tick)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%d/%u", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, gpio, level, tick);
    if (logit)
        syslog(LOG_INFO, "URI %s", fullUri);
     curl_get(fullUri);
}
/* The daemon process body */
static int portmon_daemon(void *udata)
{
    int exit = 0;
    int exit_code = EXIT_SUCCESS;
    int sfd = -1;
    sigset_t mask;
    struct signalfd_siginfo si;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    int i;
    logit = 0;
    read_conf_file(0);
    /* open the system log */
    openlog("Portmond", LOG_NDELAY, LOG_DAEMON);
    syslog(LOG_INFO, "Portmond daemon started. PID: %ld Logging %d", (long)getpid(), logit);

    /* create a file descriptor for signal handling */
    sigemptyset(&mask);
    /* handle the following signals */
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGUSR1);
    /* Block the signals so that they aren't handled
       according to their default dispositions */
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    {
        closelog();
        return EXIT_FAILURE;
    }
    sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd == -1)
    {
        syslog(LOG_ERR, "Could not open signal file descriptor!");
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        closelog();
        return EXIT_FAILURE;
    }
    int flags;
    flags = fcntl(sfd,F_GETFL,0);
    fcntl(sfd, F_SETFL, flags | O_NONBLOCK);

    if (gpioInitialise()<0) {
        syslog(LOG_INFO, "Portmond Pi GPIO Failed to Initialize. Is pigpiod running?");
        return EXIT_FAILURE;
    }

    int tick = 86400;
    int tick2;
    int level = 0;
    struct timeval waitd = {1, 0};
    //  16 Ports  17,18,27,22,23,24,25,5,6,12,13,19,16,26,20,21 example
    for (i=0; i<ports_count; i++) gpioSetPullUpDown(ports[i], PI_PUD_UP);
    for (i=0; i<ports_count; i++) gpioSetAlertFunc(ports[i], monitor);
    /* the daemon loop */
    while (!exit) {
        int result;
        fd_set readset;
        /* add the signal file descriptor to set */
        FD_ZERO(&readset);
        FD_SET(sfd, &readset);
        t = time(NULL);
        tm = *localtime(&t);
        if (tm.tm_hour == 0) {
            if (tm.tm_min == 0) {
                if (tm.tm_sec == 0) {
                    for (i=0; i<ports_count; i++) {
                        level = gpioRead(ports[i]);
                        sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%d/%u", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ports[i], level, tick);
                        curl_get(fullUri);
                    }
                    syslog(LOG_INFO, "Watchdog port watch done:");
                    sleep(1);
                }
            }
        }
        if ((tm.tm_hour != 23) && (tm.tm_min != 59) && (tm.tm_sec <= 55))
            sleep(4);
        /* non blocking wait for the data in the signal file descriptor */
        result = select(FD_SETSIZE, &readset, NULL, NULL, &waitd);
        if(result == 0) {
            continue;
        }
        if (result == -1) {
            syslog(LOG_ERR, "Fatal error during select() call.");
            /* a low level error */
            exit_code = EXIT_FAILURE;
            break;
        }
        /* read the data from the signal handler file descriptor */
        if (FD_ISSET(sfd, &readset) && read(sfd, &si, sizeof(si)) > 0)
        {
            /* handle the signals */
            switch (si.ssi_signo)
            {
                case SIGTERM: /* stop the daemon */
                    syslog(LOG_INFO, "Got SIGTERM signal. Stopping daemon...");
                    exit = 1;
                    break;
                case SIGHUP: /* reload the configuration */
                    syslog(LOG_INFO, "Configuration file reload.");
                    read_conf_file(1);
                    break;
                case SIGUSR1: /* reload the configuration */
                    syslog(LOG_INFO, "User Triggered port check.");
                    t = time(NULL);
                    tm = *localtime(&t);
                    tick2 = gpioTick();
                    for (i=0; i<ports_count; i++) {
                        level = gpioRead(ports[i]);
                        sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%d/%u", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ports[i], level, tick2);
                        curl_get(fullUri);
                    }
                    break;
                default:
                    syslog(LOG_WARNING, "Got unexpected signal (number: %d).", si.ssi_signo);
                    break;
            }
        }
    }
    gpioTerminate();
    /* close the signal file descriptor */
    close(sfd);
    /* remove the signal handlers */
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    /* write an exit code to the system log */
    syslog(LOG_INFO, "Portmond daemon stopped with status code %d.", exit_code);
    /* close the system log */
    closelog();
    return exit_code;
}


int main(int argc, char **argv)
{
    int exit_code = 0;

    if (getuid()!=0){
        printf("Please run the script as root user!\r\n");
        return EXIT_FAILURE;
    } 

    pid_t pid = rundaemon(0, /* Daemon creation flags. */
                          portmon_daemon, NULL, /* Daemon body function and its argument. */
                          &exit_code, /* Pointer to a variable to receive daemon exit code */
                          "/var/run/portmond.pid"); /* Full path to the PID-file (lock). */
    switch (pid)
    {
        case -1: /* Low level error. See errno for details. */
        {
            perror("Cannot start daemon.");
            return EXIT_FAILURE;
        }
        break;
        case -2: /* Daemon is already running */
        {
            fprintf(stderr,"Daemon already running.\n");
        }
        break;
        case 0: /* Daemon process. */
        {
            return exit_code; /* Return the daemon exit code. */
        }
        default: /* Parent process */
        {
            printf("Parent: %ld, Daemon: %ld\n", (long)getpid(), (long)pid);
        }
        break;
    }

    return EXIT_SUCCESS;
}

