#include "FF.hpp"
#include <stdexcept>

namespace FF
{
   FFMPEG::FFMPEG()
   {
      ref()++;

      if (ref() == 1)
      {
         avcodec_init();
         av_register_all();
      }
   }

   FFMPEG::~FFMPEG()
   {}

   Packet::Packet()
   {
      memset(&pkt, 0, sizeof(pkt));
   }

   AVPacket& Packet::get()
   {
      return pkt;
   }

   Packet::~Packet()
   {
      av_free_packet(&pkt);
   }

   MediaFile::MediaFile(const char *path) : vcodec(NULL), acodec(NULL), actx(NULL), vctx(NULL), fctx(NULL), vid_stream(-1), aud_stream(-1)
   {
      if (path == NULL)
         throw std::runtime_error("Got null-path\n");

      if (av_open_input_file(&fctx, path, NULL, 0, NULL) != 0)
         throw std::runtime_error("Failed to open file\n");

      if (av_find_stream_info(fctx) < 0)
         throw std::runtime_error("Failed to get stream information\n");

      resolve_codecs();
      set_media_info();

      // Debug
      dump_format(fctx, 0, path, false);
   }

   MediaFile::~MediaFile()
   {
      if (actx)
         avcodec_close(actx);
      if (vctx)
         avcodec_close(vctx);
      if (fctx)
         av_close_input_file(fctx);
   }

   void MediaFile::resolve_codecs()
   {

      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO)
         {
            vid_stream = i;
            break;
         }
      }

      for (unsigned i = 0; i < fctx->nb_streams; i++)
      {
         if (fctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
         {
            aud_stream = i;
            break;
         }
      }

      if (vid_stream >= 0)
      {
         vctx = fctx->streams[vid_stream]->codec;
         vcodec = avcodec_find_decoder(vctx->codec_id);
         if (vcodec)
            avcodec_open(vctx, vcodec);
      }

      if (aud_stream >= 0)
      {
         actx = fctx->streams[aud_stream]->codec;
         acodec = avcodec_find_decoder(actx->codec_id);
         if (acodec)
            avcodec_open(actx, acodec);
      }
   }

   void MediaFile::set_media_info()
   {
      if (acodec)
      {
         aud_info.channels = actx->channels;
         aud_info.rate = actx->sample_rate;
         aud_info.active = true;
         aud_info.ctx = actx;
      }
      else
         aud_info.active = false;

      if (vcodec)
      {
         vid_info.width = vctx->width;
         vid_info.height = vctx->height;
         vid_info.aspect_ratio = (float)vctx->width * vctx->sample_aspect_ratio.num / (vctx->height * vctx->sample_aspect_ratio.den);
         vid_info.active = true;
         vid_info.ctx = vctx;
      }
      else
         vid_info.active = false;
   }

   const MediaFile::audio_info& MediaFile::audio() const
   {
      return aud_info;
   }

   const MediaFile::video_info& MediaFile::video() const
   {
      return vid_info;
   }

   Packet::Type MediaFile::packet(Packet& pkt)
   {
      if (av_read_frame(fctx, &pkt.get()) < 0)
         return Packet::Type::Error;

      Packet::Type type = Packet::Type::None;

      int index = pkt.get().stream_index;
      if (index == aud_stream)
         type = Packet::Type::Audio;
      else if (index == vid_stream)
         type = Packet::Type::Video;

      return type;
   }
}