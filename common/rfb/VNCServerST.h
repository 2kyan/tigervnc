/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2009-2019 Pierre Ossman for Cendio AB
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

// -=- VNCServerST.h

// Single-threaded VNCServer implementation

#ifndef __RFB_VNCSERVERST_H__
#define __RFB_VNCSERVERST_H__

#include <functional>

#include <sys/time.h>

#include <core/Timer.h>

#include <rfb/VNCServer.h>
#include <rfb/Blacklist.h>
#include <rfb/Cursor.h>
#include <rfb/ScreenSet.h>

namespace rfb {

  class VNCSConnectionST;
  class ComparingUpdateTracker;
  class ListConnInfo;
  class PixelBuffer;
  class KeyRemapper;
  class SDesktop;

  class VNCServerST : public VNCServer,
                      public core::Timer::Callback {
  public:
    // -=- Constructors

    //   Create a server exporting the supplied desktop.
    VNCServerST(const char* name_, SDesktop* desktop_);
    virtual ~VNCServerST();


    // Methods overridden from VNCServer

    // addSocket
    //   Causes the server to allocate an RFB-protocol management
    //   structure for the socket & initialise it.
    bool addSocket(network::Socket* sock, bool outgoing=false,
                   AccessRights ar=AccessDefault) override;

    // removeSocket
    //   Clean up any resources associated with the Socket
    void removeSocket(network::Socket* sock) override;

    // getSockets() gets a list of sockets.  This can be used to generate an
    // fd_set for calling select().
    void getSockets(std::list<network::Socket*>* sockets) override;

    // processSocketReadEvent
    //   Read more RFB data from the Socket.  If an error occurs during
    //   processing then shutdown() is called on the Socket, causing
    //   removeSocket() to be called by the caller at a later time.
    void processSocketReadEvent(network::Socket* sock) override;

    // processSocketWriteEvent
    //   Flush pending data from the Socket on to the network.
    void processSocketWriteEvent(network::Socket* sock) override;

    void blockUpdates() override;
    void unblockUpdates() override;
    uint64_t getMsc() override;
    void queueMsc(uint64_t target) override;
    void setPixelBuffer(PixelBuffer* pb, const ScreenSet& layout) override;
    void setPixelBuffer(PixelBuffer* pb) override;
    void setScreenLayout(const ScreenSet& layout) override;
    const PixelBuffer* getPixelBuffer() const override { return pb; }

    void requestClipboard() override;
    void announceClipboard(bool available) override;
    void sendClipboardData(const char* data) override;

    void approveConnection(network::Socket* sock, bool accept,
                           const char* reason) override;
    void closeClients(const char* reason) override {closeClients(reason, nullptr);}
    SConnection* getConnection(network::Socket* sock) override;

    void add_changed(const core::Region& region) override;
    void add_copied(const core::Region& dest,
                    const core::Point& delta) override;
    void setCursor(int width, int height, const core::Point& hotspot,
                   const uint8_t* data) override;
    void setCursorPos(const core::Point& p, bool warped) override;
    void setName(const char* name_) override;
    void setLEDState(unsigned state) override;

    void bell() override;

    // VNCServerST-only methods

    // Methods to get the currently set server state

    const ScreenSet& getScreenLayout() const { return screenLayout; }
    const Cursor* getCursor() const { return cursor; }
    const core::Point& getCursorPos() const { return cursorPos; }
    const char* getName() const { return name.c_str(); }
    unsigned getLEDState() const { return ledState; }
    bool isDesktopReady() const { return desktopStarted; }

    // Audio (TigerVNC-native extension, see audioTypes.h). Not part of
    // the SDesktop-facing interface above -- called directly by
    // whatever owns the real capture backend (e.g. x0vncserver's own
    // audio integration code, which already holds a concrete
    // VNCServerST reference). Broadcasts to every connected client
    // that has both negotiated support and asked to receive audio
    // (VNCSConnectionST::audioEnabled()), same pattern as bell()/
    // setLEDState() broadcasting to every client unconditionally.
    void sendAudioBegin();
    void sendAudioData(const uint8_t* data, size_t length);
    void sendAudioEnd();

    // Whether a stream is currently active (i.e. between a
    // sendAudioBegin() and the next sendAudioEnd()) -- lets a client
    // that enables audio mid-stream (audioEnabled() flips true after
    // the real sendAudioBegin() already broadcast to whoever was
    // connected at the time) get an immediate synthesized begin of its
    // own instead of only ever seeing data/end with no begin. See
    // VNCSConnectionST::handleAudioControl().
    bool isAudioStreaming() const { return audioStreaming; }

    // Registers a hook for requestAudioRetry() below to call -- lets
    // whatever owns the real capture backend (same platform code that
    // calls sendAudioBegin/Data/End() above) give it a shot at
    // reconnecting to the system's sound server on demand, e.g. from
    // AudioCapturePulse::retryNow(). Optional: a server variant that
    // never registers one just has no manual-retry trigger, independent
    // of whatever automatic retry the capture backend does on its own.
    void setAudioRetryCallback(std::function<void()> cb) {
      audioRetryCallback = std::move(cb);
    }

    // Called when a client asks to enable audio (see
    // VNCSConnectionST::handleAudioControl()) -- gives the registered
    // callback above a chance to retry if the capture backend isn't
    // currently available. Cheap/safe to call unconditionally, even
    // while already connected and streaming fine -- the callback on the
    // other end (e.g. AudioCapturePulse::retryNow()) is responsible for
    // being a no-op in that case.
    void requestAudioRetry() {
      if (audioRetryCallback)
        audioRetryCallback();
    }

    // Event handlers
    void keyEvent(uint32_t keysym, uint32_t keycode, bool down);
    void pointerEvent(VNCSConnectionST* client,
                      const core::Point& pos, uint16_t buttonMask);

    void handleClipboardRequest(VNCSConnectionST* client);
    void handleClipboardAnnounce(VNCSConnectionST* client, bool available);
    void handleClipboardData(VNCSConnectionST* client, const char* data);

    unsigned int setDesktopSize(VNCSConnectionST* requester,
                                int fb_width, int fb_height,
                                const ScreenSet& layout);

    // closeClients() closes all RFB sessions, except the specified one (if
    // any), and logs the specified reason for closure.
    void closeClients(const char* reason, network::Socket* sock);

    // queryConnection() does some basic checks and then passes on the
    // request to the desktop.
    void queryConnection(VNCSConnectionST* client, const char* userName);

    // clientReady() is called by a VNCSConnectionST instance when the
    // client has completed the handshake and is ready for normal
    // communication.
    void clientReady(VNCSConnectionST* client, bool shared);

    // Estimated time until the next time new updates will be pushed
    // to clients
    int msToNextUpdate();

    // Part of the framebuffer that has been modified but is not yet
    // ready to be sent to clients
    core::Region getPendingRegion();

    // getRenderedCursor() returns an up to date version of the server
    // side rendered cursor buffer
    const RenderedCursor* getRenderedCursor();

  protected:

    // Timer callbacks
    void handleTimeout(core::Timer* t) override;

    // - Internal methods

    void startDesktop();
    void stopDesktop();
    void checkDesktopReady();
    void desktopReady();

    // - Check how many of the clients are authenticated.
    int authClientCount();

    bool needRenderedCursor();
    void startFrameClock();
    void stopFrameClock();
    void writeUpdate();

    bool getComparerState();

  protected:
    Blacklist blacklist;

    SDesktop* desktop;
    bool desktopStarted;
    bool desktopStarting;
    bool audioStreaming;
    std::function<void()> audioRetryCallback;
    int blockCounter;
    PixelBuffer* pb;
    ScreenSet screenLayout;
    unsigned int ledState;

    std::string name;

    std::list<VNCSConnectionST*> clients;
    VNCSConnectionST* pointerClient;
    VNCSConnectionST* clipboardClient;
    std::list<VNCSConnectionST*> clipboardRequestors;

    time_t pointerClientTime;

    ComparingUpdateTracker* comparer;

    core::Point cursorPos;
    Cursor* cursor;
    RenderedCursor renderedCursor;
    bool renderedCursorInvalid;

    KeyRemapper* keyRemapper;

    core::Timer idleTimer;
    core::Timer disconnectTimer;
    core::Timer connectTimer;

    uint64_t msc, queuedMsc;
    core::Timer frameTimer;
  };

};

#endif

