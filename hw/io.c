#include <stdlib.h>

#include "caos/log.h"

#include "hw/io.h"

#define MAX_HANDLERS 128

static int iohandlers = 0;
static io_handler_t *iohandler[MAX_HANDLERS] = { 0 };

typedef struct iohdl {
  int foo;
} iohdl_t;

struct iohdl *io_register_handler(io_handler_t *handler) {
	LOG("Registering IO handler for 0x%04x (Mask 0x%04x)",handler->base,handler->mask);

	if( iohandlers < (MAX_HANDLERS-1) ) {
		iohandler[iohandlers] = handler;
		iohandlers++;
		return (iohdl_t*)&iohandler[iohandlers-1];
	}
	return NULL;
}

static io_handler_t *lookup_handler(uint16 address) {
	static uint32_t i;

	for(i=0;i<iohandlers;i++) {
		//    LOG("Addr=0x%x  ioh.m=0x%x  ioh.b=0x%x",address,iohandler[i]->mask,iohandler[i]->base);
		if( (address & iohandler[i]->mask) == iohandler[i]->base )  {
			//LOGD("Returning handler %p (Base=0x%x,Mask=0x%x,Desc=%s) for req addr 0x%x",iohandler[i],iohandler[i]->base,iohandler[i]->mask,iohandler[i]->pzDesc,address);
			return iohandler[i];
		}
	}
	return NULL;
}


int io_init(void) {
	return 0;
}

uint8_t  io_inb(uint16_t port) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		ASSERT(handler->inb != NULL,"NULL handler for inb @ 0x%04x",port);
		return handler->inb(handler,port - handler->base);
	}

	ASSERT(handler != NULL,"No handler for reads from port 0x%04x",port);

	return ~0;
}

uint16_t io_inw(uint16_t port) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		ASSERT(handler->inw != NULL,"NULL handler for inw @ 0x%04x",port);
		return handler->inw(handler,port - handler->base);
	}

	ASSERT(handler != NULL,"No handler for reads from port 0x%04x",port);

	return ~0;
}

uint32_t io_inl(uint16_t port) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		ASSERT(handler->inl != NULL,"NULL handler for inl @ 0x%04x",port);
		return handler->inl(handler,port - handler->base);
	}

	ASSERT(handler != NULL,"No handler for reads from port 0x%04x",port);

	return ~0;
}

void     io_outb(uint16_t port,uint8_t val) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	//LOGE("outb @ 0x%04x, val=0x%x",port,val);

	if( handler != NULL ) {
		ASSERT(handler->outb != NULL,"NULL handler for outb @ 0x%04x",port);
		handler->outb(handler,port - handler->base,val);
		return;
	}

	LOGW("No handler for writes to port 0x%04x (Val=0x%02x)",port,val);

}

void     io_outw(uint16_t port,uint16_t val) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		ASSERT(handler->outw != NULL,"NULL handler for outw @ 0x%04x",port);
		handler->outw(handler,port - handler->base,val);
		return;
	}

	LOGW("No handler for writes to port 0x%04x (Val=0x%02x)",port,val);
}

void     io_outl(uint16_t port,uint32_t val) {
	io_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		ASSERT(handler->outl != NULL,"NULL handler for outl @ 0x%04x",port);
		handler->outl(handler,port - handler->base,val);
		return;
	}

	LOGW("No handler for writes to port 0x%04x (Val=0x%02x)",port,val);
}
