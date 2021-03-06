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


#include "FF.hpp"
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace FF
{

   namespace Internal
   {
      static uint64_t g_video_pkt_pts = AV_NOPTS_VALUE;

      // Custom functions for packets. Make sure that we get the correct PTS values from the packets when needed.
      extern "C" {
         static int get_buffer(AVCodecContext *c, AVFrame *pic);
         static void release_buffer(AVCodecContext *c, AVFrame *pic);
      }

      static int get_buffer(AVCodecContext *c, AVFrame *pic)
      {
         int ret = avcodec_default_get_buffer(c, pic);
         uint64_t *pts = (uint64_t*)av_malloc(sizeof(uint64_t));
         *pts = g_video_pkt_pts;
         pic->opaque = pts;
         return ret;
      }

      static void release_buffer(AVCodecContext *c, AVFrame *pic)
      {
         if (pic) av_freep(&pic->opaque);
         avcodec_default_release_buffer(c, pic);
      }
   }

   void set_global_pts(uint64_t pts)
   {
      Internal::g_video_pkt_pts = pts;
   }

   // FFmpeg global init. This is called on every instance of MediaFile.
   FFMPEG::FFMPEG()
   {
      ref()++;

      if (ref() == 1)
         av_register_all();
   }

   FFMPEG::~FFMPEG()
   {}

   Packet::Packet() : pkt(nullptr)
   {
      pkt = (AVPacket*)av_mallocz(sizeof(AVPacket));
   }

   // Move semantic. We can never allow two copies of the same Packet out there in the wild.
   Packet& Packet::operator=(Packet&& in_pkt)
   {
      if (pkt && pkt->data)
         av_free_packet(pkt);
      if (pkt)
         av_freep(&pkt);

      pkt = in_pkt.pkt;
      in_pkt.pkt = nullptr;
      return *this;
   }

   Packet::Packet(Packet&& in_pkt) : pkt(nullptr)
   {
      *this = std::move(in_pkt);
   }

   AVPacket& Packet::get()
   {
      if (pkt == nullptr)
         throw std::runtime_error("Trying to dereference nullptr\n");

      return *pkt;
   }

   Packet::~Packet()
   {
      if (pkt)
      {
         av_free_packet(pkt);
         av_freep(&pkt);
      }
   }

   MediaFile::MediaFile(const char *path) : vcodec(nullptr), acodec(nullptr), actx(nullptr), vctx(nullptr), sctx(nullptr), fctx(nullptr), vid_stream(-1), aud_stream(-1), sub_stream(-1)
   {
      if (path == nullptr)
         throw std::runtime_error("Got null-path\n");

      if (avformat_open_input(&fctx, path, nullptr, nullptr) != 0)
         throw std::runtime_error("Failed to open file\n");

      if (avformat_find_stream_info(fctx, nullptr) < 0)
         throw std::runtime_error("Failed to get stream information\n");

      resolve_codecs();
      set_media_info();

      // Debug
      //dump_format(fctx, 0, path, false);
   }

   MediaFile::~MediaFile()
   {
      if (actx)
         avcodec_close(actx);
      if (vctx)
         avcodec_close(vctx);
      if (sctx)
         avcodec_close(sctx);
      if (fctx)
         av_close_input_file(fctx);
   }

   void MediaFile::resolve_codecs()
   {
      // Find first video stream.
      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
         {
            vid_stream = i;
            break;
         }
      }
      // Find first audio stream.
      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
         {
            aud_stream = i;
            break;
         }
      }
      // Find first subtitle stream.
      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
         {
            sub_stream = i;
            break;
         }
      }
      // Map out all attachments, typically fonts for ASS. :)
      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_ATTACHMENT)
         {
            attachments.push_back(i);
         }
      }

      // Find decoders for the various formats.
      if (vid_stream >= 0)
      {
         vctx = fctx->streams[vid_stream]->codec;
         vcodec = avcodec_find_decoder(vctx->codec_id);
         if (vcodec)
            avcodec_open2(vctx, vcodec, nullptr);
      }

      if (aud_stream >= 0)
      {
         actx = fctx->streams[aud_stream]->codec;
         acodec = avcodec_find_decoder(actx->codec_id);
         if (acodec)
            avcodec_open2(actx, acodec, nullptr);
      }

      // Extract ASS metadata header from stream.
      if (sub_stream >= 0)
      {
         sctx = fctx->streams[sub_stream]->codec;
         if (sctx->codec_id == CODEC_ID_SSA)
         {
            scodec = avcodec_find_decoder(sctx->codec_id);
            if (scodec)
            {
               avcodec_open2(sctx, scodec, nullptr);

               if (sctx->extradata != nullptr)
                  sub_info.ass_data.insert(sub_info.ass_data.end(), sctx->extradata, sctx->extradata + sctx->extradata_size);
            }
         }
         else
         {
            sctx = nullptr;
            sub_stream = -1;
            attachments.clear();
         }
      }

      // Extract TTF fonts for use in ASS.
      for (auto id : attachments)
      {
         AVCodecContext *ctx = fctx->streams[id]->codec;
         if (ctx->codec_id == CODEC_ID_TTF)
            sub_info.fonts.push_back(std::make_pair("", std::vector<char>(ctx->extradata, ctx->extradata + ctx->extradata_size)));
      }
   }

   void MediaFile::set_media_info()
   {
      if (acodec)
      {
         aud_info.channels = actx->channels;
         aud_info.rate = actx->sample_rate;
         aud_info.active = true;
         aud_info.time_base = fctx->streams[aud_stream]->time_base;
         aud_info.ctx = actx;
      }
      else
         aud_info.active = false;

      if (vcodec)
      {
         vid_info.width = vctx->width;
         vid_info.height = vctx->height;

         // TODO: Fix me.
         if (vctx->sample_aspect_ratio.den != 0 && vctx->sample_aspect_ratio.num != 0)
         {
            vid_info.aspect_ratio = (float)vctx->width * av_q2d(vctx->sample_aspect_ratio) / vctx->height ;
         }
         else
            vid_info.aspect_ratio = (float)vctx->width / vctx->height;

         vid_info.active = true;
         vid_info.time_base = fctx->streams[vid_stream]->time_base;
         vid_info.ctx = vctx;

         vctx->get_buffer = Internal::get_buffer;
         vctx->release_buffer = Internal::release_buffer;
      }
      else
         vid_info.active = false;

      if (scodec)
      {
         sub_info.active = true;
         sub_info.ctx = sctx;
      }
      else
         sub_info.active = false;
   }

   void MediaFile::seek(double video_pts, double audio_pts, double rel, SeekTarget target)
   {
      int flags = (rel < 0.0) ? AVSEEK_FLAG_BACKWARD : 0;

      double seek_to = 0.0;
      int stream = -1;
      if (vid_stream >= 0 && target != SeekTarget::Audio)
      {
         stream = vid_stream;
         seek_to = (video_pts + rel) / av_q2d(fctx->streams[vid_stream]->time_base);
      }
      else if (aud_stream >= 0)
      {
         stream = aud_stream;
         seek_to = (audio_pts + rel) / av_q2d(fctx->streams[aud_stream]->time_base);
         if (seek_to < 0.0)
            seek_to = 0.0;
      }

      if (av_seek_frame(fctx, stream, seek_to, flags) < 0)
         throw std::runtime_error("av_seek_frame() failed");

      // We need to flush our buffers after seeking.
      if (acodec)
         avcodec_flush_buffers(actx);
      if (vcodec)
         avcodec_flush_buffers(vctx);
      if (scodec)
         avcodec_flush_buffers(sctx);
   }

   const MediaFile::audio_info& MediaFile::audio() const
   {
      return aud_info;
   }

   const MediaFile::video_info& MediaFile::video() const
   {
      return vid_info;
   }

   const MediaFile::subtitle_info& MediaFile::sub() const
   {
      return sub_info;
   }

   Packet::Type MediaFile::packet(Packet& pkt)
   {
      // Reads next packet from the file.
      if (av_read_frame(fctx, &pkt.get()) < 0)
         return Packet::Type::Error;

      // Makes sure that packet has it's own allocated space. So we can put it in a queue safely.
      av_dup_packet(&pkt.get());

      Packet::Type type = Packet::Type::None;

      int index = pkt.get().stream_index;
      if (index == aud_stream)
         type = Packet::Type::Audio;
      else if (index == vid_stream)
         type = Packet::Type::Video;
      else if (index == sub_stream)
         type = Packet::Type::Subtitle;

      return type;
   }
}
