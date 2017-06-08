#pragma once

#include <stdint.h>

typedef struct io_handler {
  uint16_t  base,mask;
  void     *opaque;
  uint8_t  (*inb )(struct io_handler *,uint16_t address);
  uint16_t (*inw )(struct io_handler *,uint16_t address);
  uint32_t (*inl )(struct io_handler *,uint16_t address);
  void     (*outb)(struct io_handler *,uint16_t address,uint8_t val);
  void     (*outw)(struct io_handler *,uint16_t address,uint16_t val);
  void     (*outl)(struct io_handler *,uint16_t address,uint32_t val);

  char *pzDesc;
} io_handler_t;

struct iohdl *io_register_handler(io_handler_t *handler);
int io_init(void);

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  io_inb(uint16_t port);
uint16_t io_inw(uint16_t port);
uint32_t io_inl(uint16_t port);
void     io_outb(uint16_t port,uint8_t val);
void     io_outw(uint16_t port,uint16_t val);
void     io_outl(uint16_t port,uint32_t val);

#ifdef __cplusplus
}
#endif
