/*
 *  SLIMPlayer - Simple and Lightweight Media Player
 *  Copyright (C) 2010 - Hans-Kristian Arntzen
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __FF_HPP
#define __FF_HPP

#include <stdint.h>

extern "C" {

// This stuff doesn't seem to be pulled in properly...

#ifndef __CONCAT
#define __CONCAT(value, suffix) value##suffix
#endif

#undef INT8_C
#undef UINT8_C
#undef INT16_C
#undef UINT16_C
#undef INT32_C
#undef UINT32_C
#undef INT64_C
#undef UINT64_C
#undef INTMAX_C
#undef UINTMAX_C
#define INT8_C(value) ((int8_t) value) 
#define UINT8_C(value) ((uint8_t) __CONCAT(value, U)) 
#define INT16_C(value) value 
#define UINT16_C(value) __CONCAT(value, U) 
#define INT32_C(value) __CONCAT(value, L) 
#define UINT32_C(value) __CONCAT(value, UL) 
#define INT64_C(value) __CONCAT(value, LL) 
#define UINT64_C(value) __CONCAT(value, ULL) 
#define INTMAX_C(value) __CONCAT(value, LL) 
#define UINTMAX_C(value) __CONCAT(value, ULL)

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

}

#include "General.hpp"
#include <vector>
#include <string>
#include <utility>

namespace FF
{
   void set_global_pts(uint64_t);

   enum class SeekTarget
   {
      Video,
      Audio,
      Default
   };

   class Packet
   {
      public:
         Packet();
         AVPacket& get();
         ~Packet();

         void operator=(const Packet&) = delete;
         Packet(const Packet&) = delete;

         Packet& operator=(Packet&&);
         Packet(Packet&&);

         enum class Type : unsigned
         {
            None = 0x0,
            Error = 0x1,
            Audio = 0x2,
            Video = 0x4,
            Subtitle = 0x8
         };

      private:
         AVPacket *pkt;
   };

   // A ref counted class that keeps track of FFmpeg deinit and init. :)
   class FFMPEG : public General::RefCounted<FFMPEG>
   {
      public:
         FFMPEG();
         ~FFMPEG();
   };

   class MediaFile : public FFMPEG, private General::SmartDefs<MediaFile>
   {
      public:
         DECL_SMART(MediaFile);
         MediaFile(const char *path);
         MediaFile(MediaFile&&);
         MediaFile& operator=(MediaFile&&);
         void operator=(const MediaFile&) = delete;
         MediaFile(const MediaFile&) = delete;
         ~MediaFile();

         struct audio_info
         {
            unsigned channels;
            unsigned rate;
            bool active;
            AVRational time_base;
            AVCodecContext *ctx;
         };

         struct video_info
         {
            unsigned width;
            unsigned height;
            float aspect_ratio;
            bool active;
            AVRational time_base;
            AVCodecContext *ctx;
         };

         struct subtitle_info
         {
            bool active;
            AVCodecContext *ctx;
            std::vector<std::pair<std::string, std::vector<char>>> fonts;
            std::vector<char> ass_data;
         };

         const audio_info& audio() const;
         const video_info& video() const;
         const subtitle_info& sub() const;

         Packet::Type packet(Packet&);
         void seek(double video_pts, double audio_pts, double relative, SeekTarget target = SeekTarget::Default);

      private:
         AVCodec *vcodec;
         AVCodec *acodec;
         AVCodec *scodec;
         AVCodecContext *actx;
         AVCodecContext *vctx;
         AVCodecContext *sctx;
         AVFormatContext *fctx;
         audio_info aud_info;
         video_info vid_info;
         subtitle_info sub_info;
         std::vector<int> attachments;
         int vid_stream;
         int aud_stream;
         int sub_stream;

         void resolve_codecs();
         void set_media_info();
   };
}

#endif
