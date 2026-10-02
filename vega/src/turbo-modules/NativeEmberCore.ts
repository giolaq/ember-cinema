import type {
  Int32,
  KeplerTurboModule,
} from '@amazon-devices/keplerscript-turbomodule-api';
import {TurboModuleRegistry} from '@amazon-devices/keplerscript-turbomodule-api';

// Bridge to the shared C core in ../../../src/ember.c. State and catalog are
// returned as JSON because codegen JSObject returns cannot be populated.
export interface EmberCore extends KeplerTurboModule {
  getCatalog: () => string;
  getState: () => string;
  key: (key: Int32, repeat: boolean) => string;
  stop: () => void;
  playerBusy: () => void;
  playerReady: (durationMs: Int32) => void;
  playerProgress: (positionMs: Int32, playing: boolean) => boolean;
  playerEnded: () => void;
  playerFailed: (message: string) => void;
}

export default TurboModuleRegistry.getEnforcing<EmberCore>('EmberCore');
