// Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream)]
struct FuStructMntPico {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum FuMntPicoStatus {
    Unknown,
    Failed,
}
