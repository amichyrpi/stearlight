<h1 align="center">Stearlight OS</h1>

<p align="center"><strong>Standalone Game Compatibility</strong></p>

<p align="center">
  Community testing results for standalone gaming on Stearlight OS.
</p>

## Compatibility ratings

<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="24"> **Verified**  
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/playable_50_1.png" alt="Playable" height="24"> **Playable**  
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unsupported_50.png" alt="Unsupported" height="24"> **Unsupported**  
<img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unknown_50_1.png" alt="Unknown" height="24"> **Unknown**

## Tested games

| Game                                                                                                                                                                                              | Compatibility                                                                                                                                                 | Notes                                                                 |
| :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------------------------ | :-------------------------------------------------------------------- |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/438100/library_600x900.jpg" alt="VRChat cover" width="62"><br>[VRChat](https://store.steampowered.com/app/438100/)   | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/unsupported_50.png" alt="Unsupported" height="28"> **Unsupported** | Game will not launch due to anti-cheat / Lepton compatibility issues. |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/504230/library_600x900.jpg" alt="Celeste cover" width="62"><br>[Celeste](https://store.steampowered.com/app/504230/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/verified_50.png" alt="Verified" height="28"> **Verified**          | Runs at a stable 60 FPS.                                              |
| <img src="https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/667970/library_600x900.jpg" alt="VTOL VR cover" width="62"><br>[VTOL VR](https://store.steampowered.com/app/667970/) | <img src="https://cdn.steamstatic.com/steamcommunity/public/images/steamworks_docs/english/playable_50_1.png" alt="Unsupported" height="28"> **Playable**     | Launches at 10–15 FPS.                                                |

> **Note:** Steam artwork is fetched from Steam’s public CDN. Some newer titles use hashed image paths and might need a manually specified artwork URL.

### Contributing

Add a game to `games.json` using its Steam App ID, `status`, and `notes`.
Run `python generate_compatibility.py` to refresh this table.
If Steam is unreachable, the generator uses the optional `name` fallback.
