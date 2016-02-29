/*
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * $Id: ped_script.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file ped_script.c
 * @brief Configures the router through operation script
 * 
 * These functions provide high-level operations to manipulate the configuration
 * in the router related to attaching and removing filters to interfaces.
 * 
 */

#include <sync/common.h>
#include <sys/wait.h>
#include "ped_script.h"


/*** Constants ***/


/*** Data structures ***/


/*** STATIC/INTERNAL Functions ***/


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Execute operation script
 * 
 * @param[in] cmd
 *     CLI command to execute operation script
 * 
 * @return
 *      0 if operation script didn't return any error message, otherwise -1
 */
int
exec_op_script(char *cmd)
{
    int    fd[2];
    pid_t  pid;
    char buf[64];
    int len = 0;
    int ret = 0;

    if(pipe(fd) < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Create pipe failed!", __func__);
        return -1;
    }

    if((pid = fork()) < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: fork failed!", __func__);
        close(fd[0]);
        close(fd[1]);
        ret = -1;
    }
    else if(pid > 0) {    /* parent */
        close(fd[1]);       /* close write end */

        if(waitpid(pid, NULL, 0) >= 0) {
            buf[0] = 0;
            len = read(fd[0], buf, 64);
            if(len >= 0) {
                buf[len] = 0;
            }
            if(strncmp(buf, "error", 5) == 0) {
                ERRMSG(PED, TRACE_LOG_ERR,
                       "%s: Execute op script failed: %s",
                       __func__, buf);
               ret = -1;
            }
        }
        else {
            ERRMSG(PED, TRACE_LOG_ERR, 
                   "%s: waitpid error!", __func__);
            ret = -1;
        }
        close(fd[0]);
    }
    else {    /* child */
        close(fd[0]);   /* close read end */
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);   /* don't need this after dup2 */

        if(execl("/usr/sbin/cli", "cli", "-c", cmd, NULL) < 0) {
            ERRMSG(PED, TRACE_LOG_ERR, 
                   "%s: execl error!", __func__);
        }
        exit(0);
    }

    return ret;
}
