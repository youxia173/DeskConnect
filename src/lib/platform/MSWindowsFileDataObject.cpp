/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/MSWindowsFileDataObject.h"
#include "filetransfer/FileTransfer.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <QDir>
#include <QFileInfo>
#include "base/Log.h"
#include <algorithm>
#include <cstring>
#include <utility>

// Metadata probes, stream creation and Stat never fetch remote bytes. Only a
// consumer actually reading a file (Explorer paste) opens the remote content.
namespace {
class ClipboardContentStream final : public IStream
{
public:
  ClipboardContentStream(MSWindowsFileDataObject *owner, size_t index, uint64_t size)
      : m_owner(owner), m_index(index), m_size(size) { m_owner->AddRef(); }
  ~ClipboardContentStream() { if (m_stream) m_stream->Release(); m_owner->Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream) return E_NOINTERFACE;
    *out = static_cast<IStream *>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
  ULONG STDMETHODCALLTYPE Release() override { auto n = --m_refs; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE Read(void *buffer, ULONG count, ULONG *read) override {
    if (read) *read = 0;
    if (!buffer && count) return STG_E_INVALIDPOINTER;
    if (!count) return S_OK;
    if (!m_stream) {
      const auto hr = m_owner->openContent(m_index, &m_stream);
      if (FAILED(hr)) return hr;
    }
    LARGE_INTEGER offset; offset.QuadPart = static_cast<LONGLONG>(m_position);
    auto hr = m_stream->Seek(offset, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;
    const auto remaining = m_position < m_size ? m_size - m_position : 0;
    const ULONG wanted = static_cast<ULONG>(std::min<uint64_t>(count, remaining));
    ULONG actual = 0;
    hr = m_stream->Read(buffer, wanted, &actual);
    m_position += actual;
    if (read) *read = actual;
    return FAILED(hr) ? hr : (actual == count ? S_OK : S_FALSE);
  }
  HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position) override {
    if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
    const uint64_t base = origin == STREAM_SEEK_SET ? 0 : (origin == STREAM_SEEK_CUR ? m_position : m_size);
    const auto delta = move.QuadPart;
    if (delta < 0 && static_cast<uint64_t>(-(delta + 1)) + 1 > base) return STG_E_INVALIDFUNCTION;
    if (delta >= 0 && static_cast<uint64_t>(delta) > static_cast<uint64_t>(INT64_MAX) - base) return STG_E_INVALIDFUNCTION;
    m_position = delta < 0 ? base - (static_cast<uint64_t>(-(delta + 1)) + 1) : base + delta;
    if (position) position->QuadPart = m_position;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream *out, ULARGE_INTEGER count, ULARGE_INTEGER *read, ULARGE_INTEGER *written) override {
    if (read) read->QuadPart = 0;
    if (written) written->QuadPart = 0;
    if (!out) return STG_E_INVALIDPOINTER;
    char buffer[65536]; uint64_t total = 0;
    while (total < count.QuadPart) {
      ULONG got = 0, put = 0;
      auto hr = Read(buffer, static_cast<ULONG>(std::min<uint64_t>(sizeof(buffer), count.QuadPart - total)), &got);
      if (read) read->QuadPart += got;
      if (FAILED(hr)) return hr;
      if (!got) return S_FALSE;
      hr = out->Write(buffer, got, &put);
      if (written) written->QuadPart += put;
      if (FAILED(hr)) return hr;
      if (put != got) return STG_E_MEDIUMFULL;
      total += got;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD) override {
    if (!stat) return STG_E_INVALIDPOINTER;
    *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = m_size; stat->grfMode = STGM_READ; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream **) override { return E_NOTIMPL; }
private:
  std::atomic<ULONG> m_refs{1};
  MSWindowsFileDataObject *m_owner;
  size_t m_index;
  uint64_t m_size;
  uint64_t m_position = 0;
  IStream *m_stream = nullptr;
};
}

MSWindowsFileDataObject::MSWindowsFileDataObject(std::function<std::vector<std::string>()> pull,
    std::function<std::vector<deskflow::FileOffer>()> list)
    : m_pull(std::move(pull)), m_list(std::move(list)),
      m_descriptor(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW))),
      m_contents(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"))),
      m_effect(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Preferred DropEffect"))),
      m_ownership(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Deskflow Ownership")))
{
}

bool MSWindowsFileDataObject::loadEntries()
{
  if (!m_valid || !m_ready || m_fetching) return false;
  if (!m_entries.empty()) return true;
  m_fetching = true;
  const auto list = m_list;
  try { if (list) m_entries = list(); } catch (...) { m_entries.clear(); }
  m_fetching = false;
  if (!m_valid) return false;
  for (const auto &entry : m_entries) {
    if (!deskflow::isSafeTransferPath(entry.name) || QString::fromStdString(entry.name).size() >= MAX_PATH ||
        entry.size > static_cast<uint64_t>(INT64_MAX)) {
      m_entries.clear();
      return false;
    }
  }
  return !m_entries.empty();
}

HRESULT MSWindowsFileDataObject::openContent(size_t index, IStream **stream)
{
  *stream = nullptr;
  if (!m_valid || m_fetching || index >= m_entries.size()) return E_ABORT;
  if (m_paths.empty()) {
    m_fetching = true;
    const auto pull = m_pull;
    LOG_INFO("clipboard files: Windows consumer started reading pasted file contents");
    try { if (pull) m_paths = pull(); } catch (...) { m_paths.clear(); }
    m_fetching = false;
  }
  if (!m_valid || m_paths.empty()) return E_ABORT;
  const auto &entry = m_entries[index];
  const QString relative = QString::fromStdString(entry.name);
  // Resolve only beneath a completed root. Never expose the cache directory as
  // an item to Explorer and never use a source computer's absolute path.
  for (const auto &root : m_paths) {
    const QFileInfo info(QString::fromStdString(root));
    if (info.fileName() != relative.section('/', 0, 0)) continue;
    const auto path = QDir(info.absolutePath()).filePath(relative);
    if (entry.name.back() == '/' || !QFileInfo(path).isFile() || QFileInfo(path).size() < static_cast<qint64>(entry.size)) return E_ABORT;
    return SHCreateStreamOnFileEx(reinterpret_cast<LPCWSTR>(path.utf16()),
        STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, stream);
  }
  return E_ABORT;
}

void MSWindowsFileDataObject::cancel()
{
  m_valid = false;
  m_pull = {};
  m_list = {};
}

HRESULT MSWindowsFileDataObject::QueryInterface(REFIID iid, void **object)
{
  if (!object) return E_POINTER;
  *object = nullptr;
  if (iid == IID_IUnknown || iid == IID_IDataObject) {
    *object = static_cast<IDataObject *>(this);
  } else if (iid == __uuidof(IDataObjectAsyncCapability)) {
    *object = static_cast<IDataObjectAsyncCapability *>(this);
  } else {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}
ULONG MSWindowsFileDataObject::AddRef() { return ++m_references; }
ULONG MSWindowsFileDataObject::Release()
{
  const auto references = --m_references;
  if (!references) delete this;
  return references;
}
HRESULT MSWindowsFileDataObject::QueryGetData(FORMATETC *format)
{
  if (!format) return E_POINTER;
  if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  if (format->cfFormat == m_contents) {
    if (!(format->tymed & TYMED_ISTREAM)) return DV_E_TYMED;
    // Shell probes the enumerated format with lindex=-1; an actual GetData
    // request must name an entry and is checked separately below.
    return format->lindex >= -1 ? S_OK : DV_E_LINDEX;
  }
  if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
  if (format->lindex != -1) return DV_E_LINDEX;
  return format->cfFormat == m_descriptor || format->cfFormat == m_effect || format->cfFormat == m_ownership ? S_OK : DV_E_FORMATETC;
}
HRESULT MSWindowsFileDataObject::GetData(FORMATETC *format, STGMEDIUM *medium)
{
  if (!medium) return E_POINTER;
  *medium = {};
  const auto result = QueryGetData(format);
  if (FAILED(result)) return result;
  if (format->cfFormat == m_descriptor || format->cfFormat == m_contents) {
    if (!m_ready) return E_PENDING;
    AddRef();
    const bool loaded = loadEntries();
    if (!loaded) { Release(); return E_ABORT; }
    if (format->cfFormat == m_contents) {
      const auto index = static_cast<size_t>(format->lindex);
      if (index >= m_entries.size() || m_entries[index].name.back() == '/') { Release(); return DV_E_LINDEX; }
      medium->tymed = TYMED_ISTREAM;
      medium->pstm = new ClipboardContentStream(this, index, m_entries[index].size);
      Release();
      return S_OK;
    }
    const auto bytes = FIELD_OFFSET(FILEGROUPDESCRIPTORW, fgd) + m_entries.size() * sizeof(FILEDESCRIPTORW);
    HGLOBAL data = GlobalAlloc(GHND, bytes);
    auto *group = data ? static_cast<FILEGROUPDESCRIPTORW *>(GlobalLock(data)) : nullptr;
    if (!group) { if (data) GlobalFree(data); Release(); return E_OUTOFMEMORY; }
    group->cItems = static_cast<UINT>(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i) {
      const auto &entry = m_entries[i];
      auto &descriptor = group->fgd[i];
      const bool directory = entry.name.back() == '/';
      auto name = QString::fromStdString(entry.name);
      if (directory) name.chop(1);
      name.replace('/', '\\');
      descriptor.dwFlags = FD_ATTRIBUTES | FD_UNICODE | FD_PROGRESSUI;
      descriptor.dwFileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
      if (!directory) {
        descriptor.dwFlags |= FD_FILESIZE;
        descriptor.nFileSizeHigh = static_cast<DWORD>(entry.size >> 32);
        descriptor.nFileSizeLow = static_cast<DWORD>(entry.size);
      }
      std::memcpy(descriptor.cFileName, name.utf16(), static_cast<size_t>(name.size()) * sizeof(wchar_t));
    }
    GlobalUnlock(data);
    medium->tymed = TYMED_HGLOBAL; medium->hGlobal = data;
    Release();
    return S_OK;
  }
  HGLOBAL data = GlobalAlloc(GHND, sizeof(DWORD));
  auto *value = data ? static_cast<DWORD *>(GlobalLock(data)) : nullptr;
  if (!value) { if (data) GlobalFree(data); return E_OUTOFMEMORY; }
  *value = format->cfFormat == m_effect ? DROPEFFECT_COPY : 0;
  GlobalUnlock(data);
  medium->tymed = TYMED_HGLOBAL; medium->hGlobal = data;
  return S_OK;
}
HRESULT MSWindowsFileDataObject::GetCanonicalFormatEtc(FORMATETC *, FORMATETC *output)
{
  if (!output) return E_POINTER;
  output->ptd = nullptr;
  return E_NOTIMPL;
}
HRESULT MSWindowsFileDataObject::SetData(FORMATETC *, STGMEDIUM *medium, BOOL release)
{
  // Shell completion/drop-effect notifications never turn a remote copy into
  // a move. Source files and staging files remain available for repeated paste.
  if (!medium) return E_POINTER;
  if (release) ReleaseStgMedium(medium);
  return S_OK;
}
HRESULT MSWindowsFileDataObject::EnumFormatEtc(DWORD direction, IEnumFORMATETC **enumerator)
{
  if (!enumerator) return E_POINTER;
  *enumerator = nullptr;
  if (direction != DATADIR_GET) return E_NOTIMPL;
  FORMATETC formats[] = {
      {m_descriptor, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
      {m_contents, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
      {m_effect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
      {m_ownership, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
  return SHCreateStdEnumFmtEtc(4, formats, enumerator);
}
HRESULT MSWindowsFileDataObject::GetAsyncMode(BOOL *value)
{
  if (!value) return E_POINTER;
  *value = m_async; return S_OK;
}
HRESULT MSWindowsFileDataObject::StartOperation(IBindCtx *)
{
  if (!m_valid) return E_ABORT;
  m_operation = TRUE; return S_OK;
}
HRESULT MSWindowsFileDataObject::InOperation(BOOL *value)
{
  if (!value) return E_POINTER;
  *value = m_operation; return S_OK;
}
HRESULT MSWindowsFileDataObject::EndOperation(HRESULT, IBindCtx *, DWORD)
{
  m_operation = FALSE; return S_OK;
}
