#include "kernel_proc.h"
#include "kernel_streams.h"

typedef struct processInfo_content_block{
  procinfo procinfo;
  int pcb_cursor;
} procinfo_cb;


int procinfo_read(void* procinfo_cb, char *buf, unsigned int size);
int procinfo_close(void* procinfo_cb);
void take_ProcessInfo(procinfo* procinfo, PCB* pcb);

void* procinfo_open(unsigned int file_id);
int procinfo_write(void* procinfo_cb, const char* BUFFER, unsigned int size);

static file_ops procinfo_ops = {
  .Open = NULL,
  .Read = procinfo_read,
  .Write = NULL,
  .Close = procinfo_close
};