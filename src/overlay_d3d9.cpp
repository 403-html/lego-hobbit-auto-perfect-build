// Auto Perfect Build - draws the "Mods" panel with the game's own Direct3D 9
// device while the game is paused.
//
// Hook chain (no game code is modified for this):
//   LEGOHobbit.exe IAT d3d9!Direct3DCreate9  -> wraps the returned IDirect3D9
//   IDirect3D9::CreateDevice (vtable slot 16) -> learns the device
//   IDirect3DDevice9::Present (vtable slot 17) -> draws one textured quad
// Text is rendered with GDI into a DIB and uploaded to a D3DPOOL_MANAGED texture,
// so nothing has to be recreated when the game resets its device.
#include "overlay_d3d9.h"

#include <d3d9.h>
#include <string.h>
#include <windows.h>

#include "config.h"
#include "hook.h"
#include "log.h"
#include "ui.h"

namespace apb {

extern Config g_cfg;

typedef IDirect3D9*(WINAPI* Direct3DCreate9Fn)(UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                              const RGNDATA*);

static Patch g_iatPatch, g_createDevPatch, g_presentPatch;
static Direct3DCreate9Fn o_Direct3DCreate9;
static CreateDeviceFn o_CreateDevice;
static PresentFn o_Present;

// ---------------------------------------------------------------------------
// Panel bitmap
// ---------------------------------------------------------------------------
struct PanelTex {
  IDirect3DTexture9* tex = nullptr;
  IDirect3DDevice9* owner = nullptr;
  int w = 0, h = 0;
  unsigned serial = 0;
  UINT screenH = 0;
};
static PanelTex g_panel;

static void ReleasePanel() {
  if (g_panel.tex) g_panel.tex->Release();
  g_panel = PanelTex();
}

static bool BuildPanel(IDirect3DDevice9* dev, const ui::View& v, UINT screenH) {
  int fontPx = (int)screenH / 34;
  if (fontPx < 14) fontPx = 14;
  const int pad = fontPx / 2;  // horizontal padding

  HDC dc = CreateCompatibleDC(nullptr);
  HFONT fNormal = CreateFontW(-fontPx, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              ANTIALIASED_QUALITY, 0, L"Segoe UI");
  HFONT fHeader = CreateFontW(-fontPx, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              ANTIALIASED_QUALITY, 0, L"Segoe UI");
  // Each line gets its own band of height rowH; the glyph box (internal leading
  // excluded) is centred vertically in that band.
  SelectObject(dc, fNormal);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  const int glyphH = tm.tmHeight - tm.tmInternalLeading;
  const int rowH = glyphH + fontPx * 3 / 4;
  const int textOffY = (rowH - glyphH) / 2 - tm.tmInternalLeading;
  int w = 0;
  for (int i = 0; i < v.lineCount; ++i) {
    SelectObject(dc, i == 0 ? fHeader : fNormal);
    SIZE sz;
    GetTextExtentPoint32W(dc, v.lines[i], lstrlenW(v.lines[i]), &sz);
    if (sz.cx > w) w = sz.cx;
  }
  w += 2 * pad;
  int h = v.lineCount * rowH;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (bmp) {
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)w * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int i = 0; i < v.lineCount; ++i) {
      SelectObject(dc, i == 0 ? fHeader : fNormal);
      TextOutW(dc, pad, i * rowH + textOffY, v.lines[i], lstrlenW(v.lines[i]));
    }
    GdiFlush();

    // Composite: per-row background + coloured text using the GDI coverage as alpha.
    uint32_t* px = static_cast<uint32_t*>(bits);
    for (int y = 0; y < h; ++y) {
      int line = y / rowH;
      if (line >= v.lineCount) line = v.lineCount - 1;
      bool header = line == 0, selected = line == v.selected;
      bool hint = v.selected >= 0 && line == v.lineCount - 1;
      int bgR = 18, bgG = 16, bgB = 12, bgA = 215;
      if (header) bgR = 52, bgG = 38, bgB = 14, bgA = 230;
      if (selected) bgR = 92, bgG = 66, bgB = 18, bgA = 240;
      int tR = 225, tG = 220, tB = 205;
      if (header) tR = 255, tG = 208, tB = 96;
      if (selected) tR = 255, tG = 255, tB = 255;
      if (hint) tR = 165, tG = 160, tB = 150;
      for (int x = 0; x < w; ++x) {
        uint32_t m = px[y * w + x];
        int cov = (int)((m >> 16) & 0xFF);
        int g = (int)((m >> 8) & 0xFF), b = (int)(m & 0xFF);
        if (g > cov) cov = g;
        if (b > cov) cov = b;
        int r = (bgR * (255 - cov) + tR * cov) / 255;
        int gg = (bgG * (255 - cov) + tG * cov) / 255;
        int bb = (bgB * (255 - cov) + tB * cov) / 255;
        int a = bgA + (255 - bgA) * cov / 255;
        bool border = x < 2 || y < 2 || x >= w - 2 || y >= h - 2;
        if (border) r = 200, gg = 150, bb = 50, a = 255;
        px[y * w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
      }
    }

    if (g_panel.tex && (g_panel.w != w || g_panel.h != h || g_panel.owner != dev)) ReleasePanel();
    if (!g_panel.tex)
      dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_panel.tex, nullptr);
    if (g_panel.tex) {
      D3DLOCKED_RECT lr;
      if (SUCCEEDED(g_panel.tex->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < h; ++y)
          memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, px + y * w, (size_t)w * 4);
        g_panel.tex->UnlockRect(0);
        g_panel.owner = dev;
        g_panel.w = w;
        g_panel.h = h;
        g_panel.serial = v.serial;
        g_panel.screenH = screenH;
        ok = true;
      }
    }
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
  }
  DeleteObject(fNormal);
  DeleteObject(fHeader);
  DeleteDC(dc);
  return ok;
}

struct Vtx {
  float x, y, z, rhw, u, v;
};

static void DrawPanel(IDirect3DDevice9* dev) {
  if (dev->TestCooperativeLevel() != D3D_OK) return;
  IDirect3DSurface9* bb = nullptr;
  if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
  D3DSURFACE_DESC desc;
  bb->GetDesc(&desc);

  ui::View v;
  ui::GetView(v);
  if (!g_panel.tex || g_panel.serial != v.serial || g_panel.screenH != desc.Height ||
      g_panel.owner != dev) {
    if (!BuildPanel(dev, v, desc.Height)) {
      bb->Release();
      return;
    }
  }

  IDirect3DStateBlock9* sb = nullptr;
  if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb)) || !sb) {
    bb->Release();
    return;
  }
  IDirect3DSurface9* oldRT = nullptr;
  dev->GetRenderTarget(0, &oldRT);
  dev->SetRenderTarget(0, bb);
  D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
  dev->SetViewport(&vp);

  dev->SetVertexShader(nullptr);
  dev->SetPixelShader(nullptr);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
  dev->SetRenderState(D3DRS_ZENABLE, FALSE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
  dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  dev->SetTexture(0, g_panel.tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
  dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
  dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
  dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
  dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

  float margin = (float)desc.Height / 30.0f;
  float top = (float)desc.Height * (float)g_cfg.panelTopPct / 100.0f;
  if (top + (float)g_panel.h > (float)desc.Height - margin) top = (float)desc.Height - margin - (float)g_panel.h;
  if (top < 0.0f) top = 0.0f;
  float x0 = (float)desc.Width - margin - (float)g_panel.w - 0.5f, y0 = (float)(int)top - 0.5f;
  float x1 = x0 + (float)g_panel.w, y1 = y0 + (float)g_panel.h;
  Vtx q[4] = {{x0, y0, 0, 1, 0, 0}, {x1, y0, 0, 1, 1, 0}, {x0, y1, 0, 1, 0, 1}, {x1, y1, 0, 1, 1, 1}};
  if (SUCCEEDED(dev->BeginScene())) {
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    dev->EndScene();
  }

  sb->Apply();
  sb->Release();
  if (oldRT) {
    dev->SetRenderTarget(0, oldRT);
    oldRT->Release();
  }
  bb->Release();
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Hk_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b,
                                            HWND w, const RGNDATA* r) {
  ui::Tick();
  if (ui::Visible()) DrawPanel(dev);
  return o_Present(dev, a, b, w, r);
}

static HRESULT STDMETHODCALLTYPE Hk_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type,
                                                 HWND wnd, DWORD flags,
                                                 D3DPRESENT_PARAMETERS* pp,
                                                 IDirect3DDevice9** out) {
  HRESULT hr = o_CreateDevice(d3d, adapter, type, wnd, flags, pp, out);
  if (SUCCEEDED(hr) && out && *out) {
    ReleasePanel();
    void** vt = *reinterpret_cast<void***>(*out);
    if (!g_presentPatch.active) {
      o_Present = reinterpret_cast<PresentFn>(vt[17]);
      if (!InstallPointerPatch(g_presentPatch, "IDirect3DDevice9::Present", &vt[17], vt[17],
                               reinterpret_cast<void*>(&Hk_Present)))
        Log("Overlay: could not hook Present - Mods panel disabled");
    }
  }
  return hr;
}

static IDirect3D9* WINAPI Hk_Direct3DCreate9(UINT sdk) {
  IDirect3D9* d3d = o_Direct3DCreate9(sdk);
  if (d3d && !g_createDevPatch.active) {
    void** vt = *reinterpret_cast<void***>(d3d);
    o_CreateDevice = reinterpret_cast<CreateDeviceFn>(vt[16]);
    InstallPointerPatch(g_createDevPatch, "IDirect3D9::CreateDevice", &vt[16], vt[16],
                        reinterpret_cast<void*>(&Hk_CreateDevice));
  }
  return d3d;
}

bool OverlayInstall() {
  HMODULE exe = GetModuleHandleW(nullptr);
  void** slot = FindIatSlot(exe, "d3d9.dll", "Direct3DCreate9", 0);
  HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
  void* real = d3d9 ? reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
  if (!slot || !real) {
    Log("Overlay: d3d9 import not found (slot=%p real=%p) - Mods panel disabled", slot, real);
    return false;
  }
  if (*slot != real) {
    Log("Overlay: Direct3DCreate9 import already redirected by something else (%p != %p) - "
        "Mods panel disabled", *slot, real);
    return false;
  }
  o_Direct3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(real);
  return InstallPointerPatch(g_iatPatch, "IAT d3d9!Direct3DCreate9", slot, real,
                             reinterpret_cast<void*>(&Hk_Direct3DCreate9));
}

void OverlayUninstall() {
  RemovePatch(g_presentPatch);
  RemovePatch(g_createDevPatch);
  RemovePatch(g_iatPatch);
}

}  // namespace apb
