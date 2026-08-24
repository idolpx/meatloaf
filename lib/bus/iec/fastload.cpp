// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------
// The CRC values and M-E entry addresses below are taken from sd2iec's
// doscmd.c (Ingo Korb, GPL v2), where they were collected from real loaders.
// Keeping the numbering and the ordering identical to upstream is deliberate:
// it is the only practical way to stay comparable with a table nobody here can
// re-derive.
// -----------------------------------------------------------------------------

#include "fastload.h"

// -----------------------------------------------------------------------------
// CRC-16, reflected polynomial 0xA001
// -----------------------------------------------------------------------------

uint16_t iecCrc16Update(uint16_t crc, uint8_t data)
{
  crc ^= data;
  for(uint8_t i=0; i<8; i++)
    {
      if( crc & 1 )
        crc = (crc >> 1) ^ 0xA001;
      else
        crc = (crc >> 1);
    }

  return crc;
}


uint16_t iecCrc16Block(uint16_t crc, const uint8_t *data, size_t len)
{
  while( len-- ) crc = iecCrc16Update(crc, *data++);
  return crc;
}


#ifdef IEC_SUPPORT_SOFTLOAD

// -----------------------------------------------------------------------------
// Detection tables
// -----------------------------------------------------------------------------

struct IECFastLoadCrcEntry
{
  uint16_t crc;
  uint8_t  variant;
  uint8_t  rxtx;
};

static const IECFastLoadCrcEntry s_crcTable[] =
{
#ifdef IEC_FP_TURBODISK
  { 0x9c9f, IEC_FLV_TURBODISK,        IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_FC3
  { 0xdab0, IEC_FLV_FC3_LOAD,         IEC_FLRX_NONE            }, // Final Cartridge III
  { 0x973b, IEC_FLV_FC3_LOAD,         IEC_FLRX_NONE            }, // Final Cartridge III variation
  { 0x7e38, IEC_FLV_FC3_LOAD,         IEC_FLRX_NONE            }, // EXOS v3
  { 0x1b30, IEC_FLV_FC3_SAVE,         IEC_FLRX_NONE            }, // early CRC, lots of C64 code after it
  { 0x8b0e, IEC_FLV_FC3_SAVE,         IEC_FLRX_NONE            }, // variation
  { 0x9930, IEC_FLV_FC3_FREEZED,      IEC_FLRX_NONE            },
  { 0x0281, IEC_FLV_FC3_OLDFREEZED,   IEC_FLRX_FC3OF_PAL       }, // older freezed-file loader, PAL
  { 0xc196, IEC_FLV_FC3_OLDFREEZED,   IEC_FLRX_FC3OF_NTSC      }, // older freezed-file loader, NTSC
#endif
#ifdef IEC_FP_DREAMLOAD
  { 0x2e69, IEC_FLV_DREAMLOAD,        IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_ULOAD3
  { 0xdd81, IEC_FLV_ULOAD3,           IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_ELOAD1
  { 0x393e, IEC_FLV_ELOAD1,           IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_EPYX
  { 0x5a01, IEC_FLV_EPYXCART,         IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_GEOS
  { 0xb979, IEC_FLV_GEOS_S1_64,       IEC_FLRX_GEOS_1MHZ       }, // GEOS 64 stage 1
  { 0x2469, IEC_FLV_GEOS_S1_128,      IEC_FLRX_GEOS_1MHZ       }, // GEOS 128 stage 1
  { 0x4d79, IEC_FLV_GEOS_S23_1541,    IEC_FLRX_GEOS_1MHZ       }, // GEOS 64 1541 stage 2
  { 0xb2bc, IEC_FLV_GEOS_S23_1541,    IEC_FLRX_GEOS_1MHZ       }, // GEOS 128 1541 stage 2
  { 0xb272, IEC_FLV_GEOS_S23_1541,    IEC_FLRX_GEOS_1MHZ       }, // GEOS 64/128 1541 stage 3 (Configure)
  { 0xdaed, IEC_FLV_GEOS_S23_1571,    IEC_FLRX_GEOS_2MHZ       }, // GEOS 64/128 1571 stage 3 (Configure)
  { 0x3f8d, IEC_FLV_GEOS_S23_1581,    IEC_FLRX_GEOS_2MHZ       }, // GEOS 64/128 1581 Configure 2.0
  { 0xc947, IEC_FLV_GEOS_S23_1581,    IEC_FLRX_GEOS_1581_21    }, // GEOS 64/128 1581 Configure 2.1
#ifdef IEC_FP_WHEELS
  { 0xf140, IEC_FLV_WHEELS_S1_64,     IEC_FLRX_WHEELS_1MHZ     }, // Wheels 64 stage 1
  { 0x737e, IEC_FLV_WHEELS_S1_128,    IEC_FLRX_WHEELS_1MHZ     }, // Wheels 128 stage 1
  { 0x755a, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_1MHZ     }, // Wheels 64 1541 stage 2
  { 0x2920, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_1MHZ     }, // Wheels 128 1541 stage 2
  { 0x18e9, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 1571
  { 0x9804, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 1581
  { 0x48f5, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 FD native partition
  { 0x1356, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 FD emulation partition
  { 0xe885, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 HD native partition
  { 0x4eca, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 64 HD emulation partition
  { 0xdbf6, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 1571
  { 0xe4ab, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 1581
  { 0x6de5, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 FD native
  { 0x30ff, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 FD emulation
  { 0x46e7, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 HD native
  { 0x2253, IEC_FLV_WHEELS_S2,        IEC_FLRX_WHEELS_2MHZ     }, // Wheels 128 HD emulation
  { 0xc26a, IEC_FLV_WHEELS44_S2,      IEC_FLRX_WHEELS44_1541   }, // Wheels 64/128 4.4 1541
  { 0x550c, IEC_FLV_WHEELS44_S2,      IEC_FLRX_WHEELS44_1541   }, // Wheels 64/128 4.4 1571
  { 0x825b, IEC_FLV_WHEELS44_S2_1581, IEC_FLRX_WHEELS44_1581   }, // Wheels 64/128 4.4 1581
  { 0x245b, IEC_FLV_WHEELS44_S2_1581, IEC_FLRX_WHEELS44_1581   }, // Wheels 64/128 4.4 1581
  { 0x7021, IEC_FLV_WHEELS44_S2_1581, IEC_FLRX_WHEELS44_1581   }, // Wheels 64/128 4.4 1581
  { 0xd537, IEC_FLV_WHEELS44_S2_1581, IEC_FLRX_WHEELS44_1581   }, // Wheels 64/128 4.4 1581
  { 0xf635, IEC_FLV_WHEELS44_S2_1581, IEC_FLRX_WHEELS44_1581   }, // Wheels 64/128 4.4 1581
#endif
#endif
#ifdef IEC_FP_NIPPON
  { 0x43c1, IEC_FLV_NIPPON,           IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_AR6
  { 0x4870, IEC_FLV_AR6_1581_LOAD,    IEC_FLRX_NONE            },
  { 0x2925, IEC_FLV_AR6_1581_SAVE,    IEC_FLRX_NONE            },
#endif
#ifdef IEC_FP_MMZAK
  { 0x12a6, IEC_FLV_MMZAK,            IEC_FLRX_NONE            }, // Maniac Mansion / Zak McKracken
#endif
#ifdef IEC_FP_GIJOE
  { 0x0c92, IEC_FLV_GI_JOE,           IEC_FLRX_NONE            }, // hacked-up GI Joe loader from an Eidolon crack
#endif
#ifdef IEC_FP_N0SDOS
  { 0x327d, IEC_FLV_N0SDOS_FILEREAD,  IEC_FLRX_NONE            }, // CRC up to 0x65f, to avoid junk data
#endif
#ifdef IEC_FP_SAMSJOURNEY
  { 0x6af4, IEC_FLV_SAMSJOURNEY,      IEC_FLRX_NONE            }, // CRC of the penultimate M-W
#endif

  { 0, IEC_FLV_NONE, IEC_FLRX_NONE } // end marker
};


struct IECFastLoadHandlerEntry
{
  uint16_t address;
  uint8_t  variant;
  uint8_t  param;
};

static const IECFastLoadHandlerEntry s_handlerTable[] =
{
#ifdef IEC_FP_TURBODISK
  { 0x0303, IEC_FLV_TURBODISK,        0 },
#endif
#ifdef IEC_FP_FC3
  { 0x059a, IEC_FLV_FC3_LOAD,         0 }, // FC3
  { 0x0400, IEC_FLV_FC3_LOAD,         0 }, // EXOS
  { 0x059c, IEC_FLV_FC3_SAVE,         0 },
  { 0x059a, IEC_FLV_FC3_SAVE,         0 }, // variation
  { 0x0403, IEC_FLV_FC3_FREEZED,      1 },
  { 0x057f, IEC_FLV_FC3_OLDFREEZED,   0 },
#endif
#ifdef IEC_FP_DREAMLOAD
  { 0x0700, IEC_FLV_DREAMLOAD,        0 },
#endif
#ifdef IEC_FP_ULOAD3
  { 0x0336, IEC_FLV_ULOAD3,           0 },
#endif
#ifdef IEC_FP_ELOAD1
  { 0x0300, IEC_FLV_ELOAD1,           0 },
#endif
#ifdef IEC_FP_GIJOE
  { 0x0500, IEC_FLV_GI_JOE,           0 },
#endif
#ifdef IEC_FP_EPYX
  { 0x01a9, IEC_FLV_EPYXCART,         0 },
#endif
#ifdef IEC_FP_GEOS
  { 0x0457, IEC_FLV_GEOS_S1_64,       0 },
  { 0x0470, IEC_FLV_GEOS_S1_128,      1 },
  { 0x03e2, IEC_FLV_GEOS_S23_1541,    0 },
  { 0x03dc, IEC_FLV_GEOS_S23_1541,    0 },
  { 0x03ff, IEC_FLV_GEOS_S23_1571,    0 },
  { 0x040f, IEC_FLV_GEOS_S23_1581,    0 },
#ifdef IEC_FP_WHEELS
  { 0x0400, IEC_FLV_WHEELS_S1_64,     0 },
  { 0x0400, IEC_FLV_WHEELS_S1_128,    1 },
  { 0x0300, IEC_FLV_WHEELS_S2,        0 },
  { 0x0400, IEC_FLV_WHEELS44_S2,      0 },
  { 0x0300, IEC_FLV_WHEELS44_S2_1581, 0 },
  { 0x0500, IEC_FLV_WHEELS44_S2_1581, 0 },
#endif
#endif
#ifdef IEC_FP_NIPPON
  { 0x0300, IEC_FLV_NIPPON,           0 },
#endif
#ifdef IEC_FP_AR6
  { 0x0500, IEC_FLV_AR6_1581_LOAD,    0 },
  { 0x05f4, IEC_FLV_AR6_1581_SAVE,    0 },
#endif
#ifdef IEC_FP_MMZAK
  { 0x0500, IEC_FLV_MMZAK,            0 },
#endif
#ifdef IEC_FP_N0SDOS
  { 0x041b, IEC_FLV_N0SDOS_FILEREAD,  0 },
#endif
#ifdef IEC_FP_SAMSJOURNEY
  { 0x0400, IEC_FLV_SAMSJOURNEY,      0 },
#endif

  { 0, IEC_FLV_NONE, 0 } // end marker
};


// -----------------------------------------------------------------------------
// Variant -> family / name
// -----------------------------------------------------------------------------

uint8_t iecFastLoadFamily(uint8_t variant)
{
  switch( variant )
    {
#ifdef IEC_FP_TURBODISK
    case IEC_FLV_TURBODISK:        return IEC_FP_TURBODISK;
#endif
#ifdef IEC_FP_FC3
    case IEC_FLV_FC3_LOAD:
    case IEC_FLV_FC3_SAVE:
    case IEC_FLV_FC3_FREEZED:
    case IEC_FLV_FC3_OLDFREEZED:   return IEC_FP_FC3;
#endif
#ifdef IEC_FP_DREAMLOAD
    case IEC_FLV_DREAMLOAD:
    case IEC_FLV_DREAMLOAD_OLD:    return IEC_FP_DREAMLOAD;
#endif
#ifdef IEC_FP_ULOAD3
    case IEC_FLV_ULOAD3:           return IEC_FP_ULOAD3;
#endif
#ifdef IEC_FP_ELOAD1
    case IEC_FLV_ELOAD1:           return IEC_FP_ELOAD1;
#endif
#ifdef IEC_FP_GIJOE
    case IEC_FLV_GI_JOE:           return IEC_FP_GIJOE;
#endif
#ifdef IEC_FP_EPYX
    case IEC_FLV_EPYXCART:         return IEC_FP_EPYX;
#endif
#ifdef IEC_FP_GEOS
    case IEC_FLV_GEOS_S1_64:
    case IEC_FLV_GEOS_S1_128:
    case IEC_FLV_GEOS_S23_1541:
    case IEC_FLV_GEOS_S23_1571:
    case IEC_FLV_GEOS_S23_1581:    return IEC_FP_GEOS;
#endif
#ifdef IEC_FP_WHEELS
    case IEC_FLV_WHEELS_S1_64:
    case IEC_FLV_WHEELS_S1_128:
    case IEC_FLV_WHEELS_S2:
    case IEC_FLV_WHEELS44_S2:
    case IEC_FLV_WHEELS44_S2_1581: return IEC_FP_WHEELS;
#endif
#ifdef IEC_FP_NIPPON
    case IEC_FLV_NIPPON:           return IEC_FP_NIPPON;
#endif
#ifdef IEC_FP_AR6
    case IEC_FLV_AR6_1581_LOAD:
    case IEC_FLV_AR6_1581_SAVE:    return IEC_FP_AR6;
#endif
#ifdef IEC_FP_MMZAK
    case IEC_FLV_MMZAK:            return IEC_FP_MMZAK;
#endif
#ifdef IEC_FP_N0SDOS
    case IEC_FLV_N0SDOS_FILEREAD:  return IEC_FP_N0SDOS;
#endif
#ifdef IEC_FP_SAMSJOURNEY
    case IEC_FLV_SAMSJOURNEY:      return IEC_FP_SAMSJOURNEY;
#endif
    default: break;
    }

  return 0xFF;
}


const char *iecFastLoadName(uint8_t variant)
{
  switch( variant )
    {
    case IEC_FLV_TURBODISK:        return "Turbodisk";
    case IEC_FLV_FC3_LOAD:         return "FC3 load";
    case IEC_FLV_FC3_SAVE:         return "FC3 save";
    case IEC_FLV_FC3_FREEZED:      return "FC3 freezed";
    case IEC_FLV_DREAMLOAD:        return "Dreamload";
    case IEC_FLV_DREAMLOAD_OLD:    return "Dreamload (old)";
    case IEC_FLV_ULOAD3:           return "ULoad3";
    case IEC_FLV_GI_JOE:           return "GI Joe";
    case IEC_FLV_EPYXCART:         return "Epyx cartridge";
    case IEC_FLV_GEOS_S1_64:       return "GEOS 64 stage 1";
    case IEC_FLV_GEOS_S1_128:      return "GEOS 128 stage 1";
    case IEC_FLV_GEOS_S23_1541:    return "GEOS 1541";
    case IEC_FLV_GEOS_S23_1571:    return "GEOS 1571";
    case IEC_FLV_GEOS_S23_1581:    return "GEOS 1581";
    case IEC_FLV_WHEELS_S1_64:     return "Wheels 64 stage 1";
    case IEC_FLV_WHEELS_S1_128:    return "Wheels 128 stage 1";
    case IEC_FLV_WHEELS_S2:        return "Wheels stage 2";
    case IEC_FLV_WHEELS44_S2:      return "Wheels 4.4 stage 2";
    case IEC_FLV_WHEELS44_S2_1581: return "Wheels 4.4 1581";
    case IEC_FLV_NIPPON:           return "Nippon";
    case IEC_FLV_AR6_1581_LOAD:    return "AR6 1581 load";
    case IEC_FLV_AR6_1581_SAVE:    return "AR6 1581 save";
    case IEC_FLV_ELOAD1:           return "ELoad1";
    case IEC_FLV_FC3_OLDFREEZED:   return "FC3 old freezed";
    case IEC_FLV_MMZAK:            return "Maniac Mansion / Zak McKracken";
    case IEC_FLV_N0SDOS_FILEREAD:  return "N0SDOS file read";
    case IEC_FLV_SAMSJOURNEY:      return "Sam's Journey";
    default:                       return "none";
    }
}


// -----------------------------------------------------------------------------
// IECFastLoadDetect
// -----------------------------------------------------------------------------

void IECFastLoadDetect::reset()
{
  m_crc = 0xFFFF;
  m_detected = IEC_FLV_NONE;
  m_previous = IEC_FLV_NONE;
  m_rxtx = IEC_FLRX_NONE;
}


uint8_t IECFastLoadDetect::memWrite(uint16_t address, const uint8_t *data, size_t len)
{
  // These destinations carry no loader code and must not reach the CRC, or
  // every loader that follows one of them hashes differently than the tables
  // say:
  //   119            - device address change, 1541 style
  //   0x1c06/0x1c07  - VIA 2 timer, which sets the IRQ frequency
  //   0x1802         - VIA 1 DDRB, written by N0SDOS to work around an
  //                    Action Replay problem
  if( address==119 || address==0x1c06 || address==0x1c07 || address==0x1802 )
    return m_detected;

  // A fresh upload invalidates the previous round's detection. sd2iec clears
  // this here so that a loader which uploads twice (FC3, GEOS) cannot have its
  // first stage credited to its second M-E.
  m_previous = IEC_FLV_NONE;

  for(size_t i=0; i<len; i++)
    {
      m_crc = iecCrc16Update(m_crc, data[i]);

#ifdef IEC_FP_GIJOE
      // GI Joe uploads the same code in a dozen different chunkings, so it has
      // no single end-of-upload CRC. It is recognised mid-stream instead, by
      // this CRC arriving together with an RTS.
      if( m_crc==0x38a2 && data[i]==0x60 )
        m_detected = IEC_FLV_GI_JOE;
#endif
    }

  // Run the lookup after every M-W, not only at M-E: a loader is recognisable
  // part way through its upload while later blocks keep adding to the CRC.
  for(const IECFastLoadCrcEntry *p = s_crcTable; p->variant!=IEC_FLV_NONE; p++)
    if( m_crc==p->crc )
      {
        m_detected = p->variant;
        if( p->rxtx!=IEC_FLRX_NONE ) m_rxtx = p->rxtx;
        break;
      }

  return m_detected;
}


uint8_t IECFastLoadDetect::memExec(uint16_t address, uint8_t *param)
{
  // A loader that uploads once and then starts twice (FC3, GEOS) sends the
  // second M-E with nothing in between, so this round detected nothing. Fall
  // back to what the previous round found.
  if( m_detected==IEC_FLV_NONE )
    m_detected = m_previous;

  uint8_t variant = IEC_FLV_NONE;
  for(const IECFastLoadHandlerEntry *p = s_handlerTable; p->variant!=IEC_FLV_NONE; p++)
    if( m_detected==p->variant && address==p->address )
      {
        variant = p->variant;
        if( param ) *param = p->param;
        break;
      }

  m_crc = 0xFFFF;
  m_previous = m_detected;
  m_detected = IEC_FLV_NONE;

  return variant;
}

#else // !IEC_SUPPORT_SOFTLOAD

uint8_t iecFastLoadFamily(uint8_t) { return 0xFF; }
const char *iecFastLoadName(uint8_t) { return "none"; }

void IECFastLoadDetect::reset() {}
uint8_t IECFastLoadDetect::memWrite(uint16_t, const uint8_t *, size_t) { return IEC_FLV_NONE; }
uint8_t IECFastLoadDetect::memExec(uint16_t, uint8_t *) { return IEC_FLV_NONE; }

#endif // IEC_SUPPORT_SOFTLOAD
