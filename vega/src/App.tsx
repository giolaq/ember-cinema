import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {
  AppState,
  BackHandler,
  Image,
  StyleSheet,
  Text,
  TextStyle,
  View,
  useWindowDimensions,
} from 'react-native';
import {HWEvent, useTVEventHandler} from '@amazon-devices/react-native-kepler';
import {KeplerVideoSurfaceView} from '@amazon-devices/react-native-w3cmedia';

import {EmberPlayer} from './player';
import {
  EmberAction,
  EmberCore,
  EmberKey,
  EmberKeyCode,
  EmberState,
} from './turbo-modules/EmberCore';

const posters: Record<string, number> = {
  'sintel.jpg': require('../../assets/sintel.jpg'),
  'bunny.jpg': require('../../assets/bunny.jpg'),
  'journey.jpg': require('../../assets/journey.jpg'),
  'meadow.jpg': require('../../assets/meadow.jpg'),
};

const BG = '#070b11';
const LABEL = '#f0f5fa';
const ACCENT = '#fab36e';
const MUTED = '#a6bacf';

// Vega OS 1.2 reports raw key names (the Fire TV remote's OK arrives as
// 'kpenter'); later releases use the normalized ones.
function toKey(type: string): EmberKeyCode {
  switch (type) {
    case 'select':
    case 'enter':
    case 'kpenter':
      return EmberKey.Ok;
    case 'left':
      return EmberKey.Left;
    case 'right':
      return EmberKey.Right;
    case 'up':
      return EmberKey.Up;
    case 'down':
      return EmberKey.Down;
    case 'playpause':
    case 'play_pause':
    case 'space':
      return EmberKey.PlayPause;
    case 'play':
      return EmberKey.Play;
    case 'pause':
      return EmberKey.Pause;
    case 'skip_forward':
    case 'fastforward':
    case 'fast_forward':
    case 'forward':
      return EmberKey.FastForward;
    case 'skip_backward':
    case 'rewind':
      return EmberKey.Rewind;
    default:
      return EmberKey.None;
  }
}

function clock(ms: number) {
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(Math.floor(ms / 60000))}:${pad(Math.floor(ms / 1000) % 60)}`;
}

export const App = () => {
  const [state, setState] = useState<EmberState>(EmberCore.state);
  const refresh = useCallback(() => setState(EmberCore.state()), []);
  const player = useMemo(() => new EmberPlayer(refresh), [refresh]);
  const lastDown = useRef<string | null>(null);

  const perform = useCallback(
    (action: EmberAction) => {
      switch (action.type) {
        case 'start':
          player.open(action.url!);
          break;
        case 'stop':
          player.close();
          break;
        case 'resume':
          player.play();
          break;
        case 'pause':
          player.pause();
          break;
        case 'replay':
          player.seek(0);
          player.play();
          break;
        case 'seek':
          player.seek(action.position);
          break;
      }
    },
    [player],
  );

  const press = useCallback(
    (key: EmberKeyCode, repeat: boolean) => {
      // Seeks are relative to the live position rather than the last poll.
      player.progress();
      const action = EmberCore.key(key, repeat);
      perform(action);
      refresh();
      return action;
    },
    [perform, player, refresh],
  );

  const onTVEvent = useCallback(
    (event: HWEvent) => {
      const type = event.eventType ?? '';
      if (event.eventKeyAction === 1) {
        lastDown.current = null;
        return;
      }
      const key = toKey(type);
      if (key === EmberKey.None) {
        return;
      }
      const repeat = lastDown.current === type;
      lastDown.current = type;
      press(key, repeat);
    },
    [press],
  );
  useTVEventHandler(onTVEvent);

  useEffect(() => {
    const back = BackHandler.addEventListener('hardwareBackPress', () => {
      // Returning false on the home screen lets the system close the app.
      return press(EmberKey.Back, false).type !== 'exit';
    });
    const appState = AppState.addEventListener('change', (next) => {
      if (next === 'background' && EmberCore.state().screen === 'player') {
        EmberCore.stop();
        player.close();
        refresh();
      }
    });
    return () => {
      back.remove();
      appState.remove();
      player.close();
    };
  }, [player, press, refresh]);

  // Drives progress polling and the four-second control auto-hide.
  const onPlayer = state.screen === 'player';
  useEffect(() => {
    if (!onPlayer) {
      return;
    }
    const timer = setInterval(() => {
      player.progress();
      refresh();
    }, 250);
    return () => clearInterval(timer);
  }, [onPlayer, player, refresh]);

  const {width, height} = useWindowDimensions();
  const s = Math.min(width / 1280, height / 720);
  // Positions text by its baseline, matching the coordinates used in src/main.c.
  const text = (
    x: number,
    y: number,
    size: number,
    color = LABEL,
  ): TextStyle => ({
    position: 'absolute',
    left: x * s,
    top: (y - size) * s,
    fontSize: size * s,
    lineHeight: size * 1.5 * s,
    color,
  });
  const box = (x: number, y: number, w: number, h: number) => ({
    position: 'absolute' as const,
    left: x * s,
    top: y * s,
    width: w * s,
    height: h * s,
  });
  const button = (
    x: number,
    y: number,
    w: number,
    label: string,
    focus: boolean,
  ) => (
    <View
      key={label + x}
      style={[box(x, y, w, 54), focus ? styles.focused : styles.button]}>
      <Text style={text(22, 35, 23, focus ? '#0d121a' : '#ebf0f7')}>
        {label}
      </Text>
    </View>
  );

  const movies = EmberCore.catalog;
  const movie = movies[state.movie];

  if (onPlayer) {
    return (
      <View style={styles.root}>
        <KeplerVideoSurfaceView
          style={StyleSheet.absoluteFill}
          onSurfaceViewCreated={(handle: string) =>
            player.attachSurface(handle)
          }
          onSurfaceViewDestroyed={(handle: string) =>
            player.detachSurface(handle)
          }
        />
        {!state.started ? (
          <View style={[StyleSheet.absoluteFill, {backgroundColor: BG}]}>
            <Text style={text(64, 100, 20, ACCENT)}>EMBER / NOW PLAYING</Text>
            <Text style={text(64, 164, 42)}>{movie.title}</Text>
            <Text style={text(64, 240, 23)}>
              {state.error || 'Connecting to your film...'}
            </Text>
            <Text style={text(64, 660, 20)}>BACK Return to details</Text>
          </View>
        ) : state.controls ? (
          <View style={StyleSheet.absoluteFill}>
            {Array.from({length: 24}, (_, i) => (
              <View
                key={i}
                style={[
                  box(0, 420 + i * 12.5, 1280, 12.5),
                  {
                    backgroundColor: `rgba(5,9,15,${((i / 25) * 0.94).toFixed(
                      3,
                    )})`,
                  },
                ]}
              />
            ))}
            <Text style={text(64, 498, 30)}>{movie.title}</Text>
            <Text style={text(1080, 498, 18, ACCENT)}>
              {state.ended ? 'FINISHED' : state.paused ? 'PAUSED' : 'PLAYING'}
            </Text>
            <View style={[box(64, 530, 1152, 4), styles.track]} />
            <View
              style={[
                box(
                  64,
                  530,
                  state.duration ? (1152 * state.position) / state.duration : 0,
                  4,
                ),
                {backgroundColor: ACCENT},
              ]}
            />
            <Text style={text(64, 568, 20)}>
              {`${clock(state.position)}  /  ${clock(state.duration)}`}
            </Text>
            {button(420, 585, 138, '- 10 sec', state.control === 0)}
            {button(
              574,
              585,
              138,
              state.ended ? 'Replay' : state.paused ? 'Play' : 'Pause',
              state.control === 1,
            )}
            {button(728, 585, 138, '+ 10 sec', state.control === 2)}
            <Text style={text(64, 680, 18)}>
              LEFT / RIGHT Choose OK Select BACK Movie details
            </Text>
          </View>
        ) : null}
      </View>
    );
  }

  return (
    <View style={styles.root}>
      {/* Cinematic still with a soft shade, leaving clear room for copy. */}
      <Image source={posters[movie.poster]} style={box(440, 0, 840, 473)} />
      {Array.from({length: 28}, (_, i) => (
        <View
          key={`v${i}`}
          style={[
            box(440 + i * 30, 0, 30, 475),
            {
              backgroundColor: `rgba(7,11,17,${(1 - (i * 3) / 100).toFixed(
                3,
              )})`,
            },
          ]}
        />
      ))}
      {Array.from({length: 15}, (_, i) => (
        <View
          key={`h${i}`}
          style={[
            box(440, 280 + i * 14, 840, 14),
            {backgroundColor: `rgba(7,11,17,${(i / 14).toFixed(3)})`},
          ]}
        />
      ))}
      <Text style={text(64, 62, 24, ACCENT)}>E M B E R</Text>
      <Text style={text(275, 62, 18)}>C I N E M A</Text>
      <Text style={text(1020, 62, 18, '#91a6ba')}>THE DEMO EDITION</Text>
      {state.screen === 'home' ? (
        <>
          <Text style={text(64, 131, 17, ACCENT)}>
            SMALL FILMS. BIG WORLDS.
          </Text>
          <Text style={text(64, 195, 46)}>{movie.title}</Text>
          <Text style={text(64, 234, 18, MUTED)}>{movie.tag}</Text>
          <Text style={text(64, 290, 22)}>
            Discover something worth watching.
          </Text>
          <Text style={text(64, 386, 26)}>Tonight's selection</Text>
          <Text style={text(1040, 386, 18, '#8fa3ba')}>04 FILMS / CLIPS</Text>
          {movies.map((m, i) => {
            const x = 64 + i * 294;
            const selected = i === state.movie;
            return (
              <React.Fragment key={m.title}>
                {selected && (
                  <View
                    style={[
                      box(x - 4, 410, 280, 212),
                      {backgroundColor: ACCENT},
                    ]}
                  />
                )}
                <View style={[box(x, 414, 272, 204), styles.tile]} />
                <Image
                  source={posters[m.poster]}
                  style={box(x, 414, 272, 153)}
                />
                <Text style={text(x + 12, 598, 21)}>{m.title}</Text>
                {selected && (
                  <View
                    style={[
                      box(x + 10, 425, 62, 24),
                      {backgroundColor: ACCENT},
                    ]}>
                    <Text style={text(10, 18, 14, '#080a12')}>PLAY</Text>
                  </View>
                )}
              </React.Fragment>
            );
          })}
          <Text style={text(64, 674, 18, '#a6b8cc')}>
            LEFT / RIGHT Browse OK Movie details
          </Text>
          <Text style={text(936, 674, 17, '#758fa8')}>
            FIND YOUR NEXT STORY.
          </Text>
        </>
      ) : (
        <>
          <Text style={text(64, 138, 18, ACCENT)}>
            THE OPEN MOVIE COLLECTION
          </Text>
          <Text style={text(64, 209, 48)}>{movie.title}</Text>
          <Text style={text(64, 255, 19, MUTED)}>{movie.tag}</Text>
          <Text style={text(64, 328, 23)}>{movie.description}</Text>
          {button(64, 416, 220, 'Play demo', true)}
          <Text style={text(64, 520, 19)}>
            Streamed over HTTPS | Remote ready
          </Text>
          <Text style={text(64, 565, 18, '#8ca1b8')}>
            (c) Blender Foundation | sintel.org | bigbuckbunny.org | CC BY 3.0
          </Text>
          {state.error ? (
            <Text style={text(64, 610, 18, ACCENT)}>{state.error}</Text>
          ) : null}
          <Text style={text(64, 675, 18)}>OK Play BACK Browse movies</Text>
        </>
      )}
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: BG},
  button: {backgroundColor: '#1f2938'},
  focused: {backgroundColor: ACCENT},
  track: {backgroundColor: '#4d5969'},
  tile: {backgroundColor: '#131b26'},
});
