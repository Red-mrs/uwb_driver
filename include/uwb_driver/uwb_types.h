#pragma once

#include <cstdint>

namespace uwb_driver
{

/* Bookkeeping for the per-link message statistics printed by the maintainer. */
struct msg_counter
{
  uint32_t init;
  uint32_t resp;
  uint16_t num;
};

/*
 * Range report as sent by the UWB module over LLCP.
 *
 * The module sends the image of its own unpacked uwb_range_t, which the ARM
 * toolchain pads to 16 bytes: the six address bytes are followed by two padding
 * bytes before the uint32 distance. The padding is spelled out here rather than
 * left to the host compiler, so casting the LLCP payload to this type is
 * layout-correct on any target instead of only accidentally matching.
 */
struct range
{
  uint8_t  initiator_address_lo;
  uint8_t  initiator_address_hi;
  uint8_t  responder_address_lo;
  uint8_t  responder_address_hi;
  uint8_t  own_address_lo;
  uint8_t  own_address_hi;
  uint8_t  padding[2];
  uint32_t distance_mm;
  uint32_t number;
};

static_assert(sizeof(range) == 16, "range must match the module's 16-byte payload layout");

}  // namespace uwb_driver
