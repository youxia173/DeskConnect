/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <Windows.h>
#include <objidl.h>
#include <shldisp.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

// An OLE clipboard object allows Explorer to extract on its background copy
// thread. Raw WM_RENDERFORMAT has a Windows timeout unsuitable for large files.
class MSWindowsFileDataObject final : public IDataObject, public IDataObjectAsyncCapability
{
public:
  explicit MSWindowsFileDataObject(std::function<std::vector<std::string>()> pull);
  void ready() { m_ready = true; }
  void cancel();
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override;
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override;
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *output) override;
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *medium, BOOL release) override;
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **enumerator) override;
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value) override { m_async = value; return S_OK; }
  HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL *value) override;
  HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx *) override;
  HRESULT STDMETHODCALLTYPE InOperation(BOOL *value) override;
  HRESULT STDMETHODCALLTYPE EndOperation(HRESULT, IBindCtx *, DWORD) override;

private:
  std::atomic<ULONG> m_references{1};
  std::function<std::vector<std::string>()> m_pull;
  std::vector<std::string> m_paths;
  BOOL m_async = TRUE;
  BOOL m_operation = FALSE;
  bool m_valid = true;
  bool m_ready = false;
  bool m_fetching = false;
  CLIPFORMAT m_effect;
  CLIPFORMAT m_ownership;
};
