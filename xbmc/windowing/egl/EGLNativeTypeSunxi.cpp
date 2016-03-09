/*
 *      Copyright (C) 2011-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#include "system.h"
#include <EGL/egl.h>
#include "EGL/eglplatform.h"
#include <sys/ioctl.h>
#include "EGLNativeTypeSunxi.h"
#include "utils/log.h"
#include "guilib/gui3d.h"
#include "utils/StringUtils.h"
#include "guilib/GraphicContext.h"
#include "drv_display_sun4i.h"

#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
struct mali_native_window {
        unsigned short width;
        unsigned short height;
};

static struct mali_native_window g_fbwin;
static double       g_refreshRate;
static int             g_hfb = -1;
static int             g_hdisp = -1;
static int             g_screenid = 0;
static int             g_syslayer = 0x64;
static int             g_hlayer = 0;
static int             g_width;
static int             g_height;
static CRect           g_srcRect;
static CRect           g_dstRect;
static int             g_lastnr;
static int             g_decnr;
static int             g_wridx;
static int             g_rdidx;
static pthread_mutex_t g_dispq_mutex;
#endif

static bool BlueScreenFix()
{
  int hlayer;
  __disp_layer_info_t layera;
  unsigned long args[4];

  args[0] = g_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  hlayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (hlayer <= 0)
  {
    CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_REQUEST failed.\n");
    return false;
  }

  args[0] = g_screenid;
  args[1] = hlayer;
  args[2] = (unsigned long) &layera;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);

  layera.mode = DISP_LAYER_WORK_MODE_SCALER;
  layera.fb.mode = DISP_MOD_MB_UV_COMBINED;
  layera.fb.format = DISP_FORMAT_YUV420;
  layera.fb.seq = DISP_SEQ_UVUV;
  ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  args[0] = g_screenid;
  args[1] = hlayer;
  args[2] = 0;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);

  return true;
}

CEGLNativeTypeSunxi::CEGLNativeTypeSunxi()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  int width, height;
  unsigned long       args[4];
  __disp_layer_info_t layera;
  unsigned int        i;

  pthread_mutex_init(&g_dispq_mutex, NULL);

  g_hfb = open("/dev/fb0", O_RDWR);

  g_hdisp = open("/dev/disp", O_RDWR);
  if (g_hdisp == -1)
  {
    CLog::Log(LOGERROR, "Sunxi-DISP: open /dev/disp failed. (%d)", errno);
  }

  args[0] = g_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  width  = g_width  = ioctl(g_hdisp, DISP_CMD_SCN_GET_WIDTH , args);
  height = g_height = ioctl(g_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);

  i = ioctl(g_hdisp, DISP_CMD_HDMI_GET_MODE, args);

  switch(i)
  {
  case DISP_TV_MOD_720P_50HZ:
  case DISP_TV_MOD_1080I_50HZ:
  case DISP_TV_MOD_1080P_50HZ:
    g_refreshRate = 50.0;
    break;
  case DISP_TV_MOD_720P_60HZ:
  case DISP_TV_MOD_1080I_60HZ:
  case DISP_TV_MOD_1080P_60HZ:
    g_refreshRate = 60.0;
    break;
  case DISP_TV_MOD_1080P_24HZ:
    g_refreshRate = 24.0;
    break;
  default:
    CLog::Log(LOGERROR, "Sunxi-DISP: display mode %d is unknown. Assume refreh rate 60Hz\n", i);
    g_refreshRate = 60.0;
    break;
  }

  if (g_height > 720)
  {
    //set workmode scaler (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    layera.mode = DISP_LAYER_WORK_MODE_SCALER;
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);
  }
  else
  {
    //set workmode normal (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    //source window information
    layera.src_win.x      = 0;
    layera.src_win.y      = 0;
    layera.src_win.width  = g_width;
    layera.src_win.height = g_height;
    //screen window information
    layera.scn_win.x      = 0;
    layera.scn_win.y      = 0;
    layera.scn_win.width  = g_width;
    layera.scn_win.height = g_height;
    layera.mode = DISP_LAYER_WORK_MODE_NORMAL;
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  }

  for (i = 0x65; i <= 0x67; i++)
  {
    //release possibly lost allocated layers
    args[0] = g_screenid;
    args[1] = i;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
  }

  // Hack: avoid blue picture background
  if (!BlueScreenFix())
    CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_REQUEST failed.\n");

  args[0] = g_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  g_hlayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (g_hlayer <= 0)
  {
    g_hlayer = 0;
    CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_REQUEST failed.\n");
  }

  memset(&g_srcRect, 0, sizeof(g_srcRect));
  memset(&g_dstRect, 0, sizeof(g_dstRect));

  g_lastnr = -1;
  g_decnr  = 0;
  g_rdidx  = 0;
  g_wridx  = 0;

#if !defined(HAVE_LIBVDPAU)
  for (i = 0; i < DISPQS; i++)
    g_dispq[i].pict.id = -1;
#endif
  g_fbwin.width  = width;
  g_fbwin.height = height;
#endif
}

CEGLNativeTypeSunxi::~CEGLNativeTypeSunxi()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID) 
  unsigned long args[4];

  if (g_hlayer)
  {
    //stop video
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_VIDEO_STOP, args);

    //close layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_CLOSE, args);

    //release layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
    g_hlayer = 0;
  }
  if (g_hdisp != -1)
  {
    close(g_hdisp);
    g_hdisp = -1;
  }
  if (g_hfb != -1)
  {
    close(g_hfb);
    g_hfb = -1;
  }
#endif
}

bool CEGLNativeTypeSunxi::CheckCompatibility()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  return true;
#endif
  return false;
}

void CEGLNativeTypeSunxi::Initialize()
{
  return;
}
void CEGLNativeTypeSunxi::Destroy()
{
  return;
}

bool CEGLNativeTypeSunxi::CreateNativeDisplay()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;
  return true;
}

bool CEGLNativeTypeSunxi::CreateNativeWindow()
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  m_nativeWindow = &g_fbwin;
  return true;
#else
  return false;
#endif
}  

bool CEGLNativeTypeSunxi::GetNativeDisplay(XBNativeDisplayType **nativeDisplay) const
{
  if (!nativeDisplay)
    return false;
  *nativeDisplay = (XBNativeDisplayType*) &m_nativeDisplay;
  return true;
}

bool CEGLNativeTypeSunxi::GetNativeWindow(XBNativeWindowType **nativeWindow) const
{
  if (!nativeWindow)
    return false;
  *nativeWindow = (XBNativeWindowType*) &m_nativeWindow;
  return true;
}

bool CEGLNativeTypeSunxi::DestroyNativeDisplay()
{
  return true;
}

bool CEGLNativeTypeSunxi::DestroyNativeWindow()
{
  return true;
}

bool CEGLNativeTypeSunxi::GetNativeResolution(RESOLUTION_INFO *res) const
{
#if defined(ALLWINNERA10) && !defined(TARGET_ANDROID)
  res->iWidth = g_fbwin.width;
  res->iHeight= g_fbwin.height;

  res->fRefreshRate = g_refreshRate;
  res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->iScreenWidth  = res->iWidth;
  res->iScreenHeight = res->iHeight;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
  res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  CLog::Log(LOGNOTICE,"Current resolution: %s\n",res->strMode.c_str());
  return true;
#else
  return false;
#endif
}

bool CEGLNativeTypeSunxi::SetNativeResolution(const RESOLUTION_INFO &res)
{
  return false;
}

bool CEGLNativeTypeSunxi::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  RESOLUTION_INFO res;
  bool ret = false;
  ret = GetNativeResolution(&res);
  if (ret && res.iWidth > 1 && res.iHeight > 1)
  {
    resolutions.push_back(res);
    return true;
  }
  return false;
}

bool CEGLNativeTypeSunxi::GetPreferredResolution(RESOLUTION_INFO *res) const
{
  return false;
}

bool CEGLNativeTypeSunxi::ShowWindow(bool show)
{
  return false;
}
