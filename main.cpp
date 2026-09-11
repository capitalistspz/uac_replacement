#include "impl_structs.hpp"

#include "util.hpp"
#include <wums.h>

#include <coreinit/mutex.h>
#include <coreinit/ios.h>
#include <coreinit/ipcbufpool.h>
#include <coreinit/event.h>
#include <coreinit/cosreport.h>
#include <coreinit/cache.h>

#include <cstring>
#include <iostream>
#include <memory>

#include "uac.hpp"
#define LOG_WARN(fmt, ...) do \
    { \
COSWarn(COS_REPORT_MODULE_UNKNOWN_1, "UACRepl.%s: " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__); \
} while(false)

using namespace cz::util::ops;

constexpr auto MAX_DESCRIPTOR_COUNT = 8u;
constexpr auto IPC_BACKING_POOL_SIZE = 0x333e;

namespace {
    struct UACInstance {
    private:
        OSMutex mutex{};

    public:
        IOSHandle iosHandle{-1};
        uint32_t unknownValueA{1};
        uint32_t audioRequestCount{};
        uac_ga_ring_buffer gaInfoQueue{};
        UACISODesc *descBuffer{};
        UACChannel channel;

        explicit UACInstance(UACChannel channel) : channel(channel) {
            OSInitMutexEx(&mutex, "UACReplInstMtx");
        }

        [[nodiscard]] cz::cos_util::cos_conditional_lock Acquire(bool forReal) {
            return {mutex, forReal};
        }
    };

    std::array<std::byte, IPC_BACKING_POOL_SIZE> ipcBufPoolBackingArray;
    bool initCalled = false;
    IPCBufPool *pool;
    std::optional<std::array<UACInstance, 2> > instances;

    void FreeIpcMsg(uac_ipc_txn* p) {
        if (pool && p)
            IPCBufPoolFree(pool, p);
    }

    [[gnu::malloc(FreeIpcMsg)]]
    uac_ipc_txn * AllocateIpcMsg(UACError &outError) {
        if (!pool) {
            outError = UAC_ERROR_IPC_POOL_UNINITIALIZED;
            return nullptr;
        }
        auto msg = static_cast<uac_ipc_txn *>(IPCBufPoolAllocate(pool, sizeof(uac_ipc_txn)));
        if (!msg) {
            outError = UAC_ERROR_IPC_ALLOC_FAILED;
            return nullptr;
        }
        outError = UAC_SUCCESS;
        std::memset(msg, 0, sizeof(uac_ipc_txn));
        return msg;
    }
}

static UACError uac_close(UACChannel channel, bool acquireLock) {
    if (channel != UAC_CHANNEL_0 && channel != UAC_CHANNEL_1)
        return UAC_ERROR_INVALID_ARG;
    if (!instances)
        return UAC_ERROR_UNINITIALIZED;
    auto &inst = (*instances)[static_cast<unsigned>(channel)];
    auto _ = inst.Acquire(acquireLock);

    UACError uacError;
    auto msg = AllocateIpcMsg(uacError);
    if (!msg) {
        return uacError;
    }

    msg->request.channel = channel;
    msg->request.ipcReq = uac_ipc_request_id::Close;
    msg->vecs[0].vaddr = &msg->request;
    msg->vecs[0].len = sizeof(msg->request);

    const auto res = IOS_Ioctlv(inst.iosHandle, 2, 1, 0, msg->vecs);

    if (res)
        return UAC_ERROR_IOCTL_FAILED;

    const auto err = IOS_Close(inst.iosHandle);
    if (err != IOS_ERROR_OK)
        return UAC_ERROR_IOCTL_FAILED;

    inst.iosHandle = -1;
    return UAC_SUCCESS;
}

static UACError uac_send_ipc_open_msg(UACInstance &inst, const UACIpcWorkMemory *workMem) {
    inst.gaInfoQueue.reset();
    UACError uacError;
    auto msg = AllocateIpcMsg(uacError);
    if (!msg)
        return uacError;
    inst.descBuffer = static_cast<UACISODesc *>(workMem->isoDescBuffer);
    LOG_WARN("Desc buffer: %p", inst.descBuffer);
    msg->request.ipcReq = uac_ipc_request_id::Open;
    msg->request.channel = inst.channel;
    msg->request.u.open = {
        .unknownValueA = inst.unknownValueA,
        .ipcBuffer = workMem->buffer
    };
    msg->vecs[0].vaddr = &msg->request;
    msg->vecs[0].len = sizeof(msg->request);
    msg->vecs[1].vaddr = workMem->buffer;
    msg->vecs[1].len = workMem->bufferSizeBytes;
    msg->vecs[2].vaddr = workMem->isoDescBuffer;
    msg->vecs[2].len = workMem->isoDescBufferSizeBytes;

    const auto res = IOS_Ioctlv(inst.iosHandle, +uac_ipc_request_id::Open, 1, 2, msg->vecs);
    FreeIpcMsg(msg);
    if (res != IOS_ERROR_OK) {
        LOG_WARN("Open IOCTL failed: %d", res);
        return UAC_ERROR_IOCTL_FAILED;
    }
    return UAC_SUCCESS;
}


UACError UACInit() {
    LOG_WARN("Initializing");
    if (initCalled)
        return UAC_ERROR_ALREADY_CALLED;
    initCalled = true;
    instances.emplace(std::array{UACInstance(UAC_CHANNEL_0), UACInstance(UAC_CHANNEL_1)});
    uint32_t ipcMsgCount;
    pool = IPCBufPoolCreate(ipcBufPoolBackingArray.data(), ipcBufPoolBackingArray.size(), 0x180, &ipcMsgCount, 1);
    if (pool == nullptr) {
        LOG_WARN("IPCBufPoolCreate() Failed");
    }
    return UAC_SUCCESS;
}

UACError UACOpen(UACChannel channel, const UACIpcWorkMemory *workMem) {
    if ((channel != UAC_CHANNEL_0 && channel != UAC_CHANNEL_1) || !workMem || !workMem->buffer || !workMem->
        isoDescBuffer)
        return UAC_ERROR_INVALID_ARG;

    if (workMem->bufferSizeBytes == 0 || (workMem->bufferSizeBytes & 0x7FF) != 0)
        return UAC_ERROR_INVALID_IPC_BUFFER_SIZE;

    if (workMem->isoDescBufferSizeBytes == 0 ||
        ((workMem->isoDescBufferSizeBytes / 0x60) != (workMem->bufferSizeBytes >> 0xb)))
        return UAC_ERROR_INVALID_ISO_DESC_IPC_BUFFER_SIZE;
    if (!instances.has_value())
        return UAC_ERROR_UNINITIALIZED;
    auto &inst = (*instances)[channel];
    auto _ = inst.Acquire(true);
    if (inst.iosHandle > 0) {
        return UAC_ERROR_ALREADY_CALLED;
    }
    inst.iosHandle = IOS_Open("/dev/ccr_uac", static_cast<IOSOpenMode>(channel));
    if (inst.iosHandle < 0)
        return UAC_ERROR_IOS_OPEN_FAILED;
    inst.gaInfoQueue = {};
    LOG_WARN("Doing open IOCTL...");
    const auto res = uac_send_ipc_open_msg(inst, workMem);
    if (res != UAC_SUCCESS) {
        uac_close(channel, false);
        inst.iosHandle = -1;
        return res;
    }
    return UAC_SUCCESS;
}

UACError UACClose(UACChannel channel) {
    return uac_close(channel, true);
}

UACError UACFreeISODesc(UACChannel channel, UACISODesc *desc) {
    if ((channel != UAC_CHANNEL_0 && channel != UAC_CHANNEL_1) || !desc)
        return UAC_ERROR_INVALID_ARG;
    if (!instances)
        return UAC_ERROR_NOT_OPEN;
    auto &inst = (*instances)[channel];
    auto _ = inst.Acquire(true);

    auto descIndex = 0u;
    for (; descIndex < MAX_DESCRIPTOR_COUNT; ++descIndex) {
        auto it = inst.descBuffer + descIndex;
        if (it == desc)
            break;
    }
    if (descIndex == MAX_DESCRIPTOR_COUNT)
        return UAC_ERROR_INVALID_ARG;

    UACError allocError;
    auto msg = AllocateIpcMsg(allocError);
    if (!msg)
        return allocError;
    msg->request.ipcReq = uac_ipc_request_id::FreeIsoDesc;
    msg->request.channel = channel;
    msg->request.u.freeIsoDesc.descIndex = descIndex;
    msg->vecs[0].vaddr = &msg->request;
    msg->vecs[0].len = sizeof(msg->request);
    const auto err = IOS_Ioctlv(inst.iosHandle, +uac_ipc_request_id::FreeIsoDesc, 1, 0, msg->vecs);
    FreeIpcMsg(msg);
    if (err)
        return UAC_ERROR_IOCTL_FAILED;
    return UAC_SUCCESS;
}

static void audio_req_callback(IOSError error, void *ipcMsg) {
    auto msg = static_cast<uac_ipc_txn *>(ipcMsg);
    if (!msg) {
        LOG_WARN("ipcMsg is NULL");
        return;
    }
    auto &inst = (*instances)[+msg->request.channel];
    auto _ = inst.gaInfoQueue.acquire();
    const auto descIndex = msg->response.getAudio.descIndex;
    inst.audioRequestCount -= 1;
    if (error != IOS_ERROR_OK) {
        LOG_WARN("Error %d occurred during audio request", error);
        auto current = inst.gaInfoQueue.get_current();
        if (!current) {
            LOG_WARN("Failed to get GA info");
        } else {
            OSSignalEvent(current->event);
            inst.gaInfoQueue.dequeue();
        }
        FreeIpcMsg(msg);
        return;
    }

    if (inst.iosHandle < 0) {
        LOG_WARN("UAC already closed");
        FreeIpcMsg(msg);
        return;
    }

    auto current = inst.gaInfoQueue.get_current();
    if (!current) {
        LOG_WARN("Failed to get GA info");
        FreeIpcMsg(msg);
        return;
    }

    if (!inst.descBuffer) {
        LOG_WARN("ISO descriptor buffer is NULL");
        FreeIpcMsg(msg);
        return;
    }

    auto& desc = inst.descBuffer[descIndex];
    DCInvalidateRange(&desc, sizeof(UACISODesc));

    for (auto* buf : desc.sampleBufs) {
        assert(buf != nullptr);
    }
    *current->outDesc = &desc;
    OSSignalEvent(current->event);
    inst.gaInfoQueue.dequeue();
    FreeIpcMsg(msg);
}

UACError UACGetAudio(UACChannel channel, OSEvent *event, UACISODesc **outDesc) {
    if ((channel != UAC_CHANNEL_0 && channel != UAC_CHANNEL_1) || !outDesc)
        return UAC_ERROR_INVALID_ARG;
    if (!instances)
        return UAC_ERROR_UNINITIALIZED;
    auto &inst = (*instances)[channel];
    auto _ = inst.Acquire(true);
    auto _ = inst.gaInfoQueue.acquire();
    if (inst.iosHandle < 0)
        return UAC_ERROR_NOT_OPEN;
    UACError allocError;
    auto msg = AllocateIpcMsg(allocError);
    if (!msg)
        return allocError;
    inst.gaInfoQueue.enqueue(uac_ga_info(event, outDesc));
    msg->request.channel = channel;
    msg->request.ipcReq = uac_ipc_request_id::GetAudio;
    msg->vecs[0].vaddr = &msg->request;
    msg->vecs[0].len = sizeof(msg->request);
    msg->vecs[1].vaddr = &msg->response;
    msg->vecs[1].len = sizeof(msg->response);
    inst.audioRequestCount += 1;

    const auto res = IOS_IoctlvAsync(inst.iosHandle, +uac_ipc_request_id::GetAudio, 1, 1, msg->vecs, audio_req_callback,
                                     msg);

    if (res != IOS_ERROR_OK) {
        FreeIpcMsg(msg);
        inst.audioRequestCount -= 1;
        inst.gaInfoQueue.undo_enqueue();
        return UAC_ERROR_IOCTL_FAILED;
    }
    return UAC_SUCCESS;
}

UACError UACRequest(UACChannel channel, UACRequestData *request) {
    if ((channel != UAC_CHANNEL_0 && channel != UAC_CHANNEL_1) || !request || !request->buffer || request->size > 0x60)
        return UAC_ERROR_INVALID_ARG;
    if (!instances) {
        return UAC_ERROR_UNINITIALIZED;
    }
    auto &inst = (*instances)[channel];
    auto _ = inst.Acquire(true);
    if (static_cast<uac_ipc_request_id>(request->id) == uac_ipc_request_id::Unk0x80) {
        inst.unknownValueA = request->opt == 2 ? 2 : 1;
        return UAC_SUCCESS;
    }
    if (inst.iosHandle < 0)
        return UAC_ERROR_NOT_OPEN;

    UACError allocError;
    auto msg = AllocateIpcMsg(allocError);
    if (!msg)
        return allocError;
    request->returnedSize = 0;

    msg->request.channel = channel;
    msg->request.ipcReq = static_cast<uac_ipc_request_id>(request->id);

    msg->request.u.request.requestOpt = request->opt;
    msg->request.u.request.unk = request->unk;
    auto intermediaryBuffer = reinterpret_cast<void *>(
        (reinterpret_cast<uintptr_t>(msg) + 255) & ~static_cast<uintptr_t>(63));
    msg->vecs[0].vaddr = &msg->request;
    msg->vecs[0].len = sizeof(msg->request);
    msg->request.u.request.inputSize = request->size;

    const auto isReadRequest = (request->opt & UAC_OPT_READ_FLAG) != 0;
    if (!isReadRequest) {
        std::memcpy(intermediaryBuffer, request->buffer, request->size);
    }
    msg->vecs[1].vaddr = intermediaryBuffer;
    msg->vecs[1].len = request->size;
    msg->vecs[2].vaddr = &msg->response;
    msg->vecs[2].len = sizeof(msg->response);

    LOG_WARN("Doing request: is read?: %d", isReadRequest);
    IOSError iosError;
    if (isReadRequest)
        iosError = IOS_Ioctlv(inst.iosHandle, +uac_ipc_request_id::MiscRequest, 1, 2, msg->vecs);
    else
        iosError = IOS_Ioctlv(inst.iosHandle, +uac_ipc_request_id::MiscRequest, 2, 1, msg->vecs);

    if (iosError != IOS_ERROR_OK) {
        FreeIpcMsg(msg);
        return UAC_ERROR_IOCTL_FAILED;
    }
    request->returnedSize = msg->response.request.actualSize;
    if (isReadRequest) {
        std::memcpy(request->buffer, msg->vecs[1].vaddr, request->size);
    }
    FreeIpcMsg(msg);

    return UAC_SUCCESS;
}

WUMS_EXPORT_FUNCTION(UACInit);
WUMS_EXPORT_FUNCTION(UACClose);
WUMS_EXPORT_FUNCTION(UACOpen);
WUMS_EXPORT_FUNCTION(UACGetAudio);
WUMS_EXPORT_FUNCTION(UACFreeISODesc);
WUMS_EXPORT_FUNCTION(UACRequest);

WUMS_INITIALIZE() {
    instances.reset();
    pool = nullptr;
    initCalled = false;
    OSReport("Initialized UACRepl built at " __TIMESTAMP__ "\n");
}

WUMS_APPLICATION_ENDS() {
    initCalled = false;
}