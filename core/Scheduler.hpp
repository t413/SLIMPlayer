#ifndef __SCHEDULER_HPP
#define __SCHEDULER_HPP

#include "AV.hpp"

namespace AV
{
   class Scheduler : public General::Shared<Scheduler>
   {
      public:
         Scheduler(FF::MediaFile::Ptr in_file);
         void operator=(const Scheduler&) = delete;
         Scheduler(const Scheduler&) = delete;

         ~Scheduler();

         void add_event_handler(EventHandler::APtr& ptr);

         bool active() const;
         void run();

      private:
         FF::MediaFile::Ptr file;
         bool has_video;
         bool has_audio;
         bool is_active;
         double video_pts;
         double audio_pts;
         double audio_pts_ts;
         double video_pts_ts;
         size_t audio_written;
         volatile bool is_paused;
         std::mutex avlock;
         std::mutex audio_lock;

         // If we never get proper PTS values for audio, we have to "hack" while seeking.
         bool audio_pts_hack;

         std::list<EventHandler::APtr> event_handlers;
         EventHandler::Event next_event();

         volatile bool video_thread_active;
         volatile bool audio_thread_active;
         PacketQueue vid_pkt_queue;
         PacketQueue aud_pkt_queue;
         PacketQueue sub_pkt_queue;
         AV::Sub::Renderer::APtr sub_renderer;

         std::thread video_thread;
         std::thread audio_thread;
         Audio::Stream<int16_t>::APtr audio;

         void perform_seek(double delta);

         void process_subtitle(AV::Video::Display::APtr&&);
         void process_video(AVPacket&, AV::Video::Display::APtr&&, AVFrame*);
         void process_audio(AVPacket&, AV::Audio::Stream<int16_t>::APtr&&);
         void pause_toggle();

         void video_thread_fn();
         void audio_thread_fn();

         double frame_time() const;
         static void sync_sleep(float time);
         static double get_time();
   };
}


#endif
