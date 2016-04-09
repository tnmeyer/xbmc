/*
 *      Copyright (C) 2011-2013 Team XBMC
 *      http://xbmc.org
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

#include "EGLNativeTypeSunxi.h"
#include "guilib/gui3d.h"
#include "utils/StringUtils.h"
#include "utils/SysfsUtils.h"
#include "utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <EGL/egl.h>
#include "sunxi_display.h"

#define SCREENID 0

//fixme
#ifndef _FBDEV_WINDOW_H_
#define _FBDEV_WINDOW_H_
typedef struct fbdev_window
{
  unsigned short width;
  unsigned short height;
} fbdev_window;
#endif


//global vars for sunxi DISP driver layer management
int g_hdisp = -1;
int g_screenid = 0;
int g_syslayer = 0x64;
int g_hlayer = 0;
unsigned g_width;
unsigned g_height;
double g_refreshRate = 60;
CRect  g_srcRect;
CRect  g_dstRect;

static const unsigned long supportedModes[] = 
{
  DISP_TV_MOD_480I,
  DISP_TV_MOD_576I,
  DISP_TV_MOD_480P,
  DISP_TV_MOD_576P,
  DISP_TV_MOD_720P_50HZ,
  DISP_TV_MOD_720P_60HZ,
  DISP_TV_MOD_1080I_50HZ,
  DISP_TV_MOD_1080I_60HZ,
  DISP_TV_MOD_1080P_24HZ,
  DISP_TV_MOD_1080P_50HZ,
  DISP_TV_MOD_1080P_60HZ,
};

CEGLNativeTypeSunxi::CEGLNativeTypeSunxi()
{
  m_nativeWindow = NULL;
}

CEGLNativeTypeSunxi::~CEGLNativeTypeSunxi()
{
}

bool CEGLNativeTypeSunxi::CheckCompatibility()
{
  std::string name;
  std::string modalias = "/sys/class/graphics/fb0/device/modalias";

  SysfsUtils::GetString(modalias, name);
  StringUtils::Trim(name);
  if (name == "platform:disp")
    return true;
  return false;
}

int CEGLNativeTypeSunxi::BlueScreenFix()
{
  int                 hlayer;
  __disp_layer_info_t layera;
  unsigned long       args[4];

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

  layera.mode      = DISP_LAYER_WORK_MODE_SCALER;
  layera.fb.mode   = DISP_MOD_MB_UV_COMBINED;
  layera.fb.format = DISP_FORMAT_YUV420;
  layera.fb.seq    = DISP_SEQ_UVUV;
  ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);

  args[0] = g_screenid;
  args[1] = hlayer;
  args[2] = 0;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
  return true;
}

void CEGLNativeTypeSunxi::Initialize()
{
  unsigned long       args[4];
  __disp_layer_info_t layera;
  unsigned int        i;

  g_hdisp = open("/dev/disp", O_RDWR);
  if (g_hdisp == -1)
  {
    CLog::Log(LOGERROR, "Sunxi-DISP: open /dev/disp failed. (%d)", errno);
  }

  // tell /dev/disp the API version we are using
  args[0] = SUNXI_DISP_VERSION;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  i = ioctl(g_hdisp, DISP_CMD_VERSION, args);
  CLog::Log(LOGNOTICE, "Sunxi-DISP: display API version is: %d.%d\n",
            SUNXI_DISP_VERSION_MAJOR_GET(i),
            SUNXI_DISP_VERSION_MINOR_GET(i));

  args[0] = g_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  g_width  = ioctl(g_hdisp, DISP_CMD_SCN_GET_WIDTH , args);
  g_height = ioctl(g_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);

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
  default:
    CLog::Log(LOGERROR, "Sunxi-DISP: display mode %d is unknown. Assume refreh rate 60Hz\n", i);
    g_refreshRate = 60.0;
    break;
  }

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

  for (i = 0x65; i <= 0x67; i++)
  {
    //release possibly lost allocated layers
    args[0] = g_screenid;
    args[1] = i;
    args[2] = 0;
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_RELEASE, args);
  }
  if (!BlueScreenFix())
    CLog::Log(LOGERROR, "Sunxi-DISP: BlueScreenFix failed.\n");

  args[0] = g_screenid;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  args[2] = 0;
  args[3] = 0;
  g_hlayer = ioctl(g_hdisp, DISP_CMD_LAYER_REQUEST, args);
  if (g_hlayer == NULL)
  {
    g_hlayer = 101;
    CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_REQUEST failed.\n");
  }

  CLog::Log(LOGNOTICE,"Sunxi-DISP: Screen ID: %d\n",g_screenid);
  CLog::Log(LOGNOTICE,"Sunxi-DISP: Layer Handle: %d\n",g_hlayer);

  memset(&g_srcRect, 0, sizeof(g_srcRect));
  memset(&g_dstRect, 0, sizeof(g_dstRect));

}

void CEGLNativeTypeSunxi::Destroy()
{
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
  }

  if(g_hdisp != -1)
  {
    close(g_hdisp);
    g_hdisp = -1;
  }
}

bool CEGLNativeTypeSunxi::CreateNativeDisplay()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;
  return true;
}

bool CEGLNativeTypeSunxi::CreateNativeWindow()
{
  unsigned long args[4];
  fbdev_window *nativeWindow = new fbdev_window;
  
  if (!nativeWindow)
    return false;
  
  if (g_hdisp == -1)
    return false;

  args[0] = g_screenid;
  args[1] = 0;
  args[2] = 0;
  args[3] = 0;
  nativeWindow->width = ioctl(g_hdisp, DISP_CMD_SCN_GET_WIDTH, args);
  nativeWindow->height = ioctl(g_hdisp, DISP_CMD_SCN_GET_HEIGHT, args);
  m_nativeWindow = nativeWindow;

  return true;
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
  delete (fbdev_window*)m_nativeWindow, m_nativeWindow = NULL;

  return true;
}

bool CEGLNativeTypeSunxi::GetNativeResolution(RESOLUTION_INFO *res) const
{
  if (m_nativeWindow)
  {
   res->iWidth = ((fbdev_window *)m_nativeWindow)->width;
   res->iHeight= ((fbdev_window *)m_nativeWindow)->height;
  }

  res->fRefreshRate = g_refreshRate;
  res->dwFlags= D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->iScreenWidth  = res->iWidth;
  res->iScreenHeight = res->iHeight;
  res->strMode = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
  res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  CLog::Log(LOGNOTICE,"Current resolution: %s\n",res->strMode.c_str());
  return true;
}

bool CEGLNativeTypeSunxi::SetNativeResolution(const RESOLUTION_INFO &res)
{
  if (m_nativeWindow)
  {
    ((fbdev_window *)m_nativeWindow)->width = res.iScreenWidth;
    ((fbdev_window *)m_nativeWindow)->height = res.iScreenHeight;
  }

  switch((int)(0.5 + res.fRefreshRate))
  {
    case 60:
      switch(res.iScreenHeight)
      {
        case 480:
          SetDisplayResolution(DISP_TV_MOD_480I);
          break;
        default:
        case 720:
          SetDisplayResolution(DISP_TV_MOD_720P_60HZ);
          break;
        case 1080:
          if (res.dwFlags & D3DPRESENTFLAG_INTERLACED)
            SetDisplayResolution(DISP_TV_MOD_1080I_60HZ);
          else
            SetDisplayResolution(DISP_TV_MOD_1080P_60HZ);
          break;
      }
      break;
    default:
    case 50:
      switch(res.iScreenHeight)
      {
        case 576:
          SetDisplayResolution(DISP_TV_MOD_576I);
          break;
        case 720:
          SetDisplayResolution(DISP_TV_MOD_720P_50HZ);
          break;
        default:
        case 1080:
          if (res.dwFlags & D3DPRESENTFLAG_INTERLACED)
            SetDisplayResolution(DISP_TV_MOD_1080I_50HZ);
          else
            SetDisplayResolution(DISP_TV_MOD_1080P_50HZ);
          break;
      }
      break;
    case 30:
      switch(res.iScreenHeight)
      {
        case 480:
        default:
          SetDisplayResolution(DISP_TV_MOD_480P);
          break;
      }
      break;
    case 25:
      switch(res.iScreenHeight)
      {
        default:
        case 576:
          SetDisplayResolution(DISP_TV_MOD_576P);
          break;
      }
      break;
    case 24:
      switch(res.iScreenHeight)
      {
        default:
        case 1080:
          SetDisplayResolution(DISP_TV_MOD_1080P_24HZ);
          break;
      }
      break;
  }
  
  return true;
}

bool CEGLNativeTypeSunxi::ProbeResolutions(std::vector<RESOLUTION_INFO> &resolutions)
{
  unsigned long args[4];
  
  args[0] = g_screenid;
  args[2] = 0;
  args[3] = 0;

  for(unsigned int i = 0; i < sizeof(supportedModes) / sizeof(supportedModes[0]); i++)
  {
    args[1] = supportedModes[i];
    if(ioctl(g_hdisp, DISP_CMD_HDMI_SUPPORT_MODE, args))
    {
      RESOLUTION_INFO res;

      ModeToResolution(supportedModes[i], &res);
      resolutions.push_back(res);
    }
  }

  return resolutions.size() > 0;
}

bool CEGLNativeTypeSunxi::GetPreferredResolution(RESOLUTION_INFO *res) const
{
  // check display/mode, it gets defaulted at boot
  if (!GetNativeResolution(res))
  {
    // put to 720p if we get nothing
    ModeToResolution(DISP_TV_MOD_720P_50HZ, res);
  }

  return true;
}

bool CEGLNativeTypeSunxi::ShowWindow(bool show)
{
  std::string blank_framebuffer = "/sys/class/graphics/fb0/blank";
  SysfsUtils::SetInt(blank_framebuffer.c_str(), show ? 0 : 1);
  return true;
}

bool CEGLNativeTypeSunxi::SetDisplayResolution(unsigned int mode)
{
  unsigned long args[4];
  __disp_output_type_t para;
  
  args[0] = g_screenid;
  args[1] = (unsigned long)(&para);
  args[2] = 0;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_GET_OUTPUT_TYPE, args);
  
  RESOLUTION_INFO res;
  ModeToResolution(mode, &res);
  SetFramebufferResolution(res.iScreenWidth, res.iScreenHeight);

  return true;
}

void CEGLNativeTypeSunxi::SetFramebufferResolution(int width, int height) const
{
  int fd0;
  std::string framebuffer = "/dev/fb0";

  if ((fd0 = open(framebuffer.c_str(), O_RDWR)) >= 0)
  {
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd0, FBIOGET_VSCREENINFO, &vinfo) == 0)
    {
      vinfo.xres = width;
      vinfo.yres = height;
      vinfo.xres_virtual = width;
      vinfo.yres_virtual = height * 2;
      vinfo.bits_per_pixel = 32;
      vinfo.activate = FB_ACTIVATE_ALL;
      ioctl(fd0, FBIOPUT_VSCREENINFO, &vinfo);
    }
    close(fd0);
  }
}

bool CEGLNativeTypeSunxi::ModeToResolution(unsigned int mode, RESOLUTION_INFO *res) const
{
  
  if (!res)
    return false;

  res->iWidth = 0;
  res->iHeight= 0;
  
  switch(mode)
  {
    case DISP_TV_MOD_480I:
      res->iWidth = 720;
      res->iHeight= 480;
      res->iScreenWidth = 720;
      res->iScreenHeight= 480;
      res->fRefreshRate = 60;
      res->dwFlags = D3DPRESENTFLAG_INTERLACED;
      break;
    case DISP_TV_MOD_576I:
      res->iWidth = 720;
      res->iHeight= 576;
      res->iScreenWidth = 720;
      res->iScreenHeight= 576;
      res->fRefreshRate = 50;
      res->dwFlags = D3DPRESENTFLAG_INTERLACED;
      break;
    case DISP_TV_MOD_480P:
      res->iWidth = 720;
      res->iHeight= 480;
      res->iScreenWidth = 720;
      res->iScreenHeight= 480;
      res->fRefreshRate = 30;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_576P:
      res->iWidth = 720;
      res->iHeight= 576;
      res->iScreenWidth = 720;
      res->iScreenHeight= 576;
      res->fRefreshRate = 25;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_720P_50HZ:
      res->iWidth = 1280;
      res->iHeight= 720;
      res->iScreenWidth = 1280;
      res->iScreenHeight= 720;
      res->fRefreshRate = 50;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_720P_60HZ:
      res->iWidth = 1280;
      res->iHeight= 720;
      res->iScreenWidth = 1280;
      res->iScreenHeight= 720;
      res->fRefreshRate = 60;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_1080I_50HZ:
      res->iWidth = 1920;
      res->iHeight= 1080;
      res->iScreenWidth = 1920;
      res->iScreenHeight= 1080;
      res->fRefreshRate = 50;
      res->dwFlags = D3DPRESENTFLAG_INTERLACED;
      break;
    case DISP_TV_MOD_1080I_60HZ:
      res->iWidth = 1920;
      res->iHeight= 1080;
      res->iScreenWidth = 1920;
      res->iScreenHeight= 1080;
      res->fRefreshRate = 60;
      res->dwFlags = D3DPRESENTFLAG_INTERLACED;
      break;
    case DISP_TV_MOD_1080P_24HZ:
      res->iWidth = 1920;
      res->iHeight= 1080;
      res->iScreenWidth = 1920;
      res->iScreenHeight= 1080;
      res->fRefreshRate = 24;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_1080P_50HZ:
      res->iWidth = 1920;
      res->iHeight= 1080;
      res->iScreenWidth = 1920;
      res->iScreenHeight= 1080;
      res->fRefreshRate = 50;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    case DISP_TV_MOD_1080P_60HZ:
      res->iWidth = 1920;
      res->iHeight= 1080;
      res->iScreenWidth = 1920;
      res->iScreenHeight= 1080;
      res->fRefreshRate = 60;
      res->dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
      break;
    default:
      return false;
  }
  
  res->iScreen       = 0;
  res->bFullScreen   = true;
  res->iSubtitles    = (int)(0.965 * res->iHeight);
  res->fPixelRatio   = 1.0f;
  res->strMode       = StringUtils::Format("%dx%d @ %.2f%s - Full Screen", res->iScreenWidth, res->iScreenHeight, res->fRefreshRate,
    res->dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "");
  
  return true;
}
