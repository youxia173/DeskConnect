# DeskConnect

**DeskConnect** is based on [Deskflow](https://github.com/deskflow/deskflow) source code.
This repository is the working tree we modify and ship under the **DeskConnect** name.

Upstream Deskflow provides cross-platform keyboard and mouse sharing (Windows / Linux / macOS).

## Layout

| Path | Notes |
|------|--------|
| `src/`, `cmake/`, `deploy/`, … | Product codebase (branded as DeskConnect) |
| `kdeconnect-kde/` | KDE Connect desktop sources kept for later integration |
| `docs/` | Upstream developer docs |

## Build output (Windows)

After a Release build, run:

`build/bin/Release/DeskConnect.exe`

## License

Deskflow-derived code remains under GPL-2.0 (see `LICENSE` and `LICENSES/`).
KDE Connect sources under `kdeconnect-kde/` keep their original licenses.
