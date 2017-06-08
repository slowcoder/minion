#include <stdint.h>
#include <time.h>
#include <sys/time.h>


#include "hw/io.h"
#include "caos/log.h"

#include "hw/cmos.h"

#define DEC2BCD(x) ( ((x/10)<<4) | (x % 10) )

#define BS_STATUSB_BCDBIN 2
#define BS_STATUSB_24H    1

typedef struct {
  uint8_t indexreg;

	struct {
		int divider;
		int rate;

		uint8_t statusB;
		uint8_t statusC;
		uint8_t statusD;
	} rtc;

} ctx_t;

/*
 * Gets the current (local) time for RTC representation
 */
static struct tm *getTime(void) {
  time_t rawtime;
  struct tm * timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  return timeinfo;
}

uint8 bin2bcd(ctx_t *pCtx,uint8 val) {
  if( pCtx->rtc.statusB & (1<<BS_STATUSB_BCDBIN) )
    return val;

  val = DEC2BCD(val);
  return val;
}


static uint8_t cmos_inb(struct io_handler *hdl,uint16_t port) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;
	uint8_t ret;

	//LOG("CMOS read from register 0x%x",pCtx->indexreg);

	if( pCtx->indexreg == 0x00 ) { // RTC - Seconds
		struct tm *tiin = getTime();
		ret = tiin->tm_sec;
		//LOG("Returned SECOND=%u",ret);
		ret = bin2bcd(pCtx,ret);
		return ret;
	} else if( pCtx->indexreg == 0x02 ) { // RTC - Minutes
		struct tm *tiin = getTime();
		ret = tiin->tm_min;
		//LOG("Returned MINUTE=%u",ret);
		ret = bin2bcd(pCtx,ret);
		return ret;
	} else if( pCtx->indexreg == 0x04 ) {
		struct tm *tiin = getTime();
		ret = tiin->tm_hour;
		if( !(pCtx->rtc.statusB & (1<<BS_STATUSB_24H)) ) { // 24-hour time?
			if( ret > 12 ) {
				ret = 0x80 | bin2bcd(pCtx,ret-12);
				return ret;
			}
		}
		ret = bin2bcd(pCtx,ret);
		return ret;
	} else if( pCtx->indexreg == 0x07 ) { // RTC - Day in month
		struct tm *tiin = getTime();
		ret = tiin->tm_mday;
		//LOG("Returned DAY=%u",ret);
		ret = bin2bcd(pCtx,ret);
		return ret;
	} else if( pCtx->indexreg == 0x08 ) { // RTC - Month
		struct tm *tiin = getTime();
		ret = tiin->tm_mon + 1;
		//LOG("Returned MONTH=%u",ret);
		ret = bin2bcd(pCtx,ret);
		return ret;
	} else if( pCtx->indexreg == 0x09 ) { // RTC - Year (Since 1900 or 2000)
		struct tm *tiin = getTime();
		ret = tiin->tm_year;
		if( ret >= 100 ) ret -= 100;
		//LOG("Returned YEAR=%u",ret);
		ret = bin2bcd(pCtx,ret);
		return ret;    
	} else if( pCtx->indexreg == 0x0A ) { // Status register A (Time freq div/rate)
		ret = 0;

		pCtx->rtc.divider = 32768;
		pCtx->rtc.rate    = 1024;

		if( pCtx->rtc.rate == 1024 )
			ret |= 6;
		else
			ASSERT(0,"Lazy coder error");
		if( pCtx->rtc.divider == 32768 )
			ret |= 1<<5;
		else
			ASSERT(0,"Lazy coder error");

		return ret;
	} else if( pCtx->indexreg == 0x0B ) { // RTC Status reg B
		return pCtx->rtc.statusB;
	} else if( pCtx->indexreg == 0x0C ) { // RTC Status reg C
		return pCtx->rtc.statusC;
	} else {
		LOG("CMOS read (unahandled) from register 0x%x",pCtx->indexreg);
	}

	return 0xFF;
}

static void  cmos_outb(struct io_handler *hdl,uint16_t port,uint8_t val) {
	ctx_t *pCtx = (ctx_t*)hdl->opaque;

	if( port == 0x00 ) {
		pCtx->indexreg = val & ~0x80; // Top bit determines if NMI is enabled or not
		return;
	} else {
		LOG("CMOS write to register 0x%x",pCtx->indexreg);
	}
}

int cmos_init(void) {
	static struct io_handler hdl;
	static ctx_t ctx;

	hdl.base   = 0x70;
	hdl.mask   = 0xFE;
	hdl.inb    = cmos_inb;
	hdl.outb   = cmos_outb;
	hdl.opaque = &ctx;
	io_register_handler(&hdl);

	return 0;
}