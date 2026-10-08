// Prueft die Teilaktualisierung des Display-Renderers gegen eine vollstaendige Neuberechnung
// und misst die Rechenzeit (Windows-GDI durch Ersatzfunktionen ersetzt).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using HDC = void*; struct RECT { long left, top, right, bottom; };
struct BITMAPINFOHEADER { unsigned biSize; long biWidth, biHeight; unsigned short biPlanes, biBitCount; unsigned biCompression; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; };
enum { BI_RGB = 0, DIB_RGB_COLORS = 0, HALFTONE = 4, SRCCOPY = 0 };
static std::vector<uint32_t> g_out;
static int SetDIBitsToDevice(HDC, int, int, int w, int h, int, int, int, int, const void* p, const BITMAPINFO*, int) { g_out.assign((const uint32_t*)p, (const uint32_t*)p + size_t(w) * h); return h; }
static int SetStretchBltMode(HDC, int) { return 0; } static int SetBrushOrgEx(HDC, int, int, void*) { return 0; }
static int StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void*, const BITMAPINFO*, int, int) { return 0; }
unsigned char lcd_font[240][10];
#include "lcd_render.inc"
int main() {
  for (int i = 0; i < 240; i++) for (int j = 0; j < 10; j++) lcd_font[i][j] = (uint8_t)((i * 7 + j * 13) & 31);
  std::mt19937 rng(1);
  for (auto wh : {std::pair{553, 200}, std::pair{313, 113}}) {
    RECT r{0, 0, wh.first, wh.second}; void* inc = nullptr; long mism = 0; const int frames = 600;
    lcdview::View v; v.on = true; for (int i = 0; i < 80; i++) v.dd[i] = 32 + rng() % 90; for (int i = 0; i < 64; i++) v.cg[i] = rng() & 31;
    double t_inc = 0, t_full = 0; int static_frames = 0;
    for (int f = 0; f < frames; f++) {
      // typisches Spielgeschehen: Pegelbalken aendern sich fast jedes Bild, Text gelegentlich
      if (f % 3) for (int c = 0; c < 16; c++) { int hgt = rng() % 17; for (int rr = 0; rr < 16; rr++) { uint16_t bit = 1u << (15 - c); if (15 - rr < hgt) v.matrix[rr] |= bit; else v.matrix[rr] &= ~bit; } }
      else static_frames++;
      if (f % 25 == 0) v.dd[3 + rng() % 16] = 32 + rng() % 90;
      if (f % 40 == 0) v.cg[rng() % 64] ^= 1u << (rng() % 5);
      if (f % 90 == 0) v.dd[58] ^= 1;
      auto t0 = std::chrono::steady_clock::now(); lcdview::DrawDots(nullptr, r, v, inc); auto t1 = std::chrono::steady_clock::now();
      std::vector<uint32_t> a = g_out;
      void* full = nullptr; lcdview::DrawDots(nullptr, r, v, full); auto t2 = std::chrono::steady_clock::now(); lcdview::FreeCache(full);
      if (a != g_out) mism++;
      t_inc += std::chrono::duration<double>(t1 - t0).count(); t_full += std::chrono::duration<double>(t2 - t1).count();
    }
    printf("Display %dx%d: %d Bilder, %ld Abweichungen | bisher %.2f ms/Bild, neu %.2f ms/Bild -> bei 30 Bildern/s %.1f %% statt %.1f %% eines Kerns\n",
           wh.first, wh.second, frames, mism, 1000 * t_full / frames, 1000 * t_inc / frames, 3 * 1000 * t_inc / frames, 3 * 1000 * t_full / frames);
    // ganz ohne Aenderung (stehendes Display)
    auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < 300; i++) lcdview::DrawDots(nullptr, r, v, inc); double ts = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 300;
    printf("  stehendes Display: %.3f ms/Bild -> %.2f %% eines Kerns\n", 1000 * ts, 3 * 1000 * ts);
    lcdview::FreeCache(inc);
  }
}
