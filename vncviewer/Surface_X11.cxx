/* Copyright 2016 Pierre Ossman for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include <stdexcept>

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#include "Surface.h"

void Surface::clear(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  XRenderColor color;

  color.red = (unsigned)r * 65535 / 255 * a / 255;
  color.green = (unsigned)g * 65535 / 255 * a / 255;
  color.blue = (unsigned)b * 65535 / 255 * a / 255;
  color.alpha = (unsigned)a * 65535 / 255;

  XRenderFillRectangle(fl_display, PictOpSrc, picture, &color,
                       0, 0, width(), height());
}

// FLTK 1.4 coordinates are in scaled units, but we draw directly to
// the window, which is in physical pixels. So we need to scale
// everything when we hit the actual window.
static float window_scale()
{
#if FL_API_VERSION >= 10400
  Fl_Window* win;

  win = Fl_Window::current();
  if (win == nullptr)
    return 1.0;

  return Fl::screen_scale(win->screen_num());
#else
  return 1.0;
#endif
}

// Composites part of src to the current window, scaling as needed.
static void composite_to_window(int op, Picture src, Picture mask,
                                XRenderPictFormat* visFormat,
                                int src_x, int src_y, int dst_x, int dst_y,
                                int dst_w, int dst_h)
{
  Picture winPict;
  float scale;

  winPict = XRenderCreatePicture(fl_display, fl_window, visFormat, 0, nullptr);

  scale = window_scale();
  if (scale == 1.0) {
    XRenderComposite(fl_display, op, src, mask, winPict,
                     src_x, src_y, 0, 0, dst_x, dst_y, dst_w, dst_h);
  } else {
    XTransform transform = {{
      { XDoubleToFixed(1.0 / scale), 0, 0 },
      { 0, XDoubleToFixed(1.0 / scale), 0 },
      { 0, 0, XDoubleToFixed(1.0) },
    }};
    XTransform identity = {{
      { XDoubleToFixed(1.0), 0, 0 },
      { 0, XDoubleToFixed(1.0), 0 },
      { 0, 0, XDoubleToFixed(1.0) },
    }};
    XRenderPictureAttributes pa;
    int x1, y1, x2, y2;

    // Cover every physical pixel touched by the area
    x1 = floorf(dst_x * scale);
    y1 = floorf(dst_y * scale);
    x2 = ceilf((dst_x + dst_w) * scale);
    y2 = ceilf((dst_y + dst_h) * scale);

    // Integer factors can just duplicate pixels and stay sharp
    if (scale == floorf(scale))
      XRenderSetPictureFilter(fl_display, src, FilterNearest, nullptr, 0);
    else
      XRenderSetPictureFilter(fl_display, src, FilterBilinear, nullptr, 0);
    XRenderSetPictureTransform(fl_display, src, &transform);

    // Filtering samples beyond the edges, so clamp those to the edge
    // pixels rather than letting them fade to transparent black
    pa.repeat = RepeatPad;
    XRenderChangePicture(fl_display, src, CPRepeat, &pa);

    // The source coordinates get transformed, so they need to be given
    // in physical units as well. This is only exact for fractional
    // factors if src and dst coordinates are equal, which is the case
    // for DesktopWindow's offscreen buffer (the only user on X11).
    XRenderComposite(fl_display, op, src, mask, winPict,
                     x1 + lroundf((src_x - dst_x) * scale),
                     y1 + lroundf((src_y - dst_y) * scale),
                     0, 0, x1, y1, x2 - x1, y2 - y1);

    pa.repeat = RepeatNone;
    XRenderChangePicture(fl_display, src, CPRepeat, &pa);
    XRenderSetPictureTransform(fl_display, src, &identity);
    XRenderSetPictureFilter(fl_display, src, FilterNearest, nullptr, 0);
  }

  XRenderFreePicture(fl_display, winPict);
}

void Surface::draw(int src_x, int src_y, int dst_x, int dst_y,
                   int dst_w, int dst_h)
{
  composite_to_window(PictOpSrc, picture, None, visFormat,
                      src_x, src_y, dst_x, dst_y, dst_w, dst_h);
}

void Surface::draw(Surface* dst, int src_x, int src_y,
                   int dst_x, int dst_y, int dst_w, int dst_h)
{
  XRenderComposite(fl_display, PictOpSrc, picture, None, dst->picture,
                   src_x, src_y, 0, 0, dst_x, dst_y, dst_w, dst_h);
}

static Picture alpha_mask(int a)
{
  Pixmap pixmap;
  XRenderPictFormat* format;
  XRenderPictureAttributes rep;
  Picture pict;
  XRenderColor color;

  if (a == 255)
    return None;

  pixmap = XCreatePixmap(fl_display, XDefaultRootWindow(fl_display),
                         1, 1, 8);

  format = XRenderFindStandardFormat(fl_display, PictStandardA8);
  rep.repeat = RepeatNormal;
  pict = XRenderCreatePicture(fl_display, pixmap, format, CPRepeat, &rep);
  XFreePixmap(fl_display, pixmap);

  color.alpha = (unsigned)a * 65535 / 255;

  XRenderFillRectangle(fl_display, PictOpSrc, pict, &color,
                       0, 0, 1, 1);

  return pict;
}

void Surface::blend(int src_x, int src_y, int dst_x, int dst_y,
                    int dst_w, int dst_h, int a)
{
  Picture alpha;

  alpha = alpha_mask(a);
  composite_to_window(PictOpOver, picture, alpha, visFormat,
                      src_x, src_y, dst_x, dst_y, dst_w, dst_h);

  if (alpha != None)
    XRenderFreePicture(fl_display, alpha);
}

void Surface::blend(Surface* dst, int src_x, int src_y,
                    int dst_x, int dst_y, int dst_w, int dst_h, int a)
{
  Picture alpha;

  alpha = alpha_mask(a);
  XRenderComposite(fl_display, PictOpOver, picture, alpha, dst->picture,
                   src_x, src_y, 0, 0, dst_x, dst_y, dst_w, dst_h);
  if (alpha != None)
    XRenderFreePicture(fl_display, alpha);
}


void Surface::alloc()
{
  XRenderPictFormat templ;
  XRenderPictFormat* format;

  pixmap = XCreatePixmap(fl_display, XDefaultRootWindow(fl_display),
                         width(), height(), 32);

  // Our code assumes a BGRA byte order, regardless of what the endian
  // of the machine is or the native byte order of XImage, so make sure
  // we find such a format
  templ.type = PictTypeDirect;
  templ.depth = 32;
  if (XImageByteOrder(fl_display) == MSBFirst) {
    templ.direct.alpha = 0;
    templ.direct.red   = 8;
    templ.direct.green = 16;
    templ.direct.blue  = 24;
  } else {
    templ.direct.alpha = 24;
    templ.direct.red   = 16;
    templ.direct.green = 8;
    templ.direct.blue  = 0;
  }
  templ.direct.alphaMask = 0xff;
  templ.direct.redMask = 0xff;
  templ.direct.greenMask = 0xff;
  templ.direct.blueMask = 0xff;

  format = XRenderFindFormat(fl_display, PictFormatType | PictFormatDepth |
                             PictFormatRed | PictFormatRedMask |
                             PictFormatGreen | PictFormatGreenMask |
                             PictFormatBlue | PictFormatBlueMask |
                             PictFormatAlpha | PictFormatAlphaMask,
                             &templ, 0);

  if (!format)
    throw std::runtime_error("XRenderFindFormat");

  picture = XRenderCreatePicture(fl_display, pixmap, format, 0, nullptr);

  visFormat = XRenderFindVisualFormat(fl_display, fl_visual->visual);
}

void Surface::dealloc()
{
  XRenderFreePicture(fl_display, picture);
  XFreePixmap(fl_display, pixmap);
}

void Surface::update(const Fl_RGB_Image* image)
{
  XImage* img;
  GC gc;

  int x, y;
  const unsigned char* in;
  unsigned char* out;

  assert(image->w() == width());
  assert(image->h() == height());

  img = XCreateImage(fl_display, (Visual*)CopyFromParent, 32,
                     ZPixmap, 0, nullptr, width(), height(),
                     32, 0);
  if (!img)
    throw std::runtime_error("XCreateImage");

  img->data = (char*)malloc(img->bytes_per_line * img->height);
  if (!img->data)
    throw std::bad_alloc();

  // Convert data and pre-multiply alpha
  in = (const unsigned char*)image->data()[0];
  out = (unsigned char*)img->data;
  for (y = 0;y < img->height;y++) {
    for (x = 0;x < img->width;x++) {
      switch (image->d()) {
      case 1:
        *out++ = in[0];
        *out++ = in[0];
        *out++ = in[0];
        *out++ = 0xff;
        break;
      case 2:
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = in[1];
        break;
      case 3:
        *out++ = in[2];
        *out++ = in[1];
        *out++ = in[0];
        *out++ = 0xff;
        break;
      case 4:
        *out++ = (unsigned)in[2] * in[3] / 255;
        *out++ = (unsigned)in[1] * in[3] / 255;
        *out++ = (unsigned)in[0] * in[3] / 255;
        *out++ = in[3];
        break;
      }
      in += image->d();
    }
    if (image->ld() != 0)
      in += image->ld() - image->w() * image->d();
  }

  gc = XCreateGC(fl_display, pixmap, 0, nullptr);
  XPutImage(fl_display, pixmap, gc, img,
            0, 0, 0, 0, img->width, img->height);
  XFreeGC(fl_display, gc);

  XDestroyImage(img);
}

