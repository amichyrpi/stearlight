# Third-party notices

## IHSlib (legacy receiver only)

The optional legacy `svrt-receiver` target dynamically links to
[IHSlib](https://github.com/mariotaku/ihslib) commit
`560908104b1f884792f90d8f98e33ec8c61106f5` for Steam Remote Play discovery
and authorization.

The Stearlight OS appliance does not build this target or use IHSlib. Its
native Steam Frame path launches Valve's Steam client, which owns discovery,
pairing, authorization, transport, and the Steam Frame UI.

IHSlib is licensed under the GNU Lesser General Public License, version 3.0.
Its complete license text is installed as `share/licenses/svrt/IHSlib-LICENSE`.
The corresponding source is available from the upstream repository at the
commit recorded above.
