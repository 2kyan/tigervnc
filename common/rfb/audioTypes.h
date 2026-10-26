/* Copyright 2026 mobile_tigervncviewer contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

/* TigerVNC-native audio extension: a generic RFB audio channel, distinct
 * from (and not dependent on) QEMU's own vendor-specific VNC audio
 * extension (see qemuTypes.h). Negotiated the same way every other
 * optional pseudo-encoding is: a server that doesn't recognise
 * pseudoEncodingTigerAudio (encodings.h) simply never receives these
 * messages, so an unmodified server is completely unaffected.
 *
 * The `data` op's payload (see msgTypeAudioStream in CMsgReader.cxx/
 * SMsgWriter.cxx) is one Opus-encoded frame -- not raw PCM. Chosen over
 * shipping raw S16LE (was v1's original design, ~1.5 Mbps at the fixed
 * rate/channels below) because that's an unreasonable bitrate for a
 * mobile client; Opus at audioOpusBitrate gets the same audio down to
 * roughly 1/20th the bandwidth with little perceptible quality loss for
 * general system audio (voice + music), and needs no FEC/PLC tuning
 * since RFB rides on a reliable, ordered TCP stream (no packet loss to
 * conceal). There is still no format-negotiation message -- the op byte
 * still reserves room for one -- since there's only one wire format to
 * agree on for now, same reasoning as before.
 *
 * audioSampleRate/audioChannels/audioBitsPerSample describe the
 * *decoded* PCM format on both ends: what AudioCapturePulse captures
 * (and encodes) on the server, and what VncClientImpl decodes back to
 * before handing PCM to the audio_data callback -- so the C ABI and
 * every platform consumer (Android/iOS) are completely unaware Opus is
 * even involved; only the two ends of the wire (encoder, decoder) know.
 */

#ifndef __RFB_AUDIOTYPES_H__
#define __RFB_AUDIOTYPES_H__

namespace rfb {
  // msgTypeAudioStream (server -> client) op byte
  const int audioOpBegin = 0;
  const int audioOpData = 1;
  const int audioOpEnd = 2;

  // msgTypeAudioControl (client -> server) op byte
  const int audioControlDisable = 0;
  const int audioControlEnable = 1;

  // Decoded PCM format on both ends (see this header's top comment) --
  // named here so client and server code (e.g. unix/x0vncserver's
  // AudioCapturePulse, core/src/VncClientImpl) share one definition
  // rather than each hardcoding their own copy.
  const int audioSampleRate = 48000;
  const int audioChannels = 2;
  const int audioBitsPerSample = 16;

  // Opus encoding parameters. 20ms is Opus's own most common/default
  // frame duration, and happens to already match AudioCapturePulse's
  // existing ~20ms PCM capture fragment size, so no capture-side
  // rechunking was needed to adopt this. OPUS_APPLICATION_AUDIO (not
  // _VOIP or _RESTRICTED_LOWDELAY) is the right general-purpose choice
  // since this captures arbitrary system audio, not just a voice call.
  const int audioOpusFrameSamples = 960; // 20ms @ 48000 Hz, per channel
  const int audioOpusBitrate = 64000; // bits/sec -- see this header's
                                      // top comment for the tradeoff

  // Most encoded frames the servers' capture-to-main-thread handoff
  // queues will hold (25 x 20ms = 500ms). Late audio is worthless, so
  // if the main thread falls behind the oldest frames are dropped
  // rather than piling up and being dumped on the clients in a burst.
  const unsigned audioMaxQueuedFrames = 25;
}
#endif
