/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <lib/core/Optional.h>
#include <lib/support/ScopedBuffer.h>
#include <lib/support/Span.h>

namespace chip {

#define CHIP_ERROR_TLV_PROCESSOR(e)                                                                                                \
    CHIP_GENERIC_ERROR(ChipError::Range::kLastRange, ((uint8_t) ChipError::Range::kLastRange << 3) | e)

#define CHIP_ERROR_OTA_CHANGE_PROCESSOR CHIP_ERROR_TLV_PROCESSOR(0x02)
#define CHIP_ERROR_OTA_PROCESSOR_NOT_REGISTERED CHIP_ERROR_TLV_PROCESSOR(0x03)
#define CHIP_ERROR_OTA_PROCESSOR_ALREADY_REGISTERED CHIP_ERROR_TLV_PROCESSOR(0x04)
#define CHIP_ERROR_OTA_PROCESSOR_CLIENT_INIT CHIP_ERROR_TLV_PROCESSOR(0x05)
#define CHIP_ERROR_OTA_PROCESSOR_MAKE_ROOM CHIP_ERROR_TLV_PROCESSOR(0x06)
#define CHIP_ERROR_OTA_PROCESSOR_PUSH_CHUNK CHIP_ERROR_TLV_PROCESSOR(0x07)
#define CHIP_ERROR_OTA_PROCESSOR_IMG_AUTH CHIP_ERROR_TLV_PROCESSOR(0x08)
#define CHIP_ERROR_OTA_FETCH_ALREADY_SCHEDULED CHIP_ERROR_TLV_PROCESSOR(0x09)
#define CHIP_ERROR_OTA_PROCESSOR_IMG_COMMIT CHIP_ERROR_TLV_PROCESSOR(0x0A)
#define CHIP_ERROR_OTA_PROCESSOR_CB_NOT_REGISTERED CHIP_ERROR_TLV_PROCESSOR(0x0B)
#define CHIP_ERROR_OTA_PROCESSOR_EEPROM_OFFSET CHIP_ERROR_TLV_PROCESSOR(0x0C)
#define CHIP_ERROR_OTA_PROCESSOR_EXTERNAL_STORAGE CHIP_ERROR_TLV_PROCESSOR(0x0D)
#define CHIP_ERROR_OTA_PROCESSOR_START_IMAGE CHIP_ERROR_TLV_PROCESSOR(0x0E)
#define CHIP_ERROR_OTA_PROCESSOR_DO_NOT_APPLY CHIP_ERROR_TLV_PROCESSOR(0x0F)

// Descriptor constants
constexpr size_t kVersionStringSize = 64;
constexpr size_t kBuildDateSize     = 64;

constexpr uint16_t requestedOtaMaxBlockSize = 1024;

/**
 * Used alongside RegisterDescriptorCallback to register
 * a custom descriptor processing function with a certain
 * TLV processor.
 */
typedef CHIP_ERROR (*ProcessDescriptor)(void * descriptor);

struct OTATlvHeader
{
    uint32_t tag;
    uint32_t length;
};

/**
 * This class defines an interface for a Matter TLV processor.
 * Instances of derived classes can be registered as processors
 * in OTAImageProcessorImpl. Based on the TLV type, a certain
 * processor is used to process subsequent blocks until the number
 * of bytes found in the metadata is processed. In case a block contains
 * data from two different TLVs, the processor should ensure the remaining
 * data is returned in the block passed as input.
 * The default processors: application, SSBL and factory data are registered
 * in OTAImageProcessorImpl::Init through OtaHookInit.
 * Applications should use OTAImageProcessorImpl::RegisterProcessor
 * to register additional processors.
 */
class OTATlvProcessor
{
public:
    enum class ApplyState : uint8_t
    {
        kApply = 0,
        kDoNotApply
    };

    virtual ~OTATlvProcessor() {}

    virtual CHIP_ERROR Init()        = 0;
    virtual CHIP_ERROR Clear()       = 0;
    virtual CHIP_ERROR AbortAction() = 0;
    virtual CHIP_ERROR ExitAction() { return CHIP_NO_ERROR; }
    virtual CHIP_ERROR ApplyAction();

    CHIP_ERROR Process(ByteSpan & block);
    void RegisterDescriptorCallback(ProcessDescriptor callback) { mCallbackProcessDescriptor = callback; }
    void SetLength(uint32_t length) { mLength = length; }
    void SetWasSelected(bool selected) { mWasSelected = selected; }
    bool WasSelected() { return mWasSelected; }
#if OTA_ENCRYPTION_ENABLE
    CHIP_ERROR vOtaProcessInternalEncryption(MutableByteSpan & block);
#endif

protected:
    /**
     * @brief Process custom TLV payload
     *
     * The method takes subsequent chunks of the Matter OTA image file and processes them.
     * If more image chunks are needed, CHIP_ERROR_BUFFER_TOO_SMALL error is returned.
     * Other error codes indicate that an error occurred during processing. Fetching
     * next data is scheduled automatically by OTAImageProcessorImpl if the return value
     * is neither an error code, nor CHIP_ERROR_OTA_FETCH_ALREADY_SCHEDULED (which implies the
     * scheduling is done inside ProcessInternal or will be done in the future, through a
     * callback).
     *
     * @param block Byte span containing a subsequent Matter OTA image chunk. When the method
     *              returns CHIP_NO_ERROR, the byte span is used to return a remaining part
     *              of the chunk, not used by current TLV processor.
     *
     * @retval CHIP_NO_ERROR                          Block was processed successfully.
     * @retval CHIP_ERROR_BUFFER_TOO_SMALL            Provided buffers are insufficient to decode some
     *                                                metadata (e.g. a descriptor).
     * @retval CHIP_ERROR_OTA_FETCH_ALREADY_SCHEDULED Should be returned if ProcessInternal schedules
     *                                                fetching next data (e.g. through a callback).
     * @retval Error code                             Something went wrong. Current OTA process will be
     *                                                canceled.
     */
    virtual CHIP_ERROR ProcessInternal(ByteSpan & block) = 0;

    void ClearInternal();

    bool IsError(CHIP_ERROR & status);

#if OTA_ENCRYPTION_ENABLE
    /*ota decryption*/
    uint32_t mIVOffset = 0;
    /* Expected byte size of the OTAEncryptionKeyLength */
    static constexpr size_t kOTAEncryptionKeyLength = 16;
#endif
    uint32_t mLength          = 0;
    uint32_t mProcessedLength = 0;
    bool mWasSelected         = false;

    /**
     * @brief A flag to account for corner cases during OTA apply
     *
     * Used by the default ApplyAction implementation.
     *
     * If something goes wrong during ExitAction of the TLV processor,
     * then mApplyState should be set to kDoNotApply and the image processor
     * should abort. In this case, the BDX transfer was already finished
     * and calling CancelImageUpdate will not abort the transfer, hence
     * the device will reboot even though it should not have. If ApplyAction
     * fails during HandleApply, then the process will be aborted.
     */
    ApplyState mApplyState                       = ApplyState::kApply;
    ProcessDescriptor mCallbackProcessDescriptor = nullptr;
};

/**
 * This class can be used to accumulate data until a given threshold.
 * Should be used by OTATlvProcessor derived classes if they need
 * metadata accumulation (e.g. for custom header decoding).
 */
class OTADataAccumulator
{
public:
    void Init(uint32_t threshold);
    void Clear();
    CHIP_ERROR Accumulate(ByteSpan & block);

    inline uint8_t * data() { return mBuffer.Get(); }
    inline uint32_t GetThreshold() { return mThreshold; }

private:
    uint32_t mThreshold;
    uint32_t mBufferOffset;
    Platform::ScopedMemoryBuffer<uint8_t> mBuffer;
};

} // namespace chip
