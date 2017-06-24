#pragma once
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

#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <cedarx/vdecoder.h>
#include "windowing/egl/sunxi_display.h"
#include <linux/fb.h>
#include <set>

#include "DVDVideoCodec.h"
#include "DVDStreamInfo.h"
#include "utils/BitstreamConverter.h"
#include "guilib/Geometry.h"
#include "threads/SingleLock.h"

class CCedarXBuffer 
{
public:
  CCedarXBuffer() : pPicture(NULL), m_iRefs(0) {}

  // Shared pointer interface
  virtual void Lock();
  virtual long Release();

public:
  VideoPicture *pPicture;
  VideoDecoder *pVideoCodec;
  int           leftOffset;
  int           topOffset;
  
protected:
  long          m_iRefs;
  
private:
  // private because we are reference counted
  virtual ~CCedarXBuffer();
};

class CDVDVideoCodecSunxi : public CDVDVideoCodec
{
public:
  CDVDVideoCodecSunxi(CProcessInfo &processInfo);
  virtual ~CDVDVideoCodecSunxi();

  // Required overrides
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose(void);
  virtual int  Decode(uint8_t *pData, int iSize, double dts, double pts);
  virtual void Reset(void);
  virtual bool ClearPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual bool GetPicture(DVDVideoPicture *pDvdVideoPicture);
  virtual void SetDropState(bool bDrop) { m_dropState = bDrop; }
  virtual const char* GetName(void) { return (const char*)m_pFormatName; }
  virtual void SetSpeed(int iSpeed);
  
protected:
  CCedarXBuffer   *m_buffer;
  CDVDStreamInfo   m_hints;
  VideoDecoder    *m_videoCodec;
  VideoStreamInfo  m_streamInfo;
  const char      *m_pFormatName;
  bool             m_dropState;
  bool             m_vp9orhevccodec;

  CBitstreamConverter *m_bitstream;
};

class CSunxiContext
{
public:
  CSunxiContext();
  ~CSunxiContext();
  void Configure();
  void Close();
  
  void SetDeinterlacing(bool enabled);
  void DeinterlaceMode(bool bottomFirst);
  void Pause(bool enabled);
  void Render(CCedarXBuffer *buffer, CRect &srcRect, CRect &dstRect);
  CCedarXBuffer *Deinterlace(CCedarXBuffer *current, int nField);
  void SetVSync(bool enabled);
  
// ugly hack
  void AddDecoder(VideoDecoder *decoder);
  void RemoveDecoder(VideoDecoder *decoder);
  void FreePicture(VideoDecoder *decoder, VideoPicture *picture);
  
private:
  __disp_layer_info_t video_config;
  CCriticalSection  m_renderLock;
  bool              m_deinterlace;
  int               m_difd;
  CCedarXBuffer    *m_last;
  bool              m_paused;
  unsigned int      m_instances;
  bool              m_vsync;
  int               m_fbHandle;
  // ugly hack
  std::set<VideoDecoder*> m_decoders;
  CCriticalSection  m_decodersLock;
};

extern CSunxiContext g_SunxiContext;
