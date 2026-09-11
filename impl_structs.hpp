#pragma once
#include "uac.hpp"

#include <coreinit/ios.h>
#include <coreinit/mutex.h>
#include <coreinit/event.h>

#include <array>
#include <cstdint>
#include "util.hpp"


struct uac_ga_info {
    OSEvent *event;
    UACISODesc **outDesc;
};

class uac_ga_ring_buffer {
    OSMutex m_mutex{};
    uint32_t m_writeIndex{};
    uint32_t m_readIndex{};
    std::array<uac_ga_info, 8> m_array{};

public:
    uac_ga_ring_buffer() noexcept = default;

    uac_ga_info *get_current() noexcept {
        if (m_array[m_readIndex].event)
            return &m_array[m_readIndex];
        return nullptr;
    }

    UACError enqueue(uac_ga_info info) noexcept {
        if (m_array[m_writeIndex].event)
            return UAC_ERROR_IPC_HAS_OLD_EVENT;
        m_array[m_writeIndex] = info;
        m_writeIndex = (m_writeIndex + 1) % m_array.size();
        return UAC_SUCCESS;
    }

    // Effectively uac.rpl|0x02000154
    void dequeue() noexcept {
        m_array[m_readIndex] = {};
        m_readIndex = (m_readIndex + 1) % m_array.size();
    }

    // Effectively uac.rpl|0x020001bc
    void undo_enqueue() noexcept {
        m_writeIndex = (m_writeIndex - 1) % m_array.size();
        m_array[m_writeIndex] = {};
    }

    // Serves purpose of uac.rpl|0x02000238 and uac.rpl|0x02000240
    [[nodiscard]] cz::cos_util::cos_conditional_lock acquire() noexcept {
        return {m_mutex, true};
    }


    void reset() noexcept {
        m_writeIndex = 0;
        m_readIndex = 0;
        m_array.fill({});
        OSInitMutexEx(&m_mutex, "UACReplRingBufferMtx");
    }
};

enum class uac_ipc_request_id : uint32_t {
    Open = 0x01,
    Close = 0x02,
    GetAudio = 0x03,
    MiscRequest = 0x04,
    FreeIsoDesc = 0x05,
    Unk0x80 = 0x80
};

struct uac_ipc_request {
    UACChannel channel;
    uac_ipc_request_id ipcReq;

    union {
        struct {
            uint32_t unknownValueA;
            void *ipcBuffer;
        } open;

        struct {
            uint32_t unused;
            uint32_t descIndex;
        } freeIsoDesc;

        struct {
            uint32_t requestOpt;
            uint32_t unk;
            uint32_t inputSize;
        } request;

        WUT_PADDING_BYTES(12);
    } u;
};

WUT_CHECK_OFFSET(uac_ipc_request, 0x00, channel);
WUT_CHECK_OFFSET(uac_ipc_request, 0x04, ipcReq);
WUT_CHECK_OFFSET(uac_ipc_request, 0x08, u.open.unknownValueA);
WUT_CHECK_OFFSET(uac_ipc_request, 0x0c, u.open.ipcBuffer);
WUT_CHECK_OFFSET(uac_ipc_request, 0x08, u.freeIsoDesc.unused);
WUT_CHECK_OFFSET(uac_ipc_request, 0x0c, u.freeIsoDesc.descIndex);
WUT_CHECK_SIZE(uac_ipc_request, 0x14);

union uac_ipc_response {
    struct {
        uint32_t descIndex;
    } getAudio;

    struct {
        [[maybe_unused]] uint32_t unused;
        uint32_t actualSize;
    } request;
};

static_assert(std::is_implicit_lifetime_v<uac_ipc_response>);

WUT_CHECK_OFFSET(uac_ipc_response, 0x00, getAudio.descIndex);
WUT_CHECK_OFFSET(uac_ipc_response, 0x04, request.actualSize);
WUT_CHECK_SIZE(uac_ipc_response, 0x08);

struct uac_ipc_txn {
    IOSVec vecs[3];
    WUT_PADDING_BYTES(92);
    uac_ipc_request request;
    WUT_PADDING_BYTES(172);
    uac_ipc_response response;
    WUT_PADDING_BYTES(56);
};

WUT_CHECK_OFFSET(uac_ipc_txn, 0x000, vecs);
WUT_CHECK_OFFSET(uac_ipc_txn, 0x080, request);
WUT_CHECK_OFFSET(uac_ipc_txn, 0x140, response);
WUT_CHECK_SIZE(uac_ipc_txn, 0x180);