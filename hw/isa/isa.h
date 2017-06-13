#pragma once

#include <stdint.h>

typedef struct isa_handler {
  uint16_t  base,mask;
  void     *opaque;
  uint8_t  (*inb )(struct isa_handler *,uint16_t address);
  uint16_t (*inw )(struct isa_handler *,uint16_t address);
  uint32_t (*inl )(struct isa_handler *,uint16_t address);
  void     (*outb)(struct isa_handler *,uint16_t address,uint8_t val);
  void     (*outw)(struct isa_handler *,uint16_t address,uint16_t val);
  void     (*outl)(struct isa_handler *,uint16_t address,uint32_t val);

  char *pzDesc;
} isa_handler_t;

int hw_isa_init(void);
int hw_isa_register_handler(isa_handler_t *handler);
int hw_isa_io_out(uint16_t port,int datalen,void *pData);
int hw_isa_io_in(uint16_t port,int datalen,void *pData);
