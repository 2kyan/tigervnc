/* Copyright 2026 TigerVNC Team
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/time.h>

#include <gtest/gtest.h>

#include <rfb/Congestion.h>

// Congestion reads the clock itself, so give it one we control
static long long fakeTime = 1000000000000LL;

extern "C" int gettimeofday(struct timeval* tv, void* /*tz*/)
{
  tv->tv_sec = fakeTime / 1000000;
  tv->tv_usec = fakeTime % 1000000;
  return 0;
}

static void advance(int ms)
{
  fakeTime += ms * 1000LL;
}

// A few seconds of normal updates, each followed by its ping and a pong
// after a 10 ms round trip
static unsigned warmUp(rfb::Congestion* congestion)
{
  unsigned pos = 0;

  for (int i = 0; i < 100; i++) {
    pos += 20000;
    congestion->updatePosition(pos);
    congestion->sentPing();
    advance(10);
    congestion->gotPong();
    advance(40);
  }

  congestion->updatePosition(pos);

  return pos;
}

TEST(Congestion, staticScreen)
{
  rfb::Congestion congestion;
  unsigned pos;

  pos = warmUp(&congestion);

  for (int t = 0; t < 60000; t += 100) {
    advance(100);
    congestion.updatePosition(pos);
    EXPECT_FALSE(congestion.isCongested()) << "at " << t << " ms";
  }
}

// While a large update drains, the ETA must never be 0, as the caller
// re-arms its timer with it and would busy-loop until uncongested
TEST(Congestion, largeUpdateEta)
{
  for (int pongDelay : { 0, 300 }) {
    rfb::Congestion congestion;
    unsigned pos;

    pos = warmUp(&congestion);

    // As in VNCSConnectionST::writeDataUpdate(): ping, update, ping.
    // 1 MB at 2 MB/s with a 10 ms round trip, the first pong possibly
    // late.
    congestion.sentPing();
    pos += 1000000;
    congestion.updatePosition(pos);
    congestion.sentPing();

    for (int t = 1; t <= 1000; t++) {
      advance(1);
      if (t == 10 + pongDelay)
        congestion.gotPong();
      if (t == 510)
        congestion.gotPong();
      congestion.updatePosition(pos);
      if (congestion.isCongested())
        EXPECT_NE(congestion.getUncongestedETA(), 0)
          << "at " << t << " ms, pong delay " << pongDelay << " ms";
    }

    EXPECT_FALSE(congestion.isCongested());
  }
}

// Data that isn't followed by pings (e.g. audio) while the screen is
// static used to overflow the in-flight estimate after ~20 seconds,
// reporting congestion with an ETA of 0 forever
TEST(Congestion, trickleWithoutPings)
{
  rfb::Congestion congestion;
  unsigned pos;

  pos = warmUp(&congestion);

  for (int t = 0; t < 60000; t += 20) {
    advance(20);
    pos += 190;
    if (t % 100 != 0)
      continue;
    congestion.updatePosition(pos);
    EXPECT_FALSE(congestion.isCongested()) << "at " << t << " ms";
  }
}
