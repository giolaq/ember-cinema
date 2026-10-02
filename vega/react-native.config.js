// Autolinks the in-app EmberCore Turbo Module (built from CMakeLists.txt).
module.exports = {
  dependency: {
    platforms: {
      kepler: {
        autolink: {
          'tv.cinema.ember.core': {
            libraryName: 'libEmberCore.so',
            linkDynamic: true,
            provider: 'application',
            components: [],
            turbomodules: ['EmberCore'],
          },
        },
      },
    },
  },
};
