#include "memory/dma.hpp"
#include "memory/mmio/dmg.hpp"

ObjAttrDMA::ObjAttrDMA() : dma_(*this) {}
DMA::DMA *const ObjAttrDMA::get_dma_reg() { return &dma_; }
