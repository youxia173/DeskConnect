/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "filetransfer/FileTransfer.h"
#include "io/IStream.h"

namespace deskflow {

//! Write DDRG + DFTR for the given offers. Returns false on failure/skip.
bool writeFileOffersToStream(IStream *stream, const std::vector<FileOffer> &offers);

} // namespace deskflow
