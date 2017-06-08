#pragma once

#define PCI_CFG_REG_BAR0   0x10
#define PCI_CFG_REG_BAR1   0x14
#define PCI_CFG_REG_BAR2   0x18
#define PCI_CFG_REG_BAR3   0x1C
#define PCI_CFG_REG_BAR4   0x20
#define PCI_CFG_REG_BAR5   0x24
#define PCI_CFG_REG_EXPROM 0x30

#pragma pack(push,unpack)
#pragma pack(1)
typedef struct {
  uint16_t vendorid;
  uint16_t deviceid;
  uint16_t commandreg;
  uint16_t statusreg;
  uint32_t classandrev;
  uint8_t  cachelinesize;
  uint8_t  latencytimer;
  uint8_t  headertype;
  uint8_t  bist;
  uint32_t bar[6];
  uint32_t cardbus_cis;
  uint16_t subsys_vendorid;
  uint16_t subsys_id;
  uint32_t expansionrom;
  uint32_t pack[3];

  uint32_t padding[60];
} pcidev_config_t;
#pragma pack(pop)

typedef struct {
  uint8_t bus,dev,fn;

  pcidev_config_t conf;

  uint8_t  (*readb)(int reg);
  uint16_t (*readw)(int reg);
  uint32_t (*readl)(int reg);
  void     (*writeb)(int reg,uint8_t  val);
  void     (*writew)(int reg,uint16_t val);
  void     (*writel)(int reg,uint32_t val);
} pcidev_t;

int pci_init(void);
int pci_register_dev(int bus,const pcidev_t *dev);
