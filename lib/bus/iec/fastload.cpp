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
  { 0x9c9f, IEC_FLV_TURBODISK,         IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_FC3
  { 0xdab0, IEC_FLV_FC3_LOAD,          IEC_FLRX_NONE,             }, // Final Cartridge III
  { 0x973b, IEC_FLV_FC3_LOAD,          IEC_FLRX_NONE,             }, // Final Cartridge III variation
  { 0x7e38, IEC_FLV_FC3_LOAD,          IEC_FLRX_NONE,             }, // EXOS v3
  { 0x1b30, IEC_FLV_FC3_SAVE,          IEC_FLRX_NONE,             }, // note: really early CRC; lots of C64 code at the end
  { 0x8b0e, IEC_FLV_FC3_SAVE,          IEC_FLRX_NONE,             }, // variation
  { 0x9930, IEC_FLV_FC3_FREEZED,       IEC_FLRX_NONE,             },
  { 0x0281, IEC_FLV_FC3_OLDFREEZED,    IEC_FLRX_FC3OF_PAL,        }, // older freezed-file loader, PAL
  { 0xc196, IEC_FLV_FC3_OLDFREEZED,    IEC_FLRX_FC3OF_NTSC,       }, // older freezed-file loader, NTSC
#endif
#ifdef IEC_FP_DREAMLOAD
  { 0x2e69, IEC_FLV_DREAMLOAD,         IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_ULOAD3
  { 0xdd81, IEC_FLV_ULOAD3,            IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_ELOAD1
  { 0x393e, IEC_FLV_ELOAD1,            IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_EPYX
  { 0x5a01, IEC_FLV_EPYXCART,          IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_GEOS
  { 0xb979, IEC_FLV_GEOS_S1_64,        IEC_FLRX_GEOS_1MHZ,        }, // GEOS 64 stage 1
  { 0x2469, IEC_FLV_GEOS_S1_128,       IEC_FLRX_GEOS_1MHZ,        }, // GEOS 128 stage 1
  { 0x4d79, IEC_FLV_GEOS_S23_1541,     IEC_FLRX_GEOS_1MHZ,        }, // GEOS 64 1541 stage 2
  { 0xb2bc, IEC_FLV_GEOS_S23_1541,     IEC_FLRX_GEOS_1MHZ,        }, // GEOS 128 1541 stage 2
  { 0xb272, IEC_FLV_GEOS_S23_1541,     IEC_FLRX_GEOS_1MHZ,        }, // GEOS 64/128 1541 stage 3 (Configure)
  { 0xdaed, IEC_FLV_GEOS_S23_1571,     IEC_FLRX_GEOS_2MHZ,        }, // GEOS 64/128 1571 stage 3 (Configure)
  { 0x3f8d, IEC_FLV_GEOS_S23_1581,     IEC_FLRX_GEOS_2MHZ,        }, // GEOS 64/128 1581 Configure 2.0
  { 0xc947, IEC_FLV_GEOS_S23_1581,     IEC_FLRX_GEOS_1581_21,     }, // GEOS 64/128 1581 Configure 2.1
#endif
#ifdef IEC_FP_WHEELS
  { 0xf140, IEC_FLV_WHEELS_S1_64,      IEC_FLRX_WHEELS_1MHZ,      }, // Wheels 64 stage 1
  { 0x737e, IEC_FLV_WHEELS_S1_128,     IEC_FLRX_WHEELS_1MHZ,      }, // Wheels 128 stage 1
  { 0x755a, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_1MHZ,      }, // Wheels 64 1541 stage 2
  { 0x2920, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_1MHZ,      }, // Wheels 128 1541 stage 2
  { 0x18e9, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 1571
  { 0x9804, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 1581
  { 0x48f5, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 FD native partition
  { 0x1356, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 FD emulation partition
  { 0xe885, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 HD native partition
  { 0x4eca, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 64 HD emulation partition
  { 0xdbf6, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 1571
  { 0xe4ab, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 1581
  { 0x6de5, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 FD native
  { 0x30ff, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 FD emulation
  { 0x46e7, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 HD native
  { 0x2253, IEC_FLV_WHEELS_S2,         IEC_FLRX_WHEELS_2MHZ,      }, // Wheels 128 HD emulation
  { 0xc26a, IEC_FLV_WHEELS44_S2,       IEC_FLRX_WHEELS44_1541,    }, // Wheels 64/128 4.4 1541
  { 0x550c, IEC_FLV_WHEELS44_S2,       IEC_FLRX_WHEELS44_1541,    }, // Wheels 64/128 4.4 1571
  { 0x825b, IEC_FLV_WHEELS44_S2_1581,  IEC_FLRX_WHEELS44_1581,    }, // Wheels 64/128 4.4 1581
  { 0x245b, IEC_FLV_WHEELS44_S2_1581,  IEC_FLRX_WHEELS44_1581,    }, // Wheels 64/128 4.4 1581
  { 0x7021, IEC_FLV_WHEELS44_S2_1581,  IEC_FLRX_WHEELS44_1581,    }, // Wheels 64/128 4.4 1581
  { 0xd537, IEC_FLV_WHEELS44_S2_1581,  IEC_FLRX_WHEELS44_1581,    }, // Wheels 64/128 4.4 1581
  { 0xf635, IEC_FLV_WHEELS44_S2_1581,  IEC_FLRX_WHEELS44_1581,    }, // Wheels 64/128 4.4 1581
#endif
#ifdef IEC_FP_NIPPON
  { 0x43c1, IEC_FLV_NIPPON,            IEC_FLRX_NONE,             }, // Nippon
#endif
#ifdef IEC_FP_AR6
  { 0x4870, IEC_FLV_AR6_1581_LOAD,     IEC_FLRX_NONE,             },
  { 0x2925, IEC_FLV_AR6_1581_SAVE,     IEC_FLRX_NONE,             },
#endif
#ifdef IEC_FP_MMZAK
  { 0x12a6, IEC_FLV_MMZAK,             IEC_FLRX_NONE,             }, // Maniac Mansion/Zak McKracken
#endif
#ifdef IEC_FP_GIJOE
  { 0x0c92, IEC_FLV_GI_JOE,            IEC_FLRX_NONE,             }, // hacked-up GI Joe loader seen in an Eidolon crack
#endif
#ifdef IEC_FP_N0SDOS
  { 0x327d, IEC_FLV_N0SDOS_FILEREAD,   IEC_FLRX_NONE,             }, // CRC up to 0x65f to avoid junk data
#endif
#ifdef IEC_FP_SAMSJOURNEY
  { 0x6af4, IEC_FLV_SAMSJOURNEY,       IEC_FLRX_NONE,             }, // CRC of penultimate M-W
#endif
#ifdef IEC_FP_HYPRALOAD
  { 0xd2f2, IEC_FLV_HYPRALOAD,         IEC_FLRX_HYPRALOAD_10,     },
  { 0x5983, IEC_FLV_HYPRALOAD,         IEC_FLRX_HYPRALOAD_21,     },
#endif
#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
  { 0x8667, IEC_FLV_KRILL_R146,        IEC_FLRX_NONE,             }, // r146 drvchkme
  { 0xe300, IEC_FLV_KRILL_R186,        IEC_FLRX_KRILL_CLOCK,      }, // second chunk
  { 0x19a4, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      }, // second chunk
  { 0x6264, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x741d, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x74a5, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x928f, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      },
  { 0xf7e4, IEC_FLV_KRILL_R184,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x1eec, IEC_FLV_KRILL_R164,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x4393, IEC_FLV_KRILL_R164,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x6c47, IEC_FLV_KRILL_R164,        IEC_FLRX_KRILL_CLOCK,      },
  { 0xd9f1, IEC_FLV_KRILL_R164,        IEC_FLRX_KRILL_CLOCK,      },
  { 0xa905, IEC_FLV_KRILL_R159,        IEC_FLRX_KRILL_CLOCK,      },
  { 0xe7f6, IEC_FLV_KRILL_R159,        IEC_FLRX_KRILL_CLOCK,      },
  { 0x2028, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x2c29, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x4eb4, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x5668, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       }, // second chunk
  { 0x6a90, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x74aa, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x7c5e, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x7e28, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       }, // second chunk
  { 0xa1e7, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xa350, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xb0e4, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xb340, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xc1dc, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xeb28, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xf5a8, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0xfc9a, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_DATA,       },
  { 0x03a5, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_RESEND,     },
  { 0xba1f, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_RESEND,     },
  { 0xca68, IEC_FLV_KRILL_R146,        IEC_FLRX_KRILL_RESEND,     },
  { 0x2fca, IEC_FLV_KRILL_R58,         IEC_FLRX_KRILL_DATA,       },
  { 0xb4ce, IEC_FLV_KRILL_R58,         IEC_FLRX_KRILL_DATA,       }, // second chunk
  { 0xe530, IEC_FLV_KRILL_R58,         IEC_FLRX_KRILL_DATA,       },
  { 0xf7aa, IEC_FLV_KRILL_R58PRE,      IEC_FLRX_KRILL_58PRE,      },
  { 0x379d, IEC_FLV_KRILL_R58PRE,      IEC_FLRX_KRILL_58PRE,      },
  { 0x607d, IEC_FLV_KRILL_SLEEP,       IEC_FLRX_NONE,             }, // >= r186
  { 0x40c3, IEC_FLV_KRILL_SLEEP,       IEC_FLRX_NONE,             }, // r184
  { 0x5088, IEC_FLV_KRILL_SLEEP,       IEC_FLRX_NONE,             }, // r164
#endif
#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x1fdc, IEC_FLV_SPINDLE_SLEEP,     IEC_FLRX_NONE,             },
#endif
#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x955d, IEC_FLV_BITFIRE_SLEEP,     IEC_FLRX_NONE,             },
#endif
#if defined(IEC_FP_TRANSWARP) && defined(IEC_IMPL_SOFTLOAD)
  { 0xb20a, IEC_FLV_TRANSWARP_SLEEP,   IEC_FLRX_NONE,             },
#endif
#if defined(IEC_FP_BOOZE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0c48, IEC_FLV_BOOZE,             IEC_FLRX_NONE,             },
  { 0x5f66, IEC_FLV_BOOZE,             IEC_FLRX_NONE,             },
#endif
#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x7cd6, IEC_FLV_BITFIRE_01,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0xf1ec, IEC_FLV_BITFIRE_01,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0x2b10, IEC_FLV_BITFIRE_03,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0xb0f4, IEC_FLV_BITFIRE_04,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0xaf44, IEC_FLV_BITFIRE_06,        IEC_FLRX_BITFIRE_ICLK,     },
  { 0x1f43, IEC_FLV_BITFIRE_07PRE,     IEC_FLRX_BITFIRE_IDATA,    },
  { 0xb2dd, IEC_FLV_BITFIRE_07PRE,     IEC_FLRX_BITFIRE_IDATA,    },
  { 0x809f, IEC_FLV_BITFIRE_07DBG,     IEC_FLRX_BITFIRE_IDATA,    },
  { 0x3046, IEC_FLV_BITFIRE_07,        IEC_FLRX_BITFIRE_IDATA,    },
  { 0xb8e6, IEC_FLV_BITFIRE_07,        IEC_FLRX_BITFIRE_IDATA,    },
  { 0xc83a, IEC_FLV_BITFIRE_10,        IEC_FLRX_BITFIRE_ICLK,     },
  { 0x0453, IEC_FLV_BITFIRE_11,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0x7c59, IEC_FLV_BITFIRE_11,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0xa45a, IEC_FLV_BITFIRE_11,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0x1c3d, IEC_FLV_BITFIRE_11,        IEC_FLRX_BITFIRE_CLOCK,    },
  { 0x8d3a, IEC_FLV_BITFIRE_12PR1,     IEC_FLRX_BITFIRE_DATA,     },
  { 0x4521, IEC_FLV_BITFIRE_12PR2,     IEC_FLRX_BITFIRE_DATA,     },
  { 0xc33e, IEC_FLV_BITFIRE_12,        IEC_FLRX_BITFIRE_DATA,     },
  { 0xbfef, IEC_FLV_BITFIRE_12,        IEC_FLRX_BITFIRE_DATA,     },
  { 0xb89a, IEC_FLV_BITFIRE_12,        IEC_FLRX_BITFIRE_DATA,     },
  { 0xc1bc, IEC_FLV_BITFIRE_13,        IEC_FLRX_BITFIRE_IDATA,    },
#endif

  { 0, IEC_FLV_NONE, IEC_FLRX_NONE } // end marker
};


// Windows of an upload that have to be KEPT rather than just hashed. GEOS
// stage 1 XOR-encrypts every sector chain after the first with a 256-byte key
// that is only ever present in the code it uploads, so the key has to be
// lifted out of the M-W stream as it goes past.
struct IECFastLoadCaptureEntry
{
  uint8_t  variant;
  uint16_t address;
  uint16_t length;
};

#ifdef IEC_IMPL_SOFTLOAD
static const IECFastLoadCaptureEntry s_captureTable[] =
{
#ifdef IEC_FP_GEOS
  { IEC_FLV_GEOS_S1_64,        0x42a, 256 },
  { IEC_FLV_GEOS_S1_128,       0x44f, 256 },
#endif

  { IEC_FLV_NONE, 0, 0 } // end marker
};
#endif


struct IECFastLoadHandlerEntry
{
  uint16_t address;
  uint8_t  variant;
  uint8_t  param;
};

#ifdef IEC_IMPL_SOFTLOAD
static const IECFastLoadHandlerEntry s_handlerTable[] =
{
#ifdef IEC_FP_TURBODISK
  { 0x0303, IEC_FLV_TURBODISK,         0 },
#endif
#ifdef IEC_FP_FC3
  { 0x059a, IEC_FLV_FC3_LOAD,          0 },
  { 0x0400, IEC_FLV_FC3_LOAD,          0 },
  { 0x059c, IEC_FLV_FC3_SAVE,          0 },
  { 0x059a, IEC_FLV_FC3_SAVE,          0 },
  { 0x0403, IEC_FLV_FC3_FREEZED,       1 },
  { 0x057f, IEC_FLV_FC3_OLDFREEZED,    0 },
#endif
#ifdef IEC_FP_DREAMLOAD
  { 0x0700, IEC_FLV_DREAMLOAD,         0 },
#endif
#ifdef IEC_FP_ULOAD3
  { 0x0336, IEC_FLV_ULOAD3,            0 },
#endif
#ifdef IEC_FP_ELOAD1
  { 0x0300, IEC_FLV_ELOAD1,            0 },
#endif
#ifdef IEC_FP_GIJOE
  { 0x0500, IEC_FLV_GI_JOE,            0 },
#endif
#ifdef IEC_FP_EPYX
  { 0x01a9, IEC_FLV_EPYXCART,          0 },
#endif
#ifdef IEC_FP_GEOS
  { 0x0457, IEC_FLV_GEOS_S1_64,        0 },
  { 0x0470, IEC_FLV_GEOS_S1_128,       1 },
  { 0x03e2, IEC_FLV_GEOS_S23_1541,     0 },
  { 0x03dc, IEC_FLV_GEOS_S23_1541,     0 },
  { 0x03ff, IEC_FLV_GEOS_S23_1571,     0 },
  { 0x040f, IEC_FLV_GEOS_S23_1581,     0 },
#endif
#ifdef IEC_FP_WHEELS
  { 0x0400, IEC_FLV_WHEELS_S1_64,      0 },
  { 0x0400, IEC_FLV_WHEELS_S1_128,     1 },
  { 0x0300, IEC_FLV_WHEELS_S2,         0 },
  { 0x0400, IEC_FLV_WHEELS44_S2,       0 },
  { 0x0300, IEC_FLV_WHEELS44_S2_1581,  0 },
  { 0x0500, IEC_FLV_WHEELS44_S2_1581,  0 },
#endif
#ifdef IEC_FP_NIPPON
  { 0x0300, IEC_FLV_NIPPON,            0 },
#endif
#ifdef IEC_FP_AR6
  { 0x0500, IEC_FLV_AR6_1581_LOAD,     0 },
  { 0x05f4, IEC_FLV_AR6_1581_SAVE,     0 },
#endif
#ifdef IEC_FP_MMZAK
  { 0x0500, IEC_FLV_MMZAK,             0 },
#endif
#ifdef IEC_FP_N0SDOS
  { 0x041b, IEC_FLV_N0SDOS_FILEREAD,   0 },
#endif
#ifdef IEC_FP_SAMSJOURNEY
  { 0x0400, IEC_FLV_SAMSJOURNEY,       0 },
#endif
  { 0x0205, IEC_FLV_NONE,              0 },
  { 0x0417, IEC_FLV_NONE,              0 },
#if defined(IEC_FP_ULTRABOOT) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0424, IEC_FLV_ULTRABOOT,         0 },
#endif
#ifdef IEC_FP_HYPRALOAD
  { 0x0401, IEC_FLV_HYPRALOAD,         0 },
  { 0x048b, IEC_FLV_HYPRALOAD,         0 },
#endif
#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0205, IEC_FLV_KRILL_SLEEP,       0 },
#endif
  { 0x020b, IEC_FLV_NONE,              1 },
#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0403, IEC_FLV_SPINDLE_SLEEP,     0 },
#endif
#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0205, IEC_FLV_BITFIRE_SLEEP,     0 },
#endif
#if defined(IEC_FP_TRANSWARP) && defined(IEC_IMPL_SOFTLOAD)
  { 0x030d, IEC_FLV_TRANSWARP_SLEEP,   0 },
#endif
  { 0x0205, IEC_FLV_NONE,              1 },
  { 0x020a, IEC_FLV_NONE,              2 },
#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0300, IEC_FLV_KRILL_R146,        0 },
#endif
  { 0x0209, IEC_FLV_NONE,              0 },
#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0770, IEC_FLV_KRILL_R186,        0 },
  { 0x0758, IEC_FLV_KRILL_R184,        0 },
  { 0x0770, IEC_FLV_KRILL_R184,        0 },
  { 0x07a8, IEC_FLV_KRILL_R184,        0 },
  { 0x07ac, IEC_FLV_KRILL_R184,        0 },
  { 0x07ae, IEC_FLV_KRILL_R184,        0 },
  { 0x07ce, IEC_FLV_KRILL_R184,        0 },
  { 0x07e5, IEC_FLV_KRILL_R184,        0 },
  { 0x06d8, IEC_FLV_KRILL_R164,        0 },
  { 0x077e, IEC_FLV_KRILL_R164,        0 },
  { 0x07aa, IEC_FLV_KRILL_R164,        0 },
  { 0x07ac, IEC_FLV_KRILL_R164,        0 },
  { 0x07a5, IEC_FLV_KRILL_R159,        0 },
  { 0x07b1, IEC_FLV_KRILL_R159,        0 },
  { 0x056f, IEC_FLV_KRILL_R146,        0 },
  { 0x0570, IEC_FLV_KRILL_R146,        0 },
  { 0x0577, IEC_FLV_KRILL_R146,        0 },
  { 0x05e9, IEC_FLV_KRILL_R146,        0 },
  { 0x05ea, IEC_FLV_KRILL_R146,        0 },
  { 0x05ec, IEC_FLV_KRILL_R146,        0 },
  { 0x05ee, IEC_FLV_KRILL_R146,        0 },
  { 0x05ef, IEC_FLV_KRILL_R146,        0 },
  { 0x05fc, IEC_FLV_KRILL_R146,        0 },
  { 0x05fe, IEC_FLV_KRILL_R146,        0 },
  { 0x0610, IEC_FLV_KRILL_R146,        0 },
  { 0x066e, IEC_FLV_KRILL_R146,        0 },
  { 0x06a4, IEC_FLV_KRILL_R146,        0 },
  { 0x06b6, IEC_FLV_KRILL_R146,        0 },
  { 0x05fc, IEC_FLV_KRILL_R58,         0 },
  { 0x05fe, IEC_FLV_KRILL_R58,         0 },
  { 0x05ff, IEC_FLV_KRILL_R58,         0 },
  { 0x0626, IEC_FLV_KRILL_R58,         0 },
  { 0x0668, IEC_FLV_KRILL_R58,         0 },
  { 0x05da, IEC_FLV_KRILL_R58PRE,      0 },
  { 0x05f1, IEC_FLV_KRILL_R58PRE,      0 },
  { 0x05f4, IEC_FLV_KRILL_R58PRE,      0 },
  { 0x0600, IEC_FLV_KRILL_R58PRE,      0 },
#endif
#if defined(IEC_FP_BOOZE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0373, IEC_FLV_BOOZE,             0 },
  { 0x04b8, IEC_FLV_BOOZE,             0 },
#endif
  { 0x020b, IEC_FLV_NONE,              0 },
  { 0x020d, IEC_FLV_NONE,              0 },
  { 0x020f, IEC_FLV_NONE,              0 },
  { 0x0211, IEC_FLV_NONE,              0 },
  { 0x0205, IEC_FLV_NONE,              0 },
#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
  { 0x0700, IEC_FLV_BITFIRE_01,        0 },
  { 0x0700, IEC_FLV_BITFIRE_03,        1 },
  { 0x0700, IEC_FLV_BITFIRE_04,        2 },
  { 0x0700, IEC_FLV_BITFIRE_06,        3 },
  { 0x0700, IEC_FLV_BITFIRE_07PRE,     3 },
  { 0x0700, IEC_FLV_BITFIRE_07DBG,     4 },
  { 0x0700, IEC_FLV_BITFIRE_07,        5 },
  { 0x0700, IEC_FLV_BITFIRE_10,        6 },
  { 0x0700, IEC_FLV_BITFIRE_11,        6 },
  { 0x0700, IEC_FLV_BITFIRE_12PR1,     6 },
  { 0x0700, IEC_FLV_BITFIRE_12PR2,     6 },
  { 0x0700, IEC_FLV_BITFIRE_12,        6 },
  { 0x0600, IEC_FLV_BITFIRE_13,        6 },
#endif
  { 0x0205, IEC_FLV_NONE,              0 },
};

// FL_NONE is a VALID loadertype in this table -- sd2iec's newer dispatch uses
// such entries as catch-alls, matched on the M-E address alone when no CRC
// identified anything. So the table cannot be terminated by a FL_NONE row and
// carries its length instead.
static const uint16_t s_handlerTableLen = sizeof(s_handlerTable)/sizeof(s_handlerTable[0]);
#endif



// -----------------------------------------------------------------------------
// Variant -> family / name
// -----------------------------------------------------------------------------

// Indexed by IEC_FLV_*, for the same reason the name table is: a 54-case
// switch cost the tightest ESP32 board its last bytes of iram0_2_seg. 0xFF
// means the variant belongs to no family compiled into this build.
uint8_t iecFastLoadFamily(uint8_t variant)
{
  static const uint8_t families[] = {
  0xFF,
  IEC_FP_DREAMLOAD,
  IEC_FP_DREAMLOAD,
  IEC_FP_TURBODISK,
  IEC_FP_FC3,
  IEC_FP_FC3,
  IEC_FP_FC3,
  IEC_FP_ULOAD3,
  IEC_FP_GIJOE,
  IEC_FP_EPYX,
  IEC_FP_GEOS,
  IEC_FP_GEOS,
  IEC_FP_GEOS,
  IEC_FP_GEOS,
  IEC_FP_GEOS,
  IEC_FP_WHEELS,
  IEC_FP_WHEELS,
  IEC_FP_WHEELS,
  IEC_FP_WHEELS,
  IEC_FP_WHEELS,
  IEC_FP_NIPPON,
  IEC_FP_AR6,
  IEC_FP_AR6,
  IEC_FP_ELOAD1,
  IEC_FP_FC3,
  IEC_FP_MMZAK,
  IEC_FP_N0SDOS,
  IEC_FP_SAMSJOURNEY,
  IEC_FP_ULTRABOOT,
  IEC_FP_HYPRALOAD,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_KRILL,
  IEC_FP_BOOZE,
  IEC_FP_SPINDLE,
  IEC_FP_SPINDLE,
  IEC_FP_SPINDLE,
  IEC_FP_SPINDLE,
  IEC_FP_SPINDLE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_BITFIRE,
  IEC_FP_SPARKLE,
  IEC_FP_SPARKLE,
  IEC_FP_SPARKLE,
  IEC_FP_SPARKLE,
  IEC_FP_SPARKLE,
  IEC_FP_TRANSWARP,
  };

  if( variant >= (sizeof(families)/sizeof(families[0])) ) return 0xFF;
  return families[variant];
}


// Indexed by IEC_FLV_*, which is why the numbering is kept dense and matching
// sd2iec's. A table rather than a switch: the switch cost the tightest ESP32
// board its last hundred bytes of iram0_2_seg.
const char *iecFastLoadName(uint8_t variant)
{
  static const char * const names[] = {
    "none",
    "dreamload",
    "dreamload old",
    "turbodisk",
    "fc3 load",
    "fc3 save",
    "fc3 freezed",
    "uload3",
    "gi joe",
    "epyxcart",
    "geos s1 64",
    "geos s1 128",
    "geos s23 1541",
    "geos s23 1571",
    "geos s23 1581",
    "wheels s1 64",
    "wheels s1 128",
    "wheels s2",
    "wheels44 s2",
    "wheels44 s2 1581",
    "nippon",
    "ar6 1581 load",
    "ar6 1581 save",
    "eload1",
    "fc3 oldfreezed",
    "mmzak",
    "n0sdos fileread",
    "samsjourney",
    "ultraboot",
    "hypraload",
    "krill sleep",
    "krill r58pre",
    "krill r58",
    "krill r146",
    "krill r159",
    "krill r164",
    "krill r184",
    "krill r186",
    "krill r192",
    "booze",
    "spindle sleep",
    "spindle 21",
    "spindle 22",
    "spindle 23",
    "spindle 3",
    "bitfire sleep",
    "bitfire 01",
    "bitfire 03",
    "bitfire 04",
    "bitfire 06",
    "bitfire 07pre",
    "bitfire 07dbg",
    "bitfire 07",
    "bitfire 10",
    "bitfire 11",
    "bitfire 12pr1",
    "bitfire 12pr2",
    "bitfire 12",
    "bitfire 13",
    "sparkle 10",
    "sparkle 15",
    "sparkle 20",
    "sparkle 21",
    "sparkle 32",
    "transwarp sleep"
  };

  if( variant >= (sizeof(names)/sizeof(names[0])) ) return names[0];
  return names[variant];
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
#ifdef IEC_IMPL_SOFTLOAD
  m_captureActive = false;
  m_captureDone = false;
  m_captureRemain = 0;
  m_captureOffset = 0;
#endif
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

#ifdef IEC_IMPL_SOFTLOAD
  // Copy out any part of this block that falls inside a capture window. This
  // runs AFTER the lookup, because the window is opened by the same M-W that
  // identifies the loader and can start within that very block.
  if( m_captureActive )
    {
      if( address <= m_captureAddress && address+len > m_captureAddress )
        {
          uint16_t off = (uint16_t)(m_captureAddress - address);
          uint16_t n   = (uint16_t)(len - off);
          if( n > m_captureRemain ) n = m_captureRemain;

          for(uint16_t i=0; i<n; i++)
            m_capture[m_captureOffset + i] = data[off + i];

          m_captureOffset  += n;
          m_captureAddress += n;
          m_captureRemain  -= n;

          if( m_captureRemain==0 ) { m_captureActive = false; m_captureDone = true; }
        }
    }
  else if( m_detected!=IEC_FLV_NONE && !m_captureDone )
    {
      for(const IECFastLoadCaptureEntry *p = s_captureTable; p->variant!=IEC_FLV_NONE; p++)
        if( m_detected==p->variant )
          {
            m_captureAddress = p->address;
            m_captureRemain  = p->length;
            m_captureOffset  = 0;
            m_captureActive  = true;
            break;
          }
    }
#endif

  return m_detected;
}


uint8_t IECFastLoadDetect::memExec(uint16_t address, uint8_t *param, bool *matched)
{
  // A loader that uploads once and then starts twice (FC3, GEOS) sends the
  // second M-E with nothing in between, so this round detected nothing. Fall
  // back to what the previous round found.
  if( m_detected==IEC_FLV_NONE )
    m_detected = m_previous;

#ifndef IEC_IMPL_SOFTLOAD
  // No loader implementation is compiled on this board, so there is no handler
  // table to consult. Report what the CRC identified anyway -- that is what
  // names the loader in the log -- and leave "matched" false so the caller
  // falls back to the standard protocol.
  (void) address;
  if( param ) *param = 0;
  if( matched ) *matched = false;
  uint8_t detected = m_detected;
  m_crc = 0xFFFF;
  m_previous = m_detected;
  m_detected = IEC_FLV_NONE;
  return detected;
#else
  // Two passes, mirroring sd2iec's run_loader(): first for the loader the CRC
  // identified, then again as IEC_FLV_NONE so an M-E that matched nothing can
  // still reach the catch-all rows, which are keyed on the address alone.
  uint8_t variant = IEC_FLV_NONE;
  uint8_t want = m_detected;
  bool    hit  = false;
  for(uint8_t pass=0; pass<2 && !hit; pass++)
    {
      for(uint16_t i=0; i<s_handlerTableLen; i++)
        if( want==s_handlerTable[i].variant && address==s_handlerTable[i].address )
          {
            variant = s_handlerTable[i].variant;
            if( param ) *param = s_handlerTable[i].param;
            hit = true;
            break;
          }

      if( want==IEC_FLV_NONE ) break;   // the second pass would repeat the first
      want = IEC_FLV_NONE;
    }

  if( matched ) *matched = hit;

  m_crc = 0xFFFF;
  m_previous = m_detected;
  m_detected = IEC_FLV_NONE;

  return variant;
#endif
}

#else // !IEC_SUPPORT_SOFTLOAD

uint8_t iecFastLoadFamily(uint8_t) { return 0xFF; }
const char *iecFastLoadName(uint8_t) { return "none"; }

void IECFastLoadDetect::reset() {}
uint8_t IECFastLoadDetect::memWrite(uint16_t, const uint8_t *, size_t) { return IEC_FLV_NONE; }
uint8_t IECFastLoadDetect::memExec(uint16_t, uint8_t *) { return IEC_FLV_NONE; }

#endif // IEC_SUPPORT_SOFTLOAD
