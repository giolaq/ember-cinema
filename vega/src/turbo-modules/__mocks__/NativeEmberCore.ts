// Jest stand-in for the native core: a tiny catalog and a fixed home state.
export default {
  getCatalog: () =>
    JSON.stringify(
      [
        'Sintel',
        'Big Buck Bunny',
        'Sintel: The Journey',
        'Bunny: The Meadow',
      ].map((title, i) => ({
        title,
        tag: 'TAG',
        description: 'Description',
        poster: ['sintel.jpg', 'bunny.jpg', 'journey.jpg', 'meadow.jpg'][i],
        url: 'https://example.com/movie.mp4',
      })),
    ),
  getState: () =>
    JSON.stringify({
      screen: 'home',
      movie: 0,
      control: 1,
      controls: false,
      loading: false,
      started: false,
      paused: false,
      ended: false,
      position: 0,
      duration: 0,
      error: '',
    }),
  key: () => JSON.stringify({type: 'none', position: 0}),
  stop: jest.fn(),
  playerBusy: jest.fn(),
  playerReady: jest.fn(),
  playerProgress: jest.fn(() => false),
  playerEnded: jest.fn(),
  playerFailed: jest.fn(),
};
