# Licensing and provenance

The root [LICENSE](LICENSE) applies to original CoreTrace source code, scripts and documentation. It does not relicense third-party components or artwork.

| Material | Origin / terms |
| --- | --- |
| `native-portable/IntelMSR.bin` | Unmodified signed PawnIO module extracted from LibreHardwareMonitor 0.9.6, source commit `3d331e3370efb858411f19511373eff65a218701`. Module project: [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules); LGPL-2.1-or-later. See the bundled `LICENSE-PawnIO-Modules.txt` and upstream source/build instructions. |
| LibreHardwareMonitor reference materials | [Source at the extraction commit](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/3d331e3370efb858411f19511373eff65a218701), MPL-2.0. The native app does not load a managed LibreHardwareMonitor DLL. |
| PawnIO driver and `PawnIOLib.dll` | Separately installed from [PawnIO](https://github.com/namazso/PawnIO); not redistributed in the portable package. |
| llvm-mingw / linked runtime components | [llvm-mingw 20261020](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020). Bundled LLVM notice and exceptions are retained; see the upstream distribution for component-specific notices. Windows supplies UCRT. |
| Existing panda icon: `native-src/CoreTrace-icon-*.png`, `native-src/CoreTrace.ico`, `native-portable/CoreTrace-transparent.ico` | User-supplied meme artwork and its converted/transparent derivatives. Original authorship and redistribution license have not been established. These assets are **not covered by the project's MIT grant**. Replace them with artwork you have rights to use when making your own distribution. |
| `guide/images/*.png` | Rendered by CoreTrace with synthetic demonstration data. These interface previews contain no desktop capture or real application usage history. |

The portable package includes this notice, the application license and the existing third-party notices. Keep those materials with redistributed packages. The MIT text is the standard [Open Source Initiative text](https://opensource.org/license/mit).
