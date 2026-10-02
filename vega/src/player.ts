import {VideoPlayer} from '@amazon-devices/react-native-w3cmedia';

import {EmberCore} from './turbo-modules/EmberCore';

const CONNECT_TIMEOUT_MS = 15000;

// Owns the W3C VideoPlayer for one stream at a time and reports its lifecycle
// to the C core, mirroring the MediaPlayer worker in src/main.c.
export class EmberPlayer {
  private video: VideoPlayer | null = null;
  private surface: string | null = null;
  private session = 0;
  private releasing: Promise<void> | null = null;
  private initializing = false;
  private watchdog: ReturnType<typeof setTimeout> | null = null;

  constructor(private readonly onChange: () => void) {}

  get busy() {
    return this.video !== null || this.releasing !== null;
  }

  async open(url: string) {
    if (this.busy) {
      EmberCore.playerBusy();
      this.onChange();
      return;
    }
    const session = ++this.session;
    const video = new VideoPlayer();
    this.video = video;
    this.watchdog = setTimeout(
      () =>
        this.fail(
          session,
          'Could not stream this film. Check your connection and try again.',
        ),
      CONNECT_TIMEOUT_MS,
    );
    this.initializing = true;
    try {
      await video.initialize();
    } catch {
      this.initializing = false;
      this.fail(session, 'Unable to create the video player.');
      this.releasing ??= this.teardown(video);
      return;
    }
    this.initializing = false;
    if (session !== this.session) {
      // Back was pressed while connecting; release the player now it is ready.
      this.releasing = this.teardown(video);
      return;
    }
    video.addEventListener('loadedmetadata', () => {
      if (session !== this.session) {
        return;
      }
      this.clearWatchdog();
      video
        .play()
        .catch(() =>
          this.fail(session, 'Playback could not start. Press Back to retry.'),
        );
      EmberCore.playerReady(video.duration * 1000);
      this.onChange();
    });
    video.addEventListener('ended', () => {
      if (session === this.session) {
        EmberCore.playerEnded();
        this.onChange();
      }
    });
    video.addEventListener('error', () =>
      this.fail(
        session,
        'Could not stream this film. Check your connection and try again.',
      ),
    );
    if (this.surface) {
      video.setSurfaceHandle(this.surface);
    }
    video.autoplay = false;
    video.src = url;
  }

  // Called on Back, backgrounding, or a failure; the core already left the player.
  close() {
    this.session++;
    this.clearWatchdog();
    const video = this.video;
    // A player still initializing is released by open() once it resolves.
    if (!video || this.releasing || this.initializing) {
      return;
    }
    this.releasing = this.teardown(video);
  }

  play() {
    this.video?.play().catch(() => {});
  }

  pause() {
    this.video?.pause();
  }

  seek(positionMs: number) {
    if (this.video) {
      this.video.currentTime = positionMs / 1000;
    }
  }

  // Polled by the UI: feeds playback progress into the core's end detection.
  progress() {
    const video = this.video;
    if (video && !this.releasing) {
      EmberCore.playerProgress(video.currentTime * 1000, !video.paused);
    }
  }

  position() {
    return this.video ? this.video.currentTime * 1000 : 0;
  }

  attachSurface(handle: string) {
    this.surface = handle;
    // open() attaches the surface itself once initialize() resolves.
    if (!this.initializing) {
      this.video?.setSurfaceHandle(handle);
    }
  }

  detachSurface(handle: string) {
    this.video?.clearSurfaceHandle(handle);
    if (this.surface === handle) {
      this.surface = null;
    }
  }

  private fail(session: number, message: string) {
    if (session !== this.session) {
      return;
    }
    EmberCore.playerFailed(message);
    this.close();
    this.onChange();
  }

  private clearWatchdog() {
    if (this.watchdog) {
      clearTimeout(this.watchdog);
      this.watchdog = null;
    }
  }

  private async teardown(video: VideoPlayer) {
    try {
      video.pause();
      if (this.surface) {
        video.clearSurfaceHandle(this.surface);
      }
      await video.deinitialize();
    } catch {
      // The player is discarded either way.
    }
    if (this.video === video) {
      this.video = null;
    }
    this.releasing = null;
  }
}
