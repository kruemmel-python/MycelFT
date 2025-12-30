/*++

Copyright (c) 2025 Mycelia Security

Module Name:

    MyceliaFtCrypto.c

Abstract:

    This module provides MyceliaFT crypto support, mirroring the Mycelia
    bio/quantum-inspired CTR-style keystream flow with a CPU fallback.

--*/

#include "FatProcs.h"
#include "myceliaft_crypto.h"

#define MYCELIAFT_BLOCK_SIZE           (65536ULL)
#define MYCELIAFT_BLOCK_GOLDEN_RATIO   (0x9E3779B97F4A7C15ULL)
#define MYCELIAFT_STREAM_MIX           (0xD1B54A32D192ED03ULL)

static __forceinline ULONGLONG
MyceliaFtSplitMix64(
    _In_ ULONGLONG Value
    )
{
    Value ^= Value >> 30;
    Value *= 0xBF58476D1CE4E5B9ULL;
    Value ^= Value >> 27;
    Value *= 0x94D049BB133111EBULL;
    Value ^= Value >> 31;
    return Value;
}

static __forceinline ULONGLONG
MyceliaFtNextKey(
    _Inout_ ULONGLONG* State
    )
{
    *State += MYCELIAFT_BLOCK_GOLDEN_RATIO;
    return MyceliaFtSplitMix64(*State);
}

static ULONGLONG
MyceliaFtDeriveSeed(
    _In_ PVCB Vcb
    )
{
    ULONGLONG seed = 0;

    if (Vcb != NULL && Vcb->Vpb != NULL) {
        seed = ((ULONGLONG)Vcb->Vpb->SerialNumber << 32) ^
               (ULONGLONG)Vcb->Vpb->SerialNumber;
    }

    if (seed == 0) {
        seed = (ULONGLONG)(ULONG_PTR)Vcb;
    }

    return seed ^ MYCELIAFT_STREAM_MIX;
}

NTSTATUS
MyceliaFtProcessBuffer(
    _In_ PVCB Vcb,
    _In_ LARGE_INTEGER StartingByte,
    _Inout_updates_bytes_(ByteCount) PUCHAR Buffer,
    _In_ ULONG ByteCount
    )
{
    ULONGLONG masterSeed;
    ULONGLONG streamOffset;
    SIZE_T processed;

    if (Buffer == NULL || Vcb == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (ByteCount == 0) {
        return STATUS_SUCCESS;
    }

    if (StartingByte.QuadPart < 0) {
        return STATUS_INVALID_PARAMETER;
    }

    masterSeed = MyceliaFtDeriveSeed(Vcb);
    streamOffset = (ULONGLONG)StartingByte.QuadPart;
    processed = 0;

    while (processed < ByteCount) {
        ULONGLONG absolutePos = streamOffset + processed;
        ULONGLONG blockIndex = absolutePos / MYCELIAFT_BLOCK_SIZE;
        ULONGLONG offsetInBlock = absolutePos % MYCELIAFT_BLOCK_SIZE;
        SIZE_T remaining = ByteCount - processed;
        SIZE_T availableInBlock = (SIZE_T)(MYCELIAFT_BLOCK_SIZE - offsetInBlock);
        SIZE_T toProcess = (remaining < availableInBlock) ? remaining : availableInBlock;
        ULONGLONG blockSeed = masterSeed ^ (blockIndex * MYCELIAFT_BLOCK_GOLDEN_RATIO);
        ULONGLONG state = blockSeed ^ MYCELIAFT_STREAM_MIX;
        SIZE_T keyIndex = (SIZE_T)(offsetInBlock / sizeof(ULONGLONG));
        SIZE_T keyOffset = (SIZE_T)(offsetInBlock % sizeof(ULONGLONG));
        SIZE_T localOffset = 0;

        for (SIZE_T skip = 0; skip < keyIndex; ++skip) {
            (void)MyceliaFtNextKey(&state);
        }

        while (localOffset < toProcess) {
            ULONGLONG key = MyceliaFtNextKey(&state);
            for (SIZE_T byteIndex = keyOffset;
                 byteIndex < sizeof(ULONGLONG) && localOffset < toProcess;
                 ++byteIndex) {
                Buffer[processed + localOffset] ^= (UCHAR)(key >> (byteIndex * 8));
                ++localOffset;
            }
            keyOffset = 0;
        }

        processed += toProcess;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
MyceliaFtShouldDecryptRead(
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN PagingIo
    )
{
    if (PagingIo) {
        return FALSE;
    }

    return (TypeOfOpen == UserFileOpen);
}

BOOLEAN
MyceliaFtShouldEncryptWrite(
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN PagingIo,
    _In_ BOOLEAN NonCachedIo,
    _In_ BOOLEAN Wait
    )
{
    if (PagingIo || NonCachedIo || !Wait) {
        return FALSE;
    }

    return (TypeOfOpen == UserFileOpen);
}
