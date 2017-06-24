#include <string.h>
#include "caos/log.h"

#include "hw/isa/isa.h"

#define MAX_HANDLERS 10

static int isahandlers = 0;
static isa_handler_t *isahandler[MAX_HANDLERS] = { 0 };

int hw_isa_init(void) {

	isahandlers = 0;
	memset(isahandler,0,sizeof(isahandler));
	return 0;
}

static inline isa_handler_t *lookup_handler(uint16 address) {
	static uint32_t i;

	for(i=0;i<isahandlers;i++) {
		if( (address & isahandler[i]->mask) == isahandler[i]->base )  {
			return isahandler[i];
		}
	}
	return NULL;
}

int hw_isa_io_out(uint16_t port,int datalen,void *pData) {
	isa_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		if(      (datalen == 1) && (handler->outb != NULL) ) {
			handler->outb(handler,port,*(uint8_t*)pData);
			return 0;
		} else if( (datalen == 2) && (handler->outw != NULL) ) {
			handler->outw(handler,port,*(uint16_t*)pData);
			return 0;
		} else if( (datalen == 4) && (handler->outl != NULL) ) {
			handler->outl(handler,port,*(uint32_t*)pData);
			return 0;
		} else {
			ASSERT(0,"Handler found, but no CB for datalen=%i (port=0x%04x)",datalen,port);
			return -1;
		}
	}

	LOGW("No handler for writes to port 0x%04x (datalen=%i)",port,datalen);

	return -1;
}

int hw_isa_io_in(uint16_t port,int datalen,void *pData) {
	isa_handler_t *handler;

	handler = lookup_handler(port);

	if( handler != NULL ) {
		if(      (datalen == 1) && (handler->inb != NULL) ) {
			*(uint8_t*)pData = handler->inb(handler,port);
			return 0;
		} else if( (datalen == 2) && (handler->inw != NULL) ) {
			*(uint16_t*)pData = handler->inw(handler,port);
			return 0;
		} else if( (datalen == 4) && (handler->inl != NULL) ) {
			*(uint32_t*)pData = handler->inl(handler,port);
			return 0;
		} else {
			ASSERT(0,"Handler found, but no CB for datalen=%i (port=0x%04x)",datalen,port);
			return -1;
		}
	}

	LOGW("No handler for reads from port 0x%04x",port);

	return -1;
}

int hw_isa_register_handler(isa_handler_t *handler) {
	LOG("Registering IO handler for 0x%04x (Mask 0x%04x)",handler->base,handler->mask);

	if( isahandlers < (MAX_HANDLERS-1) ) {
		isahandler[isahandlers] = handler;
		isahandlers++;
		return 0;
	}
	ASSERT(0,"Too few slots for ISA handlers");
	return -1;
}
