#ifndef __DMA_H
#define __DMA_H

#include "mmio/dmg.hpp"

// TODO
class ObjAttrDMA {
public:
  ObjAttrDMA();
  DMA::DMA *const get_dma_reg();

private:
  DMA::DMA dma_;
};

#endif //__DMA_H
