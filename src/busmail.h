#ifndef BUSMAIL_H
#define BUSMAIL_H

#include <stdint.h>
#include "buffer.h"

#define PACKET_SIZE 500
#define NO_PF 0
#define PF 1


typedef struct {
	int fd;
	uint32_t size;
	uint8_t data[PACKET_SIZE];
} packet_t;

typedef struct __attribute__((__packed__)) 
{
	uint8_t frame_header;
	uint8_t program_id;
	uint8_t task_id;
	uint16_t mail_header;
	uint8_t mail_data[1];
} busmail_t;



void * busmail_new(int fd);
void * busmail_add_handler(void * _self, void (*app_handler)(packet_t *));
int busmail_get(void * _self, packet_t *p);
void packet_dump(packet_t *p);
void busmail_dispatch(void * _self);
int busmail_write(void * _self, void * event);
void busmail_send0(void * _self, uint8_t * data, int size);
void busmail_send(void * _self, uint8_t * data, int size);
void busmail_send_prog(void * _self, uint8_t * data, int size, int prog_id);

#endif /* BUSMAIL_H */
