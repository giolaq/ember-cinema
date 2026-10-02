import NativeEmberCore from './NativeEmberCore';

// Must match EmberKey in src/ember.h.
export const EmberKey = {
  None: 0,
  Ok: 1,
  Left: 2,
  Right: 3,
  Up: 4,
  Down: 5,
  Back: 6,
  PlayPause: 7,
  Play: 8,
  Pause: 9,
  FastForward: 10,
  Rewind: 11,
} as const;
export type EmberKeyCode = (typeof EmberKey)[keyof typeof EmberKey];

export type Screen = 'home' | 'details' | 'player';

export interface Movie {
  title: string;
  tag: string;
  description: string;
  poster: string;
  url: string;
}

export interface EmberState {
  screen: Screen;
  movie: number;
  control: number;
  controls: boolean;
  loading: boolean;
  started: boolean;
  paused: boolean;
  ended: boolean;
  position: number;
  duration: number;
  error: string;
}

export interface EmberAction {
  type:
    | 'none'
    | 'start'
    | 'stop'
    | 'resume'
    | 'pause'
    | 'replay'
    | 'seek'
    | 'exit';
  position: number;
  url?: string;
}

const catalog: Movie[] = JSON.parse(NativeEmberCore.getCatalog());

export const EmberCore = {
  catalog,
  state: (): EmberState => JSON.parse(NativeEmberCore.getState()),
  key: (key: EmberKeyCode, repeat: boolean): EmberAction =>
    JSON.parse(NativeEmberCore.key(key, repeat)),
  stop: () => NativeEmberCore.stop(),
  playerBusy: () => NativeEmberCore.playerBusy(),
  playerReady: (durationMs: number) =>
    NativeEmberCore.playerReady(Math.round(durationMs)),
  playerProgress: (positionMs: number, playing: boolean) =>
    NativeEmberCore.playerProgress(Math.round(positionMs), playing),
  playerEnded: () => NativeEmberCore.playerEnded(),
  playerFailed: (message: string) => NativeEmberCore.playerFailed(message),
};
