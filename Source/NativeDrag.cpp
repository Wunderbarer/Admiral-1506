// Admiral 1506 - Datei-Drag unter Windows im Fenster-Thread (siehe NativeDrag.h). Wird nur unter Windows gebaut.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstring>
#include <cwchar>
#include "NativeDrag.h"

namespace
{
// Liste mit einer Datei im Format CF_HDROP (wie der Explorer sie liefert)
HGLOBAL makeHDrop (const wchar_t* path)
{
    const size_t chars = std::wcslen (path);
    const size_t bytes = sizeof (DROPFILES) + (chars + 2) * sizeof (wchar_t);
    HGLOBAL h = GlobalAlloc (GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (h == nullptr) return nullptr;
    auto* df = static_cast<DROPFILES*> (GlobalLock (h));
    if (df == nullptr) { GlobalFree (h); return nullptr; }
    df->pFiles = sizeof (DROPFILES);
    df->fWide = TRUE;
    auto* names = reinterpret_cast<wchar_t*> (reinterpret_cast<char*> (df) + sizeof (DROPFILES));
    std::wmemcpy (names, path, chars);
    names[chars] = 0;
    names[chars + 1] = 0;                      // Liste endet mit doppelter Null
    GlobalUnlock (h);
    return h;
}

HGLOBAL copyGlobal (HGLOBAL src)
{
    const SIZE_T size = GlobalSize (src);
    HGLOBAL dst = GlobalAlloc (GMEM_MOVEABLE, size);
    if (dst == nullptr) return nullptr;
    void* s = GlobalLock (src);
    void* d = GlobalLock (dst);
    if (s != nullptr && d != nullptr) std::memcpy (d, s, size);
    if (d != nullptr) GlobalUnlock (dst);
    if (s != nullptr) GlobalUnlock (src);
    return dst;
}

class DropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) return E_POINTER;
        if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IDropSource))
        {
            *ppv = static_cast<IDropSource*> (this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) InterlockedIncrement (&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement (&refs);
        if (r == 0) delete this;
        return (ULONG) r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag (BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed) return DRAGDROP_S_CANCEL;
        if ((keyState & (MK_LBUTTON | MK_RBUTTON)) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback (DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    LONG refs = 1;
};

class DataObject final : public IDataObject
{
public:
    explicit DataObject (HGLOBAL hdropIn) : hdrop (hdropIn)
    {
        format.cfFormat = CF_HDROP;
        format.ptd = nullptr;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;
    }
    ~DataObject() { if (hdrop != nullptr) GlobalFree (hdrop); }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) return E_POINTER;
        if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IDataObject))
        {
            *ppv = static_cast<IDataObject*> (this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) InterlockedIncrement (&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement (&refs);
        if (r == 0) delete this;
        return (ULONG) r;
    }

    HRESULT STDMETHODCALLTYPE GetData (FORMATETC* fmt, STGMEDIUM* medium) override
    {
        if (fmt == nullptr || medium == nullptr) return E_INVALIDARG;
        if (QueryGetData (fmt) != S_OK) return DV_E_FORMATETC;
        HGLOBAL copy = copyGlobal (hdrop);
        if (copy == nullptr) return E_OUTOFMEMORY;
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = copy;
        medium->pUnkForRelease = nullptr;      // der Empfänger gibt die Kopie frei
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere (FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData (FORMATETC* fmt) override
    {
        if (fmt == nullptr) return E_INVALIDARG;
        if (fmt->cfFormat != CF_HDROP) return DV_E_FORMATETC;
        if ((fmt->tymed & TYMED_HGLOBAL) == 0) return DV_E_TYMED;
        if (fmt->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc (FORMATETC*, FORMATETC* out) override
    {
        if (out != nullptr) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData (FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc (DWORD direction, IEnumFORMATETC** out) override
    {
        if (out == nullptr) return E_INVALIDARG;
        if (direction != DATADIR_GET) { *out = nullptr; return E_NOTIMPL; }
        return SHCreateStdEnumFmtEtc (1, &format, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise (FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise (DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise (IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    LONG refs = 1;
    HGLOBAL hdrop = nullptr;
    FORMATETC format {};
};
} // namespace

namespace adwin
{
bool dragFile (const wchar_t* path)
{
    if (path == nullptr || *path == 0) return false;
    const HRESULT init = OleInitialize (nullptr);
    if (FAILED (init)) return false;           // z. B. Thread im falschen COM-Modus: dann den JUCE-Weg nehmen

    bool done = false;
    if (HGLOBAL h = makeHDrop (path))
    {
        auto* data = new DataObject (h);
        auto* source = new DropSource();
        DWORD effect = DROPEFFECT_NONE;
        const HRESULT r = DoDragDrop (data, source, DROPEFFECT_COPY, &effect);
        done = (r == DRAGDROP_S_DROP || r == DRAGDROP_S_CANCEL);
        source->Release();
        data->Release();
    }
    OleUninitialize();
    return done;
}
} // namespace adwin
