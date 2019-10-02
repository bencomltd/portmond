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

/*#define _POSIX_C_SOURCE 200809L*/
#define _XOPEN_SOURCE 700

/* header files */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/signal.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <syslog.h>
#include <zconf.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/i2c-dev.h>
#include "bsec_integration.h"

#include "daemonize.h"

#include <pigpio.h>

#include <curl/curl.h>

#include "bme680.h"

#define MAX_GPIOS 32
#define OPT_S_DEF 5
#define temp_offset (0.0f)
#define sample_rate_mode (BSEC_SAMPLE_RATE_LP)

int g_i2cFid; // I2C Linux device handle
int i2c_address = BME680_I2C_ADDR_SECONDARY;
static char *filename_state = "/etc/portmond/bsec_iaq.state";
static char *filename_config = "/etc/portmond/bsec_iaq.config";

char fullUri[210]={0};
char sensUri[210]={0};
// Full path eg http://host.name.ip.addr/input/year-month-day/hour:minute:second/port/level/tick
char uri[100];
char conf_ports;
char *ports_split;
int logit;
int ports_count = 0;
int ports[MAX_GPIOS] = {255};
static char *conf_file_name = "/etc/portmond/portmon.conf";

struct thread_info {            /* Used as argument to thread_start() */
    pthread_t thread_id;        /* ID returned by pthread_create() */
    int       thread_num;       /* Application-defined thread # */
    char     *argv_string;      /* From command-line argument */
};


// open the Linux device
void i2cOpenX()
{
  g_i2cFid = open("/dev/i2c-1", O_RDWR);
  if (g_i2cFid < 0) {
    syslog(LOG_ERR, "Failed To Open i2c-1");
  }
}

// close the Linux device
void i2cCloseX()
{
  close(g_i2cFid);
}

// set the I2C slave address for all subsequent I2C device transfers
void i2cSetAddress(int address)
{
  if (ioctl(g_i2cFid, I2C_SLAVE, address) < 0) {
    syslog(LOG_ERR, "Failed To Set I2C Address");
  }
}

/*
 * Write operation in either I2C or SPI
 *
 * param[in]        dev_addr        I2C or SPI device address
 * param[in]        reg_addr        register address
 * param[in]        reg_data_ptr    pointer to the data to be written
 * param[in]        data_len        number of bytes to be written
 *
 * return          result of the bus communication function
 */
int8_t bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data_ptr,
                 uint16_t data_len)
{
  int8_t rslt = 0; /* Return 0 for Success, non-zero for failure */
  uint8_t reg[16];
  reg[0]=reg_addr;
  int i;
  for (i=1; i<data_len+1; i++)
    reg[i] = reg_data_ptr[i-1];

  if (write(g_i2cFid, reg, data_len+1) != data_len+1) {
    perror("user_i2c_write");
    rslt = 1;
  }
  return rslt;
}

/*
 * Read operation in either I2C or SPI
 *
 * param[in]        dev_addr        I2C or SPI device address
 * param[in]        reg_addr        register address
 * param[out]       reg_data_ptr    pointer to the memory to be used to store
 *                                  the read data
 * param[in]        data_len        number of bytes to be read
 *
 * return          result of the bus communication function
 */
int8_t bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data_ptr,
                uint16_t data_len)
{
  int8_t rslt = 0; /* Return 0 for Success, non-zero for failure */
  uint8_t reg[1];
  reg[0]=reg_addr;
  if (write(g_i2cFid, reg, 1) != 1) {
    perror("user_i2c_read_reg");
    rslt = 1;
  }
  if (read(g_i2cFid, reg_data_ptr, data_len) != data_len) {
    perror("user_i2c_read_data");
    rslt = 1;
  }
  return rslt;
}

/*
 * System specific implementation of sleep function
 *
 * param[in]       t_ms    time in milliseconds
 *
 * return          none
 */
void _sleep(uint32_t t_ms)
{
  struct timespec ts;
  ts.tv_sec = 0;
  /* mod because nsec must be in the range 0 to 999999999 */
  ts.tv_nsec = (t_ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

/*
 * Capture the system time in microseconds
 *
 * return          system_current_time    system timestamp in microseconds
 */
int64_t get_timestamp_us()
{
  struct timespec spec;
  clock_gettime(CLOCK_MONOTONIC, &spec);
  int64_t system_current_time_ns = (int64_t)(spec.tv_sec) * (int64_t)1000000000
                                   + (int64_t)(spec.tv_nsec);
  int64_t system_current_time_us = system_current_time_ns / 1000;
  return system_current_time_us;
}

/*
 * Handling of the ready outputs
 *
 * param[in]       timestamp       time in microseconds
 * param[in]       iaq             IAQ signal
 * param[in]       iaq_accuracy    accuracy of IAQ signal
 * param[in]       temperature     temperature signal
 * param[in]       humidity        humidity signal
 * param[in]       pressure        pressure signal
 * param[in]       raw_temperature raw temperature signal
 * param[in]       raw_humidity    raw humidity signal
 * param[in]       gas             raw gas sensor signal
 * param[in]       bsec_status     value returned by the bsec_do_steps() call
 * param[in]       static_iaq      unscaled indoor-air-quality estimate
 * param[in]       co2_equivalent  CO2 equivalent estimate [ppm]
 * param[in]       breath_voc_equivalent  breath VOC concentration estimate [ppm]
 *
 * return          none
 */
void output_ready(int64_t timestamp, float iaq, uint8_t iaq_accuracy,
                  float temperature, float humidity, float pressure,
                  float raw_temperature, float raw_humidity, float gas,
                  bsec_library_return_t bsec_status,
                  float static_iaq, float co2_equivalent,
                  float breath_voc_equivalent) {

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(sensUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%.2f/%.0f/%.2f/%.15f/%.25f/%.2f/%.2f/%.2f/%d", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, 
                iaq_accuracy, iaq ,gas, static_iaq, co2_equivalent, breath_voc_equivalent, temperature, humidity, pressure / 100, bsec_status);
    if (logit)
        syslog(LOG_INFO, "URI %s", fullUri);
    curl_get(sensUri);
}

/*
 * Load binary file from non-volatile memory into buffer
 *
 * param[in,out]   state_buffer    buffer to hold the loaded data
 * param[in]       n_buffer        size of the allocated buffer
 * param[in]       filename        name of the file on the NVM
 * param[in]       offset          offset in bytes from where to start copying
 *                                  to buffer
 * return          number of bytes copied to buffer or zero on failure
 */
uint32_t binary_load(uint8_t *b_buffer, uint32_t n_buffer, char *filename,
                     uint32_t offset)
{
  int32_t copied_bytes = 0;
  int8_t rslt = 0;
  struct stat fileinfo;
  rslt = stat(filename, &fileinfo);
  if (rslt != 0) {
    syslog(LOG_ERR, "Error Stat'ing Binary File %s: ", filename);
    return 0;
  }
  uint32_t filesize = fileinfo.st_size - offset;
  if (filesize > n_buffer) {
    syslog(LOG_ERR, "%s: %d > %d\n", "Binary Data Is Bigger Than Buffer", filesize, n_buffer);
    return 0;
  } else {
    FILE *file_ptr;
    file_ptr = fopen(filename,"rb");
    if (!file_ptr) {
      syslog(LOG_ERR, "Failed To Open Binary File");
      return 0;
    }
    fseek(file_ptr,offset,SEEK_SET);
    copied_bytes = fread(b_buffer,sizeof(char),filesize,file_ptr);
    if (copied_bytes == 0) {
      syslog(LOG_ERR,"Binary File %s Is Empty", filename);
    }
    fclose(file_ptr);
    return copied_bytes;
  }
}

/*
 * Load previous library state from non-volatile memory
 *
 * param[in,out]   state_buffer    buffer to hold the loaded state string
 * param[in]       n_buffer        size of the allocated state buffer
 *
 * return          number of bytes copied to state_buffer or zero on failure
 */
uint32_t state_load(uint8_t *state_buffer, uint32_t n_buffer)
{
  int32_t rslt = 0;
  rslt = binary_load(state_buffer, n_buffer, filename_state, 0);
  return rslt;
}

/*
 * Save library state to non-volatile memory
 *
 * param[in]       state_buffer    buffer holding the state to be stored
 * param[in]       length          length of the state string to be stored
 *
 * return          none
 */
void state_save(const uint8_t *state_buffer, uint32_t length)
{
  FILE *state_w_ptr;
  state_w_ptr = fopen(filename_state,"wb");
  fwrite(state_buffer,length,1,state_w_ptr);
  fclose(state_w_ptr);
}

/*
 * Load library config from non-volatile memory
 *
 * param[in,out]   config_buffer    buffer to hold the loaded state string
 * param[in]       n_buffer         size of the allocated state buffer
 *
 * return          number of bytes copied to config_buffer or zero on failure
 */
uint32_t config_load(uint8_t *config_buffer, uint32_t n_buffer)
{
  int32_t rslt = 0;
  /*
   * Provided config file is 4 bytes larger than buffer.
   * Apparently skipping the first 4 bytes works fine.
   *
   */
  rslt = binary_load(config_buffer, n_buffer, filename_config, 4);
  return rslt;
}

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
    if (ret > 0) {
        if (reload == 1) {
            syslog(LOG_INFO, "Reloaded configuration file %s", conf_file_name);
        } else {
            syslog(LOG_INFO, "Configuration read from file %s",  conf_file_name);
        }
    }
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
/* BME680 Thread */
void * bme680(void * arg) {
    struct thread_info *tinfo = arg;
    char *uargv, *p;
    i2cOpenX();
    i2cSetAddress(i2c_address);
    return_values_init ret;
    ret = bsec_iot_init(sample_rate_mode, temp_offset, bus_write, bus_read, _sleep, state_load, config_load);
    if (ret.bme680_status) {
    /* Could not intialize BME680 */
        syslog(LOG_ERR, "Could Not Initialize BME680"); 
    } else if (ret.bsec_status) {
    /* Could not intialize BSEC library */
        syslog(LOG_ERR, "Could Not Initialize BSEC Library"); 
    }

  /* Call to endless loop function which reads and processes data based on
   * sensor settings.
   * State is saved every 10.000 samples, which means every 10.000 * 3 secs
   * = 500 minutes (depending on the config).
   *
   */
    bsec_iot_loop(_sleep, get_timestamp_us, output_ready, state_save, 10000);
    uargv = strdup("PThread Closed");
    if (uargv == NULL)
        syslog(LOG_ERR, "Failed To Duplicate String");
    i2cCloseX();
    return uargv;
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
    int status;
    struct thread_info *tinfo;
    pthread_attr_t attr;
    logit = 0;

    /* open the system log */
    openlog("Portmond", LOG_NDELAY, LOG_DAEMON);
    read_conf_file(0);
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
        syslog(LOG_ERR, "Could not block signals!");
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
        closelog();
        return EXIT_FAILURE;
    }
    int tick;
    int level = 0;
    struct timeval waitd = {1, 0};
    //  16 Ports  17,18,27,22,23,24,25,5,6,12,13,19,16,26,20,21 example
    for (i=0; i<ports_count; i++) gpioSetPullUpDown(ports[i], PI_PUD_UP);
    for (i=0; i<ports_count; i++) gpioSetAlertFunc(ports[i], monitor);
    int result;
    fd_set readset;
    /* Allocate memory for pthread_create() arguments */
    tinfo = calloc(1, sizeof(struct thread_info));
    if (tinfo == NULL)
        syslog(LOG_ERR, "Failed To Allocate Memory For Thread Information");
    /* Initialize thread creation attributes */
    status = pthread_attr_init(&attr);
    if (status != 0)
        syslog(LOG_ERR, "Failed to create thread attributes");
    syslog(LOG_INFO, "BME680 Thread Started:");
    tinfo[0].thread_num = 1;
    tinfo[0].argv_string = uri;
    status = pthread_create(&tinfo[0].thread_id, &attr, &bme680, &tinfo[0]);
    if (status)
        syslog(LOG_ERR, "Failed To Create BME680 Thread:");
    status = pthread_attr_destroy(&attr);
    if (status != 0)
        syslog(LOG_ERR, "Failed To Destroy Thread Attribute");
    sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%s", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (char *) "portmond");
    if (logit)
        syslog(LOG_INFO, "URI %s", fullUri);
    curl_get(fullUri);
    free(tinfo);
    /* the daemon loop */
    while (!exit) {
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
                        sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%d/%u", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ports[i], level, 86400);
                        curl_get(fullUri);
                    }
                    syslog(LOG_INFO, "Watchdog port watch done:");
                    sleep(1);
                }
            }
        }
        if ((tm.tm_hour != 23) && (tm.tm_min != 59) && (tm.tm_sec <= 55))
            sleep(4);
        else
        	sleep(1);
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
                    tick = gpioTick();
                    for (i=0; i<ports_count; i++) {
                        level = gpioRead(ports[i]);
                        sprintf(fullUri, "%s/%04d-%02d-%02d/%02d:%02d:%02d/%d/%d/%u", uri, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ports[i], level, tick);
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
                          "/run/portmond.pid"); /* Full path to the PID-file (lock). */
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
//            printf("Parent: %ld, Daemon: %ld\n", (long)getpid(), (long)pid);
        }
        break;
    }

    return EXIT_SUCCESS;
}

