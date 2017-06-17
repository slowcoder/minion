#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>

#include "vm/vm_internal.h"
#include "hw/isa/isa.h"
//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "hw/isa/i8250.h"

typedef struct {
	int con_out,con_in;

	struct {
		uint8_t rbr,thr,dll; // base + 0
		uint8_t ier,dlm; // base + 1
		uint8_t iir,fcr; // base + 2
		uint8_t lcr; // base + 3
		uint8_t mcr; // base + 4
		uint8_t lsr; // base + 5
		uint8_t msr; // base + 6
		uint8_t scr; // base = 7
	} reg;
} ctx_t;

#define IER_THREMPTY (1<<1)

#define DLAB ((pCtx->reg.lcr&(1<<7))>>7)

static void output_truncate(void) {
	FILE *fd;

	fd = fopen("uartlog.txt","wt");
	if( fd != NULL ) {
		fclose(fd);
	}
}

static void output_append(ctx_t *pCtx,uint8_t b) {
	FILE *fd;

	if( !isascii(b) )
		return;

	fd = fopen("uartlog.txt","at");
	if( fd != NULL ) {
		fwrite(&b,1,1,fd);
		fflush(fd);
		fclose(fd);
	}
	if( pCtx->con_out >= 0 ) {
		write(pCtx->con_out,&b,1);
	}
}

static void check_for_irqs(ctx_t *pCtx) {
	static volatile int irq_set = 0;
	int irq_pending;

	irq_pending = 0;

	LOGD("IER_THREMPTY=0x%x",pCtx->reg.ier & IER_THREMPTY);
	if( pCtx->reg.ier & IER_THREMPTY ) {
		pCtx->reg.iir = (1<<1); // THR empty
		pCtx->reg.iir |= (1<<0); // Interrupt pending
		pCtx->reg.iir &= ~(1<<0); // Interrupt pending (0=pending IRQ)
		irq_pending = 1;
	}

	if( irq_pending && !irq_set ) {
		intvm_irq_set(4,1);
		irq_set = 1;
	} else if( !irq_pending && irq_set ) {
		pCtx->reg.iir = 0;
		intvm_irq_set(4,0);
		irq_set = 0;
	}
}

static uint8_t i8250_inb(struct isa_handler *hdl,uint16_t port) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	port -= hdl->base;

	LOGD("Read from port 0x%04x (DLAB=%i)",port,DLAB);

	if( (port == 0) && (DLAB == 0) ) { // RBR
		return 0x00;
	} else if( (port == 1) && (DLAB == 0) ) { // IER
		return pCtx->reg.ier;
	} else if( (port == 2) && (DLAB == 0) ) { // IIR
		uint8_t ret;

		ret = pCtx->reg.iir;
		// Reset THREMPY in IIR
		pCtx->reg.iir &= ~(1<<1);
		check_for_irqs(pCtx);

		return ret;
	} else if( port == 3 ) { // LCR
		return pCtx->reg.lcr;
	} else if( port == 4 ) { // MCR
		return pCtx->reg.mcr;
	} else if( (port == 5) && (DLAB == 0) ) { // LSR
		pCtx->reg.lsr &= ~(1<<0); // Reset data available
		pCtx->reg.lsr |= (1<<5); // THR is empty
		pCtx->reg.lsr |= (1<<6); // THR is empty + Line idle
		return pCtx->reg.lsr;
	} else if( (port == 6) && (DLAB == 0) ) { // MSR
		return pCtx->reg.msr;
	} else if( port == 7 ) { // SCR
//		pCtx->reg.scr = 0x00; // Invalidate SCR, trying to be a 8250A
		return pCtx->reg.scr;
	} else {
		ASSERT(0,"Unhandled read from port 0x%04x (DLAB=%i)",port,DLAB);
	}

	return 0xFF;
}

static void  i8250_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	port -= hdl->base;

	LOGD("Write to port 0x%04x (DLAB=%i,Val=0x%02x)",port,DLAB,val);

	if( (port == 0) && (DLAB == 0) ) { // THR

		output_append(pCtx,val);

		// Reset THREMPY in IIR
		pCtx->reg.iir &= ~(1<<1);

		check_for_irqs(pCtx);
	} else if( (port == 0) && (DLAB == 1) ) { // DLL
		pCtx->reg.dll = val;
	} else if( (port == 1) && (DLAB == 0) ) { // IER
		LOGD("IER=0x%02x",val);
		pCtx->reg.ier = val;

	} else if( (port == 1) && (DLAB == 1) ) { // DLM
		pCtx->reg.dlm = val;
	} else if( port == 2 ) { // FCR
		pCtx->reg.fcr = val;
		LOGD("EnFIFO=%i ClRXFIFO=%i ClTXFIFO=%i DMAMODE=%i FIFOwmark=%x",
			val&1,
			(val>>1)&1,
			(val>>2)&1,
			(val>>3)&1,
			(val>>6)&3);
		if( (val&1) == 0 ) {
			pCtx->reg.fcr = 0;
		} else {
			pCtx->reg.fcr = 0;
		}
	} else if( port == 3 ) { // LCR
		pCtx->reg.lcr = val;
		LOGD("%i%c%i DLAB=%i",5+(val&3),val&(1<<3) ? '?' : 'N',((val>>2)&1)+1,val>>7);
	} else if( port == 4 ) { // MCR
		pCtx->reg.mcr = val;
		ASSERT( (val&(1<<4)) == 0,"Loopback not implemented");
	} else if( port == 7 ) { // SCR
		pCtx->reg.scr = val;
		pCtx->reg.scr = 0; // Invalidate SCR, trying to be a 8250A
	} else {
		ASSERT(0,"Unimplemented write @ 0x%04x (DLAB=%i, val=0x%02x)",port,DLAB,val);
	}
//	check_for_irqs(pCtx);
}

static uint8_t i8250_dummy_inb(struct isa_handler *hdl,uint16_t port) {
	return 0;
}

static void  i8250_dummy_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
}

int hw_isa_i8250_init(struct vm *pVM) {
	static struct isa_handler hdl[4];
	static ctx_t ctx = {0};

	hdl[0].base   = 0x3F8;
	hdl[0].mask   = ~7;
	hdl[0].inb    = i8250_inb;
	hdl[0].outb   = i8250_outb;
	hdl[0].opaque = &ctx;
	hw_isa_register_handler(&hdl[0]);

	hdl[1].base   = 0x2F8;
	hdl[1].mask   = ~7;
	hdl[1].inb    = i8250_dummy_inb;
	hdl[1].outb   = i8250_dummy_outb;
	hdl[1].opaque = NULL;
	hw_isa_register_handler(&hdl[1]);

	hdl[2].base   = 0x3E8;
	hdl[2].mask   = ~7;
	hdl[2].inb    = i8250_dummy_inb;
	hdl[2].outb   = i8250_dummy_outb;
	hdl[2].opaque = NULL;
	hw_isa_register_handler(&hdl[2]);

	hdl[3].base   = 0x2E8;
	hdl[3].mask   = ~7;
	hdl[3].inb    = i8250_dummy_inb;
	hdl[3].outb   = i8250_dummy_outb;
	hdl[3].opaque = NULL;
	hw_isa_register_handler(&hdl[3]);

	output_truncate();

	intvm_get_console(pVM,&ctx.con_out,&ctx.con_in);

	return 0;
}