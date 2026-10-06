// Host stub for FreeRTOS stream buffers.
//
// ModemPort is a pair of these, and the modem AT suite drives the real
// ModemPort so that what a test observes in a port's TX buffer is what the
// firmware would have put on the wire. The native suite is single-threaded, so
// no locking is modelled -- but the CAPACITY is, because a full TX buffer is
// what makes pushTx() return short and Modem count a dropped byte.
#ifndef ML_STUB_STREAM_BUFFER_H
#define ML_STUB_STREAM_BUFFER_H

#include "FreeRTOS.h"

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>

struct MlStubStreamBuffer
{
    std::deque<uint8_t> bytes;
    size_t              capacity = 0;
};

typedef MlStubStreamBuffer *StreamBufferHandle_t;

static inline StreamBufferHandle_t xStreamBufferCreate(size_t capacity, size_t trigger)
{
    (void)trigger;
    MlStubStreamBuffer *b = new MlStubStreamBuffer();
    b->capacity = capacity;
    return b;
}

static inline void vStreamBufferDelete(StreamBufferHandle_t h) { delete h; }

// Takes as much as fits and reports how much it took -- a short return is the
// signal the real buffer gives when the reader has not kept up.
static inline size_t xStreamBufferSend(StreamBufferHandle_t h, const void *src,
                                       size_t n, TickType_t ticks)
{
    (void)ticks;
    if (h == nullptr || src == nullptr)
        return 0;
    const uint8_t *p = static_cast<const uint8_t *>(src);
    size_t room = h->capacity > h->bytes.size() ? h->capacity - h->bytes.size() : 0;
    size_t take = n < room ? n : room;
    for (size_t i = 0; i < take; ++i)
        h->bytes.push_back(p[i]);
    return take;
}

static inline size_t xStreamBufferReceive(StreamBufferHandle_t h, void *dst,
                                          size_t n, TickType_t ticks)
{
    (void)ticks;
    if (h == nullptr || dst == nullptr)
        return 0;
    uint8_t *p = static_cast<uint8_t *>(dst);
    size_t give = n < h->bytes.size() ? n : h->bytes.size();
    for (size_t i = 0; i < give; ++i)
    {
        p[i] = h->bytes.front();
        h->bytes.pop_front();
    }
    return give;
}

static inline size_t xStreamBufferBytesAvailable(StreamBufferHandle_t h)
{
    return h == nullptr ? 0 : h->bytes.size();
}

static inline BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t h)
{
    return (h == nullptr || h->bytes.empty()) ? pdTRUE : pdFALSE;
}

static inline BaseType_t xStreamBufferReset(StreamBufferHandle_t h)
{
    if (h != nullptr)
        h->bytes.clear();
    return pdTRUE;
}
#endif // __cplusplus

#endif
