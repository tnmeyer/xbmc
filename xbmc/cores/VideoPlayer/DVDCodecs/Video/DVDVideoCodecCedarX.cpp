/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <cedarx/memoryAdapter.h>

#include "DVDVideoCodecCedarX.h"
#include "DVDClock.h"
#include "utils/log.h"
#include "threads/Atomics.h"
#include "windowing/WindowingFactory.h"
#include "settings/Settings.h"
#include "settings/VideoSettings.h"

//FIXME: find better way
#include "windowing/egl/EGLNativeTypeSunxi.h"

#define _4CC(c1,c2,c3,c4) (((uint32_t)(c4)<<24)|((uint32_t)(c3)<<16)|((uint32_t)(c2)<<8)|(uint32_t)(c1))

// global instance
CSunxiContext g_SunxiContext;

CDVDVideoCodecSunxi::CDVDVideoCodecSunxi(CProcessInfo &processInfo) : CDVDVideoCodec(processInfo),
  m_videoCodec(NULL),
  m_pFormatName("cedarx-xxx"),
  m_dropState(false),
  m_vp9orhevccodec(false),
  m_bitstream(NULL)
{
}

CDVDVideoCodecSunxi::~CDVDVideoCodecSunxi()
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecSunxi destructor");
  Dispose();
}

bool CDVDVideoCodecSunxi::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  VConfig vConfig;
  bool ignoreExtraData = false;
  
  m_hints = hints;

  if (!CSettings::GetInstance().GetBool("videoplayer.usecedarx"))
     {
       CLog::Log(LOGERROR, "Use Software decoding. CedarX disabled.");
        return false;
     }

  memset(&m_streamInfo, 0, sizeof(m_streamInfo));
  memset(&vConfig, 0, sizeof(VConfig));
  vConfig.eOutputPixelFormat = PIXEL_FORMAT_NV21;

  switch (hints.codec)
  {
    case AV_CODEC_ID_MPEG1VIDEO:
      if ((CSettings::GetInstance().GetBool("videoplayer.useawmpeg2")) && (m_hints.width > 799))
      {
        m_pFormatName = "cedarx-mpeg1";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MPEG1;
        break;
      }
      else { return false; }
    case AV_CODEC_ID_MPEG2VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO_XVMC:
      if ((CSettings::GetInstance().GetBool("videoplayer.useawmpeg2")) && (m_hints.width > 799))
      {
        m_pFormatName = "cedarx-mpeg2";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MPEG2;
        break;
      }
      else { return false; }
    case AV_CODEC_ID_H263:
      if (CSettings::GetInstance().GetBool("videoplayer.useawmpeg4"))
      {
        m_pFormatName = "cedarx-h263";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H263;
        break;
      }
      else { return false; }
    case AV_CODEC_ID_MPEG4:
      if (CSettings::GetInstance().GetBool("videoplayer.useawmpeg4"))
      {
        switch(m_hints.codec_tag)
        {
        case _4CC('X','V','I','D'):
        case _4CC('x','v','i','d'):
        case _4CC('M','P','4','V'):
        case _4CC('m','p','4','v'):
        case _4CC('P','M','P','4'):
        case _4CC('F','M','P','4'):
          m_pFormatName = "cedarx-xvid";
          m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_XVID;
          break;
        default:
          CLog::Log(LOGERROR, "CedarX : Using XVID for MPEG4 codec with tag %d.", m_hints.codec_tag);
          CLog::Log(LOGERROR, "CedarX : Probably unsupported, expect trouble...");
          m_pFormatName = "cedarx-xvid";
          m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_XVID;
          break;
        }
        break;
      }
      else { return false; }
    case AV_CODEC_ID_H264:
      if (CSettings::GetInstance().GetBool("videoplayer.useawh264"))
      {
        m_pFormatName = "cedarx-h264";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
        // convert h264-avcC to h264-annex-b as h264-avcC
        // under streamers can have issues when seeking.
        if (hints.extradata)
        {
          m_bitstream = new CBitstreamConverter;
          if (!m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, true))
          {
            SAFE_DELETE(m_bitstream);
          } 
          else 
          {
              // make sure we do not leak the existing hints.extradata
              free(hints.extradata);
              hints.extrasize = m_bitstream->GetExtraSize();
              hints.extradata = malloc(hints.extrasize);
              memcpy(hints.extradata, m_bitstream->GetExtraData(), hints.extrasize);
          }
        }
        break;
      }
      else { return false; }
    case AV_CODEC_ID_MJPEG:
      m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_MJPEG;
      m_pFormatName = "cedarx-mjpeg";
      break;
    case AV_CODEC_ID_VP8:
      if (CSettings::GetInstance().GetBool("videoplayer.useawvp8"))
      {
        CLog::Log(LOGERROR, "CedarX : VP8 is broken...");
        m_pFormatName = "cedarx-vp8";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_VP8;
        break;
      }
      else { return false; }
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_WMV3:
      if (CSettings::GetInstance().GetBool("videoplayer.useawwmv3"))
      {
        CLog::Log(LOGERROR, "CedarX : VC1/WMV3 is broken...");
        m_pFormatName = "cedarx-wmv3";
        m_streamInfo.eCodecFormat = VIDEO_CODEC_FORMAT_WMV3;
        ignoreExtraData = true;
        break;
      }
      else { return false; }
    default:
      CLog::Log(LOGDEBUG, "CDVDVideoCodecSunxi::OpenDecoder - Codec format is not supported (%d)!", (int)hints.codec);
      return false;
  }
  
  m_streamInfo.nWidth = hints.width;
  m_streamInfo.nHeight = hints.height;

  // handle video rate
  if((hints.fpsscale > 0) && (hints.fpsrate > 0))
  {
    // check ffmpeg r_frame_rate 1st
    m_streamInfo.nFrameRate = hints.fpsrate * 1000 / hints.fpsscale; 
  }
  else if(hints.fpsscale > 0)
  {
    // then ffmpeg avg_frame_rate next
    m_streamInfo.nFrameRate = hints.fpsrate * 1000 / hints.fpsscale; 
  }
  else
  {
    m_streamInfo.nFrameRate = hints.fpsrate * 1000;
  }
  
  CLog::Log(LOGDEBUG, "CDVDVideoCodecSunxi::OpenDecoder - AVCodecID(%d) -> CedarXID(0x%x)",
            (int)hints.codec, m_streamInfo.eCodecFormat);
  
  if ((hints.extrasize > 0) && !ignoreExtraData)
  {
    m_streamInfo.pCodecSpecificData = (char*)hints.extradata;
    m_streamInfo.nCodecSpecificDataLen = hints.extrasize;
    CLog::Log(LOGDEBUG, "CDVDVideoCodecSunxi::OpenDecoder - ExtraSize[%d]", m_streamInfo.nCodecSpecificDataLen);
  }

  m_videoCodec = CreateVideoDecoder();
  if(m_videoCodec == NULL)
  {
      CLog::Log(LOGERROR,"CDVDVideoCodecSunxi::OpenDecoder - CreateVideoDecoder failed!\n");
      return false;
  }
  
  InitializeVideoDecoder(m_videoCodec, &m_streamInfo, &vConfig);
  g_SunxiContext.Configure();
  g_SunxiContext.AddDecoder(m_videoCodec);

  CLog::Log(LOGINFO, "CDVDVideoCodecSunxi: Opened AllwinnerTech CedarX Codec");
  
  m_processInfo.SetVideoDecoderName(m_pFormatName, true);
  m_processInfo.SetVideoDimensions(m_hints.width, m_hints.height);
  m_processInfo.SetVideoDeintMethod("hardware");

  return true;
}

void CDVDVideoCodecSunxi::Dispose(void)
{
  CLog::Log(LOGDEBUG,"CDVDVideoCodecSunxi::Dispose");
  
  g_SunxiContext.Close();
  
  if (m_videoCodec) 
  {
    g_SunxiContext.RemoveDecoder(m_videoCodec);
    DestroyVideoDecoder(m_videoCodec);
    m_videoCodec = NULL;
  }
  
  if (m_bitstream)
  {
    m_bitstream->Close();
    SAFE_DELETE(m_bitstream);
  }
}

int CDVDVideoCodecSunxi::Decode(uint8_t *pData, int iSize, double dts, double pts)
{
  VideoPicture        *picture;
  VideoStreamDataInfo  dataInfo;
  char*                buf0;
  char*                buf1;
  int64_t              nPts = -1;
  int                  buf0Size;
  int                  buf1Size;
  int                  ret;
  int                  status;
  
  if (m_hints.ptsinvalid)
    pts = DVD_NOPTS_VALUE; 
  if (pts != DVD_NOPTS_VALUE)
    nPts = pts;
  else if (dts!= DVD_NOPTS_VALUE)
    nPts = dts;

  if((pData != NULL) && (iSize > 0))
  {
    if (m_bitstream)
    {
      if (!m_bitstream->Convert(pData, iSize))
        return VC_ERROR;

      pData = m_bitstream->GetConvertBuffer();
      iSize = m_bitstream->GetConvertSize();
    }

    ret = RequestVideoStreamBuffer(m_videoCodec, iSize, &buf0,
                                   &buf0Size, &buf1, &buf1Size, 0);
    if(ret < 0)
    {
        CLog::Log(LOGERROR,"CDVDVideoCodecSunxi::Decode - RequestVideoStreamBuffer failed");
        return VC_ERROR;
    }
    
    if(iSize > buf0Size)
    {
      memcpy(buf0, pData,            buf0Size);
      memcpy(buf1, pData + buf0Size, buf1Size);
    }
    else
    {
      memcpy(buf0, pData, iSize);
    }
    
    memset(&dataInfo, 0, sizeof(dataInfo));    
    dataInfo.pData = buf0;   
    dataInfo.nLength = iSize;
    dataInfo.bIsFirstPart = 1;
    dataInfo.bIsLastPart = 1;
    dataInfo.nPts = nPts;
    dataInfo.nPcr = 0;

    if (nPts != -1) {
        dataInfo.bValid = 1;
    } else {
        dataInfo.bValid = 0;
    }
    
    do {
        int rep_count = 0;
        ret = SubmitVideoStreamData(m_videoCodec, &dataInfo, 0);
        if (ret != 0)
        {
            rep_count++;
            if (rep_count > 5) {
                    CLog::Log(LOGERROR, "SubmitVideoStreamData() error!");
                    return VC_ERROR;
            }
            usleep(5);
        }
        else {
                break;
             }
    } while(1);

  }
  
  ret = DecodeVideoStream(m_videoCodec, 0, 0, 0, 0);
  switch(ret)
  {
    case VDECODE_RESULT_FRAME_DECODED:
    case VDECODE_RESULT_KEYFRAME_DECODED:
    case VDECODE_RESULT_OK:
    case VDECODE_RESULT_NO_FRAME_BUFFER: //FIXME: what to do for this one?
    case VDECODE_RESULT_NO_BITSTREAM:
    case VDECODE_RESULT_CONTINUE:
      status = VC_BUFFER;
      break;
    case VDECODE_RESULT_RESOLUTION_CHANGE:
      status = VC_REOPEN;
      break;
    case VDECODE_RESULT_UNSUPPORTED:
    default:
      status = VC_ERROR;
      break;
  }
  
//  CLog::Log(LOGDEBUG, "CDVDVideoCodecSunxi::Decode - returned value: %d", ret);
  
  picture = RequestPicture(m_videoCodec, 0);
  if(picture != NULL)
  { 
    m_buffer = new CCedarXBuffer;
    
    if(m_vp9orhevccodec)
    {
      m_buffer->leftOffset = picture->nLeftOffset;
      m_buffer->topOffset = picture->nTopOffset;
    }
    else
    {
      m_buffer->leftOffset = 0;
      m_buffer->topOffset = 0;
    }
    
    m_buffer->pPicture = picture;
    m_buffer->pVideoCodec = m_videoCodec;
    m_buffer->Lock();
    
    status |= VC_PICTURE;
  }
  
  return status;
}

void CDVDVideoCodecSunxi::Reset(void)
{
  if(m_videoCodec != NULL)
  {
    ResetVideoDecoder(m_videoCodec);
  }
}

bool CDVDVideoCodecSunxi::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if(pDvdVideoPicture != NULL)
  {
    SAFE_RELEASE(pDvdVideoPicture->Disp2Buffer);
  }

  return true;
}

bool CDVDVideoCodecSunxi::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (pDvdVideoPicture != NULL)
  {
    int realWidth  = m_buffer->pPicture->nRightOffset - m_buffer->leftOffset;
    int realHeight = m_buffer->pPicture->nBottomOffset - m_buffer->topOffset;
    
    pDvdVideoPicture->pts = (m_buffer->pPicture->nPts > 0) ? 
      (double)m_buffer->pPicture->nPts : DVD_NOPTS_VALUE;
    pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
    
    pDvdVideoPicture->format = RENDER_FMT_DISP2;
    pDvdVideoPicture->iWidth  = realWidth;
    pDvdVideoPicture->iHeight = realHeight;
    pDvdVideoPicture->iDisplayWidth  = pDvdVideoPicture->iWidth;
    pDvdVideoPicture->iDisplayHeight = pDvdVideoPicture->iHeight;

    if (m_hints.aspect > 1.0 && !m_hints.forced_aspect)
    {
      pDvdVideoPicture->iDisplayWidth  = ((int)lrint(pDvdVideoPicture->iHeight * m_hints.aspect)) & ~3;
      if (pDvdVideoPicture->iDisplayWidth > pDvdVideoPicture->iWidth)
      {
        pDvdVideoPicture->iDisplayWidth  = pDvdVideoPicture->iWidth;
        pDvdVideoPicture->iDisplayHeight = ((int)lrint(pDvdVideoPicture->iWidth / m_hints.aspect)) & ~3;
      }
    }

    pDvdVideoPicture->Disp2Buffer = m_buffer;
    pDvdVideoPicture->color_range  = 0;
    pDvdVideoPicture->color_matrix = 4;
    
    pDvdVideoPicture->iFlags = DVP_FLAG_ALLOCATED;
    if (!m_buffer->pPicture->bIsProgressive)
      pDvdVideoPicture->iFlags |= DVP_FLAG_INTERLACED;
    if (m_buffer->pPicture->bTopFieldFirst)
      pDvdVideoPicture->iFlags |= DVP_FLAG_TOP_FIELD_FIRST;
    if (m_buffer->pPicture->bRepeatTopField)
      pDvdVideoPicture->iFlags |= DVP_FLAG_REPEAT_TOP_FIELD;
    if (m_dropState)
      pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;
    
    return true;
  }
  
  return false;
}

void CDVDVideoCodecSunxi::SetSpeed(int iSpeed)
{
  g_SunxiContext.Pause(iSpeed == DVD_PLAYSPEED_PAUSE);
}

void CCedarXBuffer::Lock()
{
  AtomicIncrement(&m_iRefs);
}

long CCedarXBuffer::Release()
{
  if (AtomicDecrement(&m_iRefs) == 0)
  {
    delete this;
  }
  return m_iRefs;
}

CCedarXBuffer::~CCedarXBuffer()
{
  g_SunxiContext.FreePicture(pVideoCodec, pPicture);
  pPicture = NULL;
}

CSunxiContext::CSunxiContext() : 
  m_deinterlace(false),
  m_last(NULL),
  m_paused(false),
  m_instances(0),
  m_vsync(false),
  m_fbHandle(-1)
{
}

CSunxiContext::~CSunxiContext()
{
}

void CSunxiContext::Configure()
{
  CSingleLock lk(m_renderLock);
  if(m_instances == 0)
  {
    unsigned long args[4];


    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = (unsigned long)(&video_config);
    args[3] = 0;

    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);

    video_config.mode = DISP_LAYER_WORK_MODE_NORMAL;
    video_config.alpha_en = 1;
    video_config.alpha_val = 255;

    ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args);
    
    m_fbHandle  = open("/dev/fb0", O_RDWR);
    if (m_fbHandle == -1)
    {
        CLog::Log(LOGERROR, "Can't open framebuffer!");
    }
  }

  m_instances++;
}

void CSunxiContext::Close()
{
  unsigned long args[4];

  CSingleLock lk(m_renderLock);

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
  }

  memset(&g_srcRect, 0, sizeof(g_srcRect));
  memset(&g_dstRect, 0, sizeof(g_dstRect));

  if (m_instances == 0)
    return;
  
  if (m_instances == 1)
  {
    unsigned long args[4];
    SAFE_RELEASE(m_last);

    if (m_fbHandle != -1)
    {
      close(m_fbHandle);
    }
  }
  
  m_instances--;
}

void CSunxiContext::SetDeinterlacing(bool enabled)
{
  m_deinterlace = enabled;
}

void CSunxiContext::Pause(bool enabled)
{
  m_paused = enabled;
}

void CSunxiContext::SetVSync(bool enabled)
{
  m_vsync = enabled && (m_fbHandle != -1);
}

void CSunxiContext::Render(CCedarXBuffer *buffer, CRect &srcRect, CRect &dstRect)
{
  unsigned long       args[4];
  __disp_layer_info_t layera;
  __disp_video_fb_t   frmbuf;
  __disp_colorkey_t   colorkey;

  CSingleLock lk(m_renderLock);
  memset(&frmbuf, 0, sizeof(__disp_video_fb_t));

  // sanity check
  if(buffer == NULL)
    return;
  
  if(m_instances == 0)
    return;

  CCedarXBuffer *renderbuf;
  VideoPicture *pPicture;
  renderbuf = buffer;
  pPicture = renderbuf->pPicture;
    
  frmbuf.addr[0] = (unsigned long )MemAdapterGetPhysicAddressCpu(pPicture->pData0);
  frmbuf.addr[1] = (unsigned long )MemAdapterGetPhysicAddressCpu(pPicture->pData1);

  if ((g_srcRect != srcRect) || (g_dstRect != dstRect))
  {
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = (unsigned long) (&layera);
    args[3] = 0;
    ioctl(g_hdisp, DISP_CMD_LAYER_GET_PARA, args);
    //set video layer attribute
    layera.mode          = DISP_LAYER_WORK_MODE_SCALER;
    layera.b_from_screen = 0; //what is this? if enabled all is black
    layera.pipe          = 1;
    //use alpha blend
    layera.alpha_en      = 1;
    layera.alpha_val     = 0xff;
    layera.ck_enable     = 0;
    layera.b_trd_out     = 0;
    layera.out_trd_mode  = (__disp_3d_out_mode_t)0;
    //frame buffer pst and size information
    if (pPicture->nHeight < 720)
    {
      layera.fb.cs_mode = DISP_BT601;
    }
    else
    {
      layera.fb.cs_mode = DISP_BT709;
    }
    layera.fb.mode        = DISP_MOD_MB_UV_COMBINED;
    layera.fb.format      = DISP_FORMAT_YUV420;
    layera.fb.br_swap     = 0;
    layera.fb.seq         = DISP_SEQ_UVUV;
    layera.fb.addr[0]     = frmbuf.addr[0];
    layera.fb.addr[1]     = frmbuf.addr[1];
    layera.fb.b_trd_src   = 0;
    layera.fb.trd_mode    = (__disp_3d_src_mode_t)0;
    layera.fb.size.width  = pPicture->nWidth;
    layera.fb.size.height = pPicture->nHeight;
    //source window information
    layera.src_win.x      = lrint(srcRect.x1);
    layera.src_win.y      = lrint(srcRect.y1);
    layera.src_win.width  = lrint(srcRect.x2-srcRect.x1);
    layera.src_win.height = lrint(srcRect.y2-srcRect.y1);
    //screen window information
    layera.scn_win.x      = lrint(dstRect.x1);
    layera.scn_win.y      = lrint(dstRect.y1);
    layera.scn_win.width  = lrint(dstRect.x2-dstRect.x1);
    layera.scn_win.height = lrint(dstRect.y2-dstRect.y1);

    CLog::Log(LOGDEBUG, "Sunxi-DISP: srcRect=(%lf,%lf)-(%lf,%lf)\n", srcRect.x1, srcRect.y1, srcRect.x2, srcRect.y2);
    CLog::Log(LOGDEBUG, "Sunxi-DISP: dstRect=(%lf,%lf)-(%lf,%lf)\n", dstRect.x1, dstRect.y1, dstRect.x2, dstRect.y2);

    if (    (layera.scn_win.x < 0)
         || (layera.scn_win.y < 0)
         || (layera.scn_win.width  > g_width)
         || (layera.scn_win.height > g_height)    )
    {
      double xzoom, yzoom;

      //TODO: this calculation is against the display fullscreen dimensions,
      //but should be against the fullscreen area of xbmc

      xzoom = (dstRect.x2 - dstRect.x1) / (srcRect.x2 - srcRect.x1);
      yzoom = (dstRect.y2 - dstRect.y1) / (srcRect.y2 - srcRect.y1);

      if (layera.scn_win.x < 0)
      {
        layera.src_win.x -= layera.scn_win.x / xzoom;
        layera.scn_win.x = 0;
      }
      if (layera.scn_win.width > g_width)
      {
        layera.src_win.width -= (layera.scn_win.width - g_width) / xzoom;
        layera.scn_win.width = g_width;
      }

      if (layera.scn_win.y < 0)
      {
        layera.src_win.y -= layera.scn_win.y / yzoom;
        layera.scn_win.y = 0;
      }
      if (layera.scn_win.height > g_height)
      {
        layera.src_win.height -= (layera.scn_win.height - g_height) / yzoom;
        layera.scn_win.height = g_height;
      }
    }

    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = (unsigned long)&layera;
    args[3] = 0;
    if(ioctl(g_hdisp, DISP_CMD_LAYER_SET_PARA, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_SET_PARA failed.\n");

    //open layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_OPEN, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_OPEN failed.\n");

    //put behind system layer
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_BOTTOM, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_BOTTOM failed.\n");

    //turn off colorkey (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_CK_OFF, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_CK_OFF failed.\n");

    //turn off global alpha (system layer)
    args[0] = g_screenid;
    args[1] = g_syslayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_ALPHA_OFF, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_ALPHA_OFF failed.\n");

    //enable vpp
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_VPP_ON, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_VPP_ON failed.\n");

    //enable enhance
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_LAYER_ENHANCE_ON, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_LAYER_ENHANCE_ON failed.\n");

    //start video
    args[0] = g_screenid;
    args[1] = g_hlayer;
    args[2] = 0;
    args[3] = 0;
    if (ioctl(g_hdisp, DISP_CMD_VIDEO_START, args))
      CLog::Log(LOGERROR, "Sunxi-DISP: DISP_CMD_VIDEO_START failed.\n");

    g_srcRect = srcRect;
    g_dstRect = dstRect;
  }

  args[0] = g_screenid;
  args[1] = g_hlayer;
  args[2] = (unsigned long)&frmbuf;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_VIDEO_SET_FB, args);

  args[0] = g_screenid;
  args[1] = g_hlayer;
  args[2] = 0;
  args[3] = 0;
  ioctl(g_hdisp, DISP_CMD_VIDEO_GET_FRAME_ID, args);

  renderbuf->Lock();
  SAFE_RELEASE(m_last);
  m_last = renderbuf;

}

void CSunxiContext::AddDecoder(VideoDecoder *decoder)
{
  CSingleLock lk(m_decodersLock);
  
  if(decoder != NULL)
  {
    m_decoders.insert(decoder);
  }
}

void CSunxiContext::RemoveDecoder(VideoDecoder *decoder)
{
  CSingleLock lk(m_decodersLock);
  
  m_decoders.erase(decoder);
}

void CSunxiContext::FreePicture(VideoDecoder *decoder, VideoPicture *picture)
{
  CSingleLock lk(m_decodersLock);
  
  if((m_decoders.find(decoder) != m_decoders.end()) &&
     (picture != NULL))
  { 
    ReturnPicture(decoder, picture);
  }
}
