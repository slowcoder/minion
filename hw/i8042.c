#include <stdio.h>
#include <stdint.h>

#include "hw/io.h"
#include "caos/log.h"

#include "hw/i8042.h"

static struct {
	uint8_t status;
	uint8_t command_byte;

	uint8_t buf_output[8];
	int     ndx_output;

	uint8_t cmdparams[8];
	int     cmdparams_left;
	uint8_t cmdparams_cmd;

	uint8_t kbdparams[8];
	int     kbdparams_left;
	uint8_t kbdparams_cmd;

	uint8_t control;
} regs;


static void handle_command(uint8 cmd) {
	switch(cmd) {
		case 0x20: // Read command byte
			regs.buf_output[0] = regs.command_byte;
			regs.ndx_output = 0;
			regs.status |= (1<<0); // Flag output buffer full
			break;
		case 0x60: // Write command byte
			regs.cmdparams_left = 1;
			regs.cmdparams_cmd = cmd;
			break;
		case 0xD1: // Write next byte to controller output port
			regs.cmdparams_left = 1;
			regs.cmdparams_cmd = cmd;
			break;
		case 0xFF: // ??
			break;
		default:
			LOG("Unhandled command 0x%02x",cmd);
			break;
	}
}

static void handle_command_withparams(uint8_t cmd,uint8_t *pParams) {
  // NOTE!! Parameters are in reverse order..

  if( cmd == 0x60 ) { // Write command byte
    regs.command_byte = pParams[0];
  } else if( cmd == 0xD3 ) { // Write next byte to first PS/2 port input buffer
    // do nothing..
  } else if( cmd == 0xD4 ) { // Write next byte to second PS/2 port input buffer
    // do nothing..
  } else if( cmd == 0xD1 ) { // Write next byte to controller output port
    //ASSERT(0,"Val=0x%02x",pParams[0]);
  } else {
    ASSERT(0,"Implement me (cmd=0x%02x)",cmd);
  }
}

static uint8_t kbd_inb(struct io_handler *hdl,uint16_t port) {
  port += hdl->base;

  if( port == 0x64 ) { // Read status register
  	return 0x14;
  	ASSERT(0,"Implement read-status");
    //LOG(" Status is 0x%02x (CCB=0x%02x, XLTon=%i)",regs.status,regs.command_byte,(regs.command_byte>>6)&1);
//    return regs.status;
  } else if( port == 0x60 ) { // Read data
  	ASSERT(0,"Implement read-data");
  } else if( port == 0x61 ) { // Port B
  	return regs.status;
  }

  ASSERT(0,"Unimplemented read from 0x%04x",port);
  return 0xFF;
}

static uint16_t kbd_inw(struct io_handler *hdl,uint16_t port) {
  ASSERT(0,"Unimplemented read from 0x%04x",hdl->base + port);
  return 0x0000;
}

static uint32_t kbd_inl(struct io_handler *hdl,uint16_t port) {
  ASSERT(0,"Unimplemented read from 0x%04x",hdl->base + port);
  return 0x00000000;
}

static void  kbd_outb(struct io_handler *hdl,uint16_t port,uint8_t val) {
	port += hdl->base;

	if( (port == 0x60) && (regs.cmdparams_left > 0) ) { // Parameters to a command
		regs.cmdparams[regs.cmdparams_left - 1] = val;
		regs.cmdparams_left--;
		if( regs.cmdparams_left <= 0 )
			handle_command_withparams(regs.cmdparams_cmd,regs.cmdparams);
		return;
	} else if( port == 0x61 ) { // Port B
		return;
	} else if( port == 0x64 ) { // Command port
    	handle_command(val);
    	return;
	}
	ASSERT(0,"Unimplemented write to 0x%04x",port);
}

static void  kbd_outw(struct io_handler *hdl,uint16_t port,uint16_t val) {
  ASSERT(0,"Unimplemented write to 0x%04x",hdl->base + port);
}

static void  kbd_outl(struct io_handler *hdl,uint16_t port,uint32_t val) {
  ASSERT(0,"Unimplemented write to 0x%04x",hdl->base + port);
}


int i8042_init(void) {
	static struct io_handler hdl;

	hdl.base   = 0x0060;
	hdl.mask   = 0xFFF8;
	hdl.inb    = kbd_inb;
	hdl.inw    = kbd_inw;
	hdl.inl    = kbd_inl;
	hdl.outb   = kbd_outb;
	hdl.outw   = kbd_outw;
	hdl.outl   = kbd_outl;
	io_register_handler(&hdl);

	regs.status = 0x14;

	return 0;
}