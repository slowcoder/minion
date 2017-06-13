#include <string.h>
#include "caos/log.h"

#include "hw/isa/isa.h"
#include "hw/pci/pci.h"

#define STAT_PERR (1<<7)
#define STAT_TO   (1<<6)
#define STAT_MOBF (1<<5)
#define STAT_INH  (1<<4)
#define STAT_A2   (1<<3)
#define STAT_SYS  (1<<2)
#define STAT_IBF  (1<<1)
#define STAT_OBF  (1<<0)

#define CB_XLAT   (1<<7)
#define CB_nEN2   (1<<6)
#define CB_nEN    (1<<4)
#define CB_SYS    (1<<2)
#define CB_INT2   (1<<1)
#define CB_INT    (1<<0)

typedef struct {
	uint8_t status;
	uint8_t ob,ib; // Output buffer, Input buffer
	uint8_t ctr; // Control byte (not command to i8042)

	uint16_t pendingcmd;
} ctx_t;

static uint8_t i8042_inb(struct isa_handler *hdl,uint16_t port) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	// 0x60 = Read input buffer
	// 0x64 = Read status register

	if( port == 0x64 ) { // Status read
//		LOG("STAT read");
		return pCtx->status;
	} else if( port == 0x60 ) { // Read IB
		LOGD("Read IB (0x%02x)",pCtx->ib);
		return pCtx->ib;
	} else {
		LOGE("Unhandled read from 0x%04x",port);
	}
	return ~0;
}

static void  i8042_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	// 0x60 = Write to output buffer
	// 0x64 = Send command

	if( port == 0x60 ) { // Write to output buffer
		pCtx->ob = val;
		pCtx->status |= STAT_OBF;
		LOGD("OBF=0x%02x",val);

		if( (pCtx->pendingcmd>>8) ) {
			switch(pCtx->pendingcmd&0xFF) {
				case 0x60: // Write CTR
					pCtx->ctr = val;
					pCtx->status &= ~( STAT_OBF | STAT_IBF );
					break;
				case 0xD1: // Write output port
					break;
				default:
					LOGE("Unhandled parameterized command 0x%02x",pCtx->pendingcmd&0xFF);
					break;
			}
		}

	} else if( port == 0x64 ) { // Send command
		LOGD("CMD=0x%02x",val);
		pCtx->status &= ~STAT_OBF; // "Flush" OB

		switch(val) { // 0-param commands
			case 0x20: // Read CTR
				pCtx->ib = pCtx->ctr;
				pCtx->status |= STAT_OBF;
				return;
				break;
			default:
				break;
		}
		pCtx->pendingcmd = (1<<9) | val;
	} else {
		LOGE("Unhandled write to 0x%04x, val=0x%02x",port,val);
	}
}

int hw_isa_i8042_init(void) {
	static struct isa_handler hdl;
	static ctx_t ctx;

	memset(&ctx,0,sizeof(ctx));

	hdl.base   = 0x0060;
//	hdl.mask   = 0xFFF8;
	hdl.mask   = 0xFFFB; // 0x60, 0x64
	hdl.inb    = i8042_inb;
	hdl.outb   = i8042_outb;
	hdl.opaque = &ctx;
	hw_isa_register_handler(&hdl);

	ctx.status = STAT_SYS | STAT_INH;
	ctx.ctr    = CB_XLAT | CB_SYS;

	return 0;
}
