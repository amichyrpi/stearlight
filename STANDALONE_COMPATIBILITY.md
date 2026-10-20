<h1 align="center">Stearlight OS</h1>

<p align="center"><strong>Standalone Game Compatibility</strong></p>

<p align="center">
  Community testing results for standalone gaming on Stearlight OS.
</p>

## Compatibility ratings

<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="24"> **Verified**<br>
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/playable_50_1.png" alt="Playable" height="24"> **Playable**<br>
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unsupported_50.png" alt="Unsupported" height="24"> **Unsupported**<br>
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unknown_50_1.png" alt="Unknown" height="24"> **Unknown**<br>

## Tested games

| Game | Compatibility | Notes |
| :--- | :--- | :--- |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/438100/library_600x900.jpg" alt="VRChat cover" width="62"><br>[VRChat](https://store.steampowered.com/app/438100/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unsupported_50.png" alt="Unsupported" height="28"> **Unsupported** | Game will not launch due to anti-cheat / Lepton compatibility issues. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/504230/library_600x900.jpg" alt="Celeste cover" width="62"><br>[Celeste](https://store.steampowered.com/app/504230/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="28"> **Verified** | Runs at a stable 60 FPS. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/667970/library_600x900.jpg" alt="VTOL VR cover" width="62"><br>[VTOL VR](https://store.steampowered.com/app/667970/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/playable_50_1.png" alt="Playable" height="28"> **Playable** | Launches at 10â€“15 FPS. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/2386250/library_600x900.jpg" alt="It gets so lonely here cover" width="62"><br>[It gets so lonely here](https://store.steampowered.com/app/2386250/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="28"> **Verified** | Story, Quest controller input and music tested on Quest 2 with native ARM64 Ren'Py and Adreno GPU rendering. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/1598780/library_600x900.jpg" alt="Silly Polly Beast cover" width="62"><br>[Silly Polly Beast](https://store.steampowered.com/app/1598780/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unsupported_50.png" alt="Unsupported" height="28"> **Unsupported** | Standalone launch is unreliable on Quest 2: startup hangs or closes during scene loading. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/2530470/library_600x900.jpg" alt="Garden of Witches cover" width="62"><br>[Garden of Witches](https://store.steampowered.com/app/2530470/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="28"> **Verified** | Local gameplay, picture, Quest gamepad controls and music confirmed on Quest 2. Startup is slow. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/4122860/library_600x900.jpg" alt="Z.A.T.O. // I Love the World and Everything In It cover" width="62"><br>[Z.A.T.O. // I Love the World and Everything In It](https://store.steampowered.com/app/4122860/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/playable_50_1.png" alt="Playable" height="28"> **Playable** | Story, Quest controller input and music work with native ARM64 Ren'Py and Adreno GPU rendering. Menus and transitions remain slow. |

> **Note:** Steam artwork is fetched from Steam’s public CDN. Some newer titles use hashed image paths and might need a manually specified artwork URL.

### Contributing

Add a game to `games.json` using its Steam App ID, `status`, and `notes`.
Run `python generate_compatibility.py` to refresh this table.
If Steam is unreachable, the generator uses the optional `name` fallback.
