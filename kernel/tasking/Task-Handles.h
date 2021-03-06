#pragma once

#include "kernel/tasking/Task.h"

Result task_fshandle_open(Task *task, int *handle_index, const char *path, OpenFlag flags);

Result task_fshandle_close(Task *task, int handle_index);

void task_fshandle_close_all(Task *task);

Result task_fshandle_select(
    Task *task,
    HandleSet *handles_set,
    int *selected_index,
    SelectEvent *selected_events,
    Timeout timeout);

Result task_fshandle_read(Task *task, int handle_index, void *buffer, size_t size, size_t *read);

Result task_fshandle_write(Task *task, int handle_index, const void *buffer, size_t size, size_t *written);

Result task_fshandle_seek(Task *task, int handle_index, int offset, Whence whence);

Result task_fshandle_tell(Task *task, int handle_index, Whence whence, int *offset);

Result task_fshandle_call(Task *task, int handle_index, IOCall request, void *args);

Result task_fshandle_stat(Task *task, int handle_index, FileState *stat);

Result task_fshandle_connect(Task *task, int *handle_index, const char *path);

Result task_fshandle_accept(Task *task, int handle_index, int *connection_handle_index);

Result task_fshandle_send(Task *task, int handle_index, const void *buffer, size_t size, size_t *sended);

Result task_fshandle_receive(Task *task, int handle_index, void *buffer, size_t size, size_t *received);

Result task_create_pipe(Task *task, int *reader_handle_index, int *writer_handle_index);

Result task_create_term(Task *task, int *master_handle_index, int *slave_handle_index);
