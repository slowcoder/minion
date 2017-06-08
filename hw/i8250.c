#include <stdio.h>
#include <stdint.h>

#include "hw/io.h"
#include "caos/log.h"

#include "hw/i8250.h"

typedef struct {
	uint8_t foo;
	FILE *out;
} ctx_t;


static uint8_t i8250_inb(struct io_handler *hdl,uint16_t port) {

	return 0xFF;
}

static void  i8250_outb(struct io_handler *hdl,uint16_t port,uint8_t val) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	if( port == 0x0 ) {
		pCtx->out = fopen("uartlog.txt","at");
		fwrite(&val,1,1,pCtx->out);
		fflush(pCtx->out);
		fclose(pCtx->out);
	}
}

static uint8_t i8250_dummy_inb(struct io_handler *hdl,uint16_t port) {
	return 0;
}

static void  i8250_dummy_outb(struct io_handler *hdl,uint16_t port,uint8_t val) {
}

int i8250_init(void) {
	static struct io_handler hdl[4];
	static ctx_t ctx;

	hdl[0].base   = 0x3F8;
	hdl[0].mask   = ~7;
	hdl[0].inb    = i8250_inb;
	hdl[0].outb   = i8250_outb;
	hdl[0].opaque = &ctx;
	io_register_handler(&hdl[0]);

	hdl[1].base   = 0x2F8;
	hdl[1].mask   = ~7;
	hdl[1].inb    = i8250_dummy_inb;
	hdl[1].outb   = i8250_dummy_outb;
	hdl[1].opaque = NULL;
	io_register_handler(&hdl[1]);

	hdl[2].base   = 0x3E8;
	hdl[2].mask   = ~7;
	hdl[2].inb    = i8250_dummy_inb;
	hdl[2].outb   = i8250_dummy_outb;
	hdl[2].opaque = NULL;
	io_register_handler(&hdl[2]);

	hdl[3].base   = 0x2E8;
	hdl[3].mask   = ~7;
	hdl[3].inb    = i8250_dummy_inb;
	hdl[3].outb   = i8250_dummy_outb;
	hdl[3].opaque = NULL;
	io_register_handler(&hdl[3]);

	ctx.out = fopen("uartlog.txt","wt");
	fclose(ctx.out);

	return 0;
}