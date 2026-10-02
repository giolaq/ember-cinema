import 'react-native';
import {render} from '@testing-library/react-native';
import * as React from 'react';

import {App} from '../src/App';

jest.mock('../src/turbo-modules/NativeEmberCore');
jest.mock('@amazon-devices/react-native-w3cmedia', () => ({
  VideoPlayer: jest.fn(),
  KeplerVideoSurfaceView: 'KeplerVideoSurfaceView',
}));
// Some Vega preset mocks return undefined subscriptions, so stub the APIs
// App subscribes to.
const subscription = () => ({remove: jest.fn()});
const ReactNative = require('react-native');
Object.defineProperty(ReactNative, 'BackHandler', {
  value: {addEventListener: jest.fn(subscription)},
});
Object.defineProperty(ReactNative, 'useWindowDimensions', {
  value: () => ({width: 1920, height: 1080, scale: 1, fontScale: 1}),
});

describe('App', () => {
  it('renders the home screen from the native catalog', () => {
    const screen = render(<App />);
    expect(screen.getByText("Tonight's selection")).toBeTruthy();
    expect(screen.getAllByText('Sintel').length).toBe(2);
    expect(screen.getByText('Bunny: The Meadow')).toBeTruthy();
    expect(screen.getByText('PLAY')).toBeTruthy();
  });
});
