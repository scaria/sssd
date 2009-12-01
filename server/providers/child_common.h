/*
    SSSD

    Common helper functions to be used in child processes

    Authors:
        Sumit Bose   <sbose@redhat.com>

    Copyright (C) 2009 Red Hat

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __CHILD_COMMON_H__
#define __CHILD_COMMON_H__

#include <sys/types.h>
#include <sys/wait.h>
#include <tevent.h>

#define IN_BUF_SIZE         512
#define MAX_CHILD_MSG_SIZE  255

struct response {
    size_t max_size;
    size_t size;
    uint8_t *buf;
};

uint8_t *copy_buffer_and_add_zero(TALLOC_CTX *mem_ctx,
                                  const uint8_t *src, size_t len);

/* Async communication with the child process via a pipe */
struct read_pipe_state;

struct tevent_req *read_pipe_send(TALLOC_CTX *mem_ctx,
                                  struct tevent_context *ev, int fd);

int read_pipe_recv(struct tevent_req *req, TALLOC_CTX *mem_ctx,
                   uint8_t **buf, ssize_t *len);

/* The pipes to communicate with the child must be nonblocking */
void fd_nonblocking(int fd);

void child_sig_handler(struct tevent_context *ev,
                       struct tevent_signal *sige, int signum,
                       int count, void *__siginfo, void *pvt);

#endif /* __CHILD_COMMON_H__ */