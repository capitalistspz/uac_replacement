#pragma once
#include <wut.h>
#include <coreinit/event.h>

#define UAC_OPT_READ_FLAG 0x80

typedef struct UACIpcWorkMemory UACIpcWorkMemory;
typedef struct UACRequestData UACRequestData;
typedef struct UACSampleBufInfo UACSampleBufInfo;
typedef struct UACISODesc UACISODesc;

typedef enum UACError {
    UAC_ERROR_IPC_POOL_UNINITIALIZED = -1638452,
    UAC_ERROR_IPC_ALLOC_FAILED = -1638451,
    UAC_ERROR_INVALID_ISO_DESC_IPC_BUFFER_SIZE = -1638438,
    UAC_ERROR_INVALID_IPC_BUFFER_SIZE = -1638437,
    UAC_ERROR_IPC_HAS_OLD_EVENT = -1638436,
    UAC_ERROR_IOS_OPEN_FAILED = -1638435,
    UAC_ERROR_IOCTL_FAILED = -1638434,
    UAC_ERROR_NOT_OPEN = -1638433,
    UAC_ERROR_ALREADY_CALLED = -1638432,
    UAC_ERROR_UNINITIALIZED = -1638431,
    UAC_ERROR_INVALID_ARG = -1638430,
    UAC_SUCCESS = 0
} UACError;

WUT_CHECK_SIZE(UACError, 4);

typedef enum UACChannel {
    UAC_CHANNEL_0,
    UAC_CHANNEL_1
} UACChannel;

WUT_CHECK_SIZE(UACChannel, 4);


struct WUT_PACKED UACRequestData {
    uint8_t id;
    uint8_t opt;
    //! Always set to 1 by mic.rpl
    uint16_t unk;
    void *buffer;
    uint32_t size;
    //! Size set after a request with UAC_OPT_READ_FLAG set on opt
    uint32_t returnedSize;
};

WUT_CHECK_OFFSET(UACRequestData, 0x00, id);
WUT_CHECK_OFFSET(UACRequestData, 0x01, opt);
WUT_CHECK_OFFSET(UACRequestData, 0x02, unk);
WUT_CHECK_OFFSET(UACRequestData, 0x04, buffer);
WUT_CHECK_OFFSET(UACRequestData, 0x08, size);
WUT_CHECK_OFFSET(UACRequestData, 0x0c, returnedSize);
WUT_CHECK_SIZE(UACRequestData, 0x10);


//! Both buffers must be allocated from an IPCBufPool
struct UACIpcWorkMemory {
    void *buffer;
    // Must be a multiple of 2048
    uint32_t bufferSizeBytes;
    void *isoDescBuffer;
    // Must equal (bufferSizeInBytes * 96) / 2048
    uint32_t isoDescBufferSizeBytes;
};

WUT_CHECK_OFFSET(UACIpcWorkMemory, 0x00, buffer);
WUT_CHECK_OFFSET(UACIpcWorkMemory, 0x04, bufferSizeBytes);
WUT_CHECK_OFFSET(UACIpcWorkMemory, 0x08, isoDescBuffer);
WUT_CHECK_OFFSET(UACIpcWorkMemory, 0x0c, isoDescBufferSizeBytes);
WUT_CHECK_SIZE(UACIpcWorkMemory, 0x10);

struct UACSampleBufInfo {
    //! Not sure of this, here because of logging in mic.rpl
    uint16_t frameSlipMs;
    uint16_t sizeBytes;
};

WUT_CHECK_OFFSET(UACSampleBufInfo, 0x00, frameSlipMs);
WUT_CHECK_OFFSET(UACSampleBufInfo, 0x02, sizeBytes);
WUT_CHECK_SIZE(UACSampleBufInfo, 0x04);

struct UACISODesc {
    //! May just be padding, seem to always be zero
    WUT_UNKNOWN_BYTES(0x20);
    void *sampleBufs[0x08];
    UACSampleBufInfo bufInfo[0x08];
};

WUT_CHECK_OFFSET(UACISODesc, 0x20, sampleBufs);
WUT_CHECK_OFFSET(UACISODesc, 0x40, bufInfo);
WUT_CHECK_SIZE(UACISODesc, 0x60);

UACError UACInit();

UACError UACOpen(UACChannel channel, const UACIpcWorkMemory *workMem);

UACError UACClose(UACChannel channel);

UACError UACFreeISODesc(UACChannel channel, UACISODesc *desc);

UACError UACGetAudio(UACChannel channel, OSEvent *event, UACISODesc **outDesc);

UACError UACRequest(UACChannel channel, UACRequestData *request);
