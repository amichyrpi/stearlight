export {};

declare global {
  interface Window {
    SteamClient?: any;
    settingsStore?: any;
    appDetailsStore?: any;
    Router?: any;
  }
}
