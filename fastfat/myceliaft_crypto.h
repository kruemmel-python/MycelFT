/*++

Copyright (c) 2025 Mycelia Security

Module Name:

    MyceliaFtCrypto.h

Abstract:

    This module defines the interfaces for MyceliaFT crypto support.

--*/

#ifndef _MYCELIAFT_CRYPTO_H_
#define _MYCELIAFT_CRYPTO_H_

NTSTATUS
MyceliaFtProcessBuffer(
    _In_ PVCB Vcb,
    _In_ LARGE_INTEGER StartingByte,
    _Inout_updates_bytes_(ByteCount) PUCHAR Buffer,
    _In_ ULONG ByteCount
    );

BOOLEAN
MyceliaFtShouldDecryptRead(
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN PagingIo
    );

BOOLEAN
MyceliaFtShouldEncryptWrite(
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN PagingIo,
    _In_ BOOLEAN NonCachedIo,
    _In_ BOOLEAN Wait
    );

#endif // _MYCELIAFT_CRYPTO_H_
