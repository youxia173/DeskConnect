/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/MSWindowsFileDataObject.h"
#include "platform/MSWindowsClipboardFileConverter.h"
#include "filetransfer/FileTransfer.h"
#include <shlobj.h>
#include <utility>

MSWindowsFileDataObject::MSWindowsFileDataObject(std::function<std::vector<std::string>()> pull)
    : m_pull(std::move(pull)),
      m_effect(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Preferred DropEffect"))),
      m_ownership(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Deskflow Ownership")))
{
}

void MSWindowsFileDataObject::cancel()
{
  m_valid = false;
  m_pull = {};
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
  if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
  if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  if (format->lindex != -1) return DV_E_LINDEX;
  return format->cfFormat == CF_HDROP || format->cfFormat == m_effect || format->cfFormat == m_ownership ?
      S_OK : DV_E_FORMATETC;
}
HRESULT MSWindowsFileDataObject::GetData(FORMATETC *format, STGMEDIUM *medium)
{
  if (!medium) return E_POINTER;
  *medium = {};
  const auto result = QueryGetData(format);
  if (FAILED(result)) return result;
  HGLOBAL data = nullptr;
  if (format->cfFormat == CF_HDROP) {
    // OleSetClipboard can synchronously notify clipboard viewers. A viewer
    // must not start nested core dispatch before installation has returned.
    if (!m_ready) return E_PENDING;
    if (!m_valid || m_fetching) return E_ABORT;
    // Keep this object alive if clipboard ownership changes during dispatch.
    AddRef();
    if (m_paths.empty() && m_pull) {
      m_fetching = true;
      const auto pull = m_pull;
      try {
        m_paths = pull();
      } catch (...) {
        m_paths.clear();
      }
      m_fetching = false;
    }
    if (m_valid && !m_paths.empty()) {
      MSWindowsClipboardFileConverter converter;
      data = static_cast<HGLOBAL>(converter.fromIClipboard(deskflow::clipboardDataFromPaths(m_paths)));
    }
    Release();
  } else {
    data = GlobalAlloc(GHND, sizeof(DWORD));
    if (data) {
      auto *value = static_cast<DWORD *>(GlobalLock(data));
      if (!value) { GlobalFree(data); return E_OUTOFMEMORY; }
      *value = format->cfFormat == m_effect ? DROPEFFECT_COPY : 0;
      GlobalUnlock(data);
    }
  }
  if (!data) return E_ABORT;
  medium->tymed = TYMED_HGLOBAL;
  medium->hGlobal = data;
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
      {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
      {m_effect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
      {m_ownership, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
  return SHCreateStdEnumFmtEtc(3, formats, enumerator);
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
