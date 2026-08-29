const GLOBAL_RESOLUTION_KEY = "gamescope_game_resolution_global";

export function getGlobalResolution(): string {
  return window.settingsStore?.GetClientSetting?.(GLOBAL_RESOLUTION_KEY)?.[0] || "Default";
}

export async function setGlobalResolution(value: string): Promise<string> {
  const setting = window.settingsStore?.GetClientSetting?.(GLOBAL_RESOLUTION_KEY);
  const setter = setting?.[1];
  if (!setter) throw new Error("Steam settings are unavailable");
  await Promise.resolve(setter(value));
  return getGlobalResolution();
}
