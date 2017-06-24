/*
 *      Copyright (C) 2007-2015 Team Kodi
 *      http://kodi.tv
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "RendererSunxi.h"

#if defined(HAS_CEDARX)
#include "cores/IPlayer.h"
#include "windowing/egl/EGLWrapper.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "settings/MediaSettings.h"
#include "windowing/WindowingFactory.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"

#define RENDER_FLAG_FIELDS (RENDER_FLAG_FIELD0 | RENDER_FLAG_FIELD1)

CRendererSunxi::CRendererSunxi()
{
  m_bufHistory.clear();
}

CRendererSunxi::~CRendererSunxi()
{
  UnInit();
  std::for_each(m_bufHistory.begin(), m_bufHistory.end(), Release);
}

bool CRendererSunxi::RenderCapture(CRenderCapture* capture)
{
  capture->BeginRender();
  capture->EndRender();
  return true;
}

void CRendererSunxi::AddVideoPictureHW(DVDVideoPicture &picture, int index)
{
  YUVBUFFER &buf = m_buffers[index];

  buf.hwDec = picture.Disp2Buffer;

  if (picture.Disp2Buffer)
    picture.Disp2Buffer->Lock();
}

void CRendererSunxi::ReleaseBuffer(int idx)
{
  CCedarXBuffer *buffer =  static_cast<CCedarXBuffer*>(m_buffers[idx].hwDec);
  SAFE_RELEASE(buffer);
  m_buffers[idx].hwDec = NULL;
}

int CRendererSunxi::GetImageHook(YV12Image *image, int source, bool readonly)
{
  return source;
}

bool CRendererSunxi::IsGuiLayer()
{
  return false;
}

bool CRendererSunxi::Supports(ERENDERFEATURE feature)
{
  if (feature == RENDERFEATURE_PIXEL_RATIO ||
      feature == RENDERFEATURE_ZOOM)
    return true;

  return false;
}

bool CRendererSunxi::Supports(ESCALINGMETHOD method)
{
  return method == VS_SCALINGMETHOD_AUTO;
}

bool CRendererSunxi::WantsDoublePass()
{
    return false;
}

CRenderInfo CRendererSunxi::GetRenderInfo()
{
  CRenderInfo info;
  info.formats = m_formats;
  info.max_buffer_size = NUM_BUFFERS;
  // Let the codec control the buffer size
  info.optimal_buffer_size = 2;
  return info;
}

bool CRendererSunxi::LoadShadersHook()
{
  CLog::Log(LOGNOTICE, "GL: Using Sunxi DISP2 render method");
  m_textureTarget = GL_TEXTURE_2D;
  m_renderMethod = RENDER_DISP2;
  return true;
}

void CRendererSunxi::RenderDisp2Texture(int index, int field)
{
}

bool CRendererSunxi::RenderHook(int index)
{
  RenderDisp2Texture(index, m_currentField);
  VerifyGLState();
  return true;
}

bool CRendererSunxi::RenderUpdateVideoHook(bool clear, DWORD flags, DWORD alpha)
{
  static DWORD flagsPrev;
  CCedarXBuffer *buffer = static_cast<CCedarXBuffer*>(m_buffers[m_iYV12RenderBuffer].hwDec);
  if (buffer)
  {
    if ((!m_bufHistory.empty() && m_bufHistory.back() != buffer) || (m_bufHistory.empty()))
    {
      buffer->Lock();
      m_bufHistory.push_back(buffer);
    }
    else if (!m_bufHistory.empty() && m_bufHistory.back() == buffer && flagsPrev == flags)
    {
//      g_IMX.WaitVsync();
      return true;
    }

    flagsPrev = flags;

    int size = flags & RENDER_FLAG_FIELDMASK ? 2 : 1;
    while (m_bufHistory.size() > size)
    {
      m_bufHistory.front()->Release();
      m_bufHistory.pop_front();
    }
    // this hack is needed to get the 2D mode of a 3D movie going
    RENDER_STEREO_MODE stereo_mode = g_graphicsContext.GetStereoMode();
    if (stereo_mode)
        g_graphicsContext.SetStereoView(RENDER_STEREO_VIEW_LEFT);

    ManageRenderArea();

    if (stereo_mode)
      g_graphicsContext.SetStereoView(RENDER_STEREO_VIEW_OFF);

    CRect dstRect(m_destRect);
    CRect srcRect(m_sourceRect);
    switch (stereo_mode)
    {
        case RENDER_STEREO_MODE_SPLIT_HORIZONTAL:
          dstRect.y2 *= 2.0;
          srcRect.y2 *= 2.0;
        break;

        case RENDER_STEREO_MODE_SPLIT_VERTICAL:
          dstRect.x2 *= 2.0;
          srcRect.x2 *= 2.0;
        break;

        default:
        break;
    }

    if (flags & RENDER_FLAG_FIELDMASK)
    {
      g_SunxiContext.SetDeinterlacing(true);
    }
    // Progressive
    else
      g_SunxiContext.SetDeinterlacing(false);

    CCedarXBuffer *buffer_p = m_bufHistory.front();
    g_SunxiContext.Render(buffer, srcRect, dstRect);

   }
  return true;
}

bool CRendererSunxi::CreateTexture(int index)
{
  return true;
}

void CRendererSunxi::DeleteTexture(int index)
{
  ReleaseBuffer(index);
}

bool CRendererSunxi::UploadTexture(int index)
{
  return true;// nothing todo for Sunxi
}
#endif

