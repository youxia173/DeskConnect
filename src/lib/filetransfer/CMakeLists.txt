# SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
# SPDX-License-Identifier: MIT

add_library(filetransfer STATIC
  FileTransfer.cpp
  FileTransfer.h
  FileReceiveSession.cpp
  FileReceiveSession.h
  FileSend.cpp
  FileSend.h
  TransferProgress.cpp
  TransferProgress.h
)

# ProtocolUtil lives in app; consumers (server/client/core) already link app.
target_link_libraries(filetransfer PUBLIC common)

target_include_directories(filetransfer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/lib>
)
