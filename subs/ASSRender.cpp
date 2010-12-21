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


#include "ASSRender.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <iostream>
#include <assert.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <algorithm>

using namespace AV::Sub;

namespace AV
{
   namespace Sub
   {
      namespace Internal
      {
         extern "C" 
         {
            static void ass_msg_cb(int level, const char *fmt, va_list args, void *data);
         }

         static void ass_msg_cb(int level, const char *fmt, va_list args, void *)
         {
            if (level < 6)
            {
               std::cerr << "libass debug:" << std::endl;
               vfprintf(stderr, fmt, args);
               std::cerr << std::endl;
            }
         }
      }
   }
}

Message ASSRenderer::create_message(ASS_Image *img)
{
   std::vector<uint32_t> data(img->w * img->h);

   std::fill(data.begin(), data.end(), img->color);

   int stride = img->stride;
   for (int y = 0; y < img->h; y++)
   {
      for (int x = 0; x < img->w; x++)
      {
         data[y * img->w + x] = (0xFFFFFF00U & data[y * img->w + x]) | img->bitmap[y * stride + x];
      }
   }

   return Message(img->dst_x, img->dst_y, img->w, img->h, &data[0]);
}

ASSRenderer::ASSRenderer(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& fonts, const std::vector<uint8_t>& ass_data, unsigned width, unsigned height)
{
   library = ass_library_init();
   ass_set_message_cb(library, Internal::ass_msg_cb, nullptr);

   // Here enters the prettycast! :D
   std::for_each(fonts.begin(), fonts.end(), 
         [this](const std::pair<std::string, std::vector<uint8_t>>& font)
         {
            ass_add_font(library, const_cast<char*>(font.first.c_str()), 
               reinterpret_cast<char*>(const_cast<uint8_t*>(&font.second[0])), font.second.size());
         });

   renderer = ass_renderer_init(library);
   // Hardcode for now.
   ass_set_frame_size(renderer, width, height);
   ass_set_extract_fonts(library, 1);
   ass_set_fonts(renderer, nullptr, nullptr, 1, nullptr, 1);
   ass_set_hinting(renderer, ASS_HINTING_LIGHT);


   track = ass_new_track(library);
   ass_process_codec_private(track, reinterpret_cast<char*>(const_cast<uint8_t*>(&ass_data[0])), ass_data.size());
}

ASSRenderer::~ASSRenderer()
{
   ass_free_track(track);
   ass_renderer_done(renderer);
   ass_library_done(library);
}

void ASSRenderer::flush()
{
   ass_flush_events(track);
}

// Grab decoded messages from ffmpeg here.
void ASSRenderer::push_msg(const std::string &msg, double video_pts)
{
   //std::cout << "Push_msg!" << std::endl;

   ass_process_data(track, const_cast<char*>(msg.c_str()), msg.size());
}

// Return a list of messages to overlay on frame at pts.
const ASSRenderer::ListType& ASSRenderer::msg_list(double pts)
{
   int change;
   ASS_Image *img = ass_render_frame(renderer, track, (long long)(pts * 1000), &change);
   
   if (change)
   {
      active_list.clear();
      while (img)
      {
         active_list.push_back(create_message(img));
         img = img->next;
      }
   }

   // Process some stuff and return something sensible.
   return active_list;
}

