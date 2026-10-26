<p align="center">
  <img src="icons/172x172/harbour-pangolin-ui.png" width="120" alt="Pangolin app icon">
</p>

<h1 align="center">Pangolin for Sailfish OS</h1>

<p align="center">
  A native Sailfish OS client for <a href="https://github.com/fosrl/pangolin">Pangolin</a>:
  connect your phone to private resources with one tap.
</p>

---

**harbour-pangolin-ui** is a Silica app plus a Top Menu quick toggle for the
[Pangolin CLI](https://github.com/fosrl/cli). It starts and stops the Pangolin client
tunnel, shows the live connection status, and lets you log in with a device code. Private
resources (aliases such as `app.example.internal`) then work in the browser and in other apps.

The package bundles everything it needs: the Pangolin CLI and a small root helper.
Installing the RPM is the only step that needs root. No `sudo`, no sudoers rules, and no
developer-mode password while using the app.

> This is an unofficial community project. It is not affiliated with or endorsed by
> Fossorial (the Pangolin authors) or Jolla.

## Features

- **Start / Stop** the tunnel from the app, the app cover, or a **Top Menu quick toggle**.
  All three control the same systemd service, so they always agree.
- **Live status**: connection state, organization, and per-site status with latency.
- **Device-code login** (`pangolin login`): copy the one-time code and open the browser in one tap.
- **Private DNS that works on Sailfish**: aliases of private resources resolve to their tunnel
  addresses. This includes a workaround for systemd-resolved 238 (see [DNS](#dns-private-resource-aliases)).
- **Logs**: the service output and the client log, viewed inside the app.
- **Native look**: Silica components, ambience colors, a Sailfish-style icon, portrait and landscape.
- **Translations**: English and Ukrainian.

## Requirements

- Sailfish OS on an **aarch64** device (most current phones). Other architectures, including the
  SDK emulator, are not packaged.
- A Pangolin server and an account with at least one site and private resource.
- Kernel TUN support (the client creates a `pangolin` TUN interface).

## Installation

1. Build the aarch64 RPM (see [Building](#building)).
2. Install it as root, e.g. in Terminal:
   ```sh
   devel-su pkcon install-local harbour-pangolin-ui-*.rpm
   ```
3. Open **Pangolin**, pull down → **Login**, and finish the device-code login in the browser.
   If your account has several organizations, the CLI asks you to pick one interactively.
   In that case run `pangolin login` once in Terminal instead.
4. Press **Start**.

**Quick toggle:** open *Settings › Top Menu*, find **Pangolin** among the toggles, and enable it.
If it doesn't show up, restart the home screen (*Settings › Developer tools › Restart home screen*) or reboot.

## How it works

```
App (Start/Stop, cover)     ──systemctl --user──▶  systemd --user
Top Menu toggle (lipstick)  ──D-Bus (session)───▶       │
                                                        ▼
                      harbour-pangolin.service
                        ExecStart: pangolin-helper up --attach …   (setuid root)
                        ExecStop:  pangolin down                    (no root needed)
```

| Part | Path | Purpose |
|---|---|---|
| App | `/usr/bin/harbour-pangolin-ui` | Silica UI (C++ `PangolinController` + QML) |
| Pangolin CLI | `/usr/bin/pangolin` | [fosrl/cli](https://github.com/fosrl/cli) 0.19.0, built with a small olm patch (see [DNS](#dns-private-resource-aliases)) |
| Root helper | `/usr/libexec/harbour-pangolin-ui/pangolin-helper` | small setuid-root C program |
| Service | `/usr/lib/systemd/user/harbour-pangolin.service` | systemd user unit that runs the client |
| Quick toggle | `/usr/share/jolla-settings/…` | pure-QML `SettingsToggle` for the Top Menu |

The app GUI never runs as root. Only the helper does, and it is deliberately narrow:

- It only runs the fixed, root-owned `/usr/bin/pangolin`, and only `up --attach` with an
  allow-list of flags. Flags that take a path, open a listener or rename interfaces
  (`--tls-client-cert`, `--http-addr`, `--interface-name`, `--subnet-router`) are rejected.
- It clears the environment. It sets `SUDO_USER` so the CLI keeps using your
  `~/.config/pangolin` login, and pins the CLI's log path so a user-edited config can't make
  root write files elsewhere.
- It refuses to run if `~/.config/pangolin` or its JSON files are symlinks.

This follows the approach of [harbour-tailscale](https://github.com/ilpianista/harbour-Tailscale):
the package grants root once at install time. Here, though, only a tiny helper is privileged, not the whole app.

### DNS (private resource aliases)

Private resources are reached by alias names that only Pangolin's DNS proxy (on the `pangolin`
interface) can resolve. That requires **Override system DNS**, which is on by default.

Sailfish ships **systemd-resolved 238**. The upstream client (olm 1.10.1) calls `Link.SetDefaultRoute`,
which only exists in systemd ≥ 240. On Sailfish that call fails, and the client gives up before it:
- sets the catch-all routing domain `~.`, so aliases resolve to their *public* addresses;
- marks itself registered, so status shows *Disconnected* even though the tunnel works;
- starts its keepalive ping monitor.

The bundled CLI is therefore built from source with
[`pangolin-cli/olm-systemd238.patch`](pangolin-cli/olm-systemd238.patch), which treats the missing
method as non-fatal. On systemd 238 the `~.` domain alone sends all queries to the tunnel. The client
reverts the DNS settings on stop.

### Logs

On Sailfish the user can't read the user journal, so the service writes its output to
`/run/user/<uid>/harbour-pangolin.log` instead (truncated on each start).
It is shown in the app under *pull-down › Client log*. If the service fails within about 15 s
of Start, the app prints the exit code, the last log lines, and a hint in *Output*.

## Building

Requires the [Sailfish SDK](https://docs.sailfishos.org/Tools/Sailfish_SDK/).

1. Build the patched CLI into `rpm/` (used as `%{_sourcedir}` by the SDK). This needs Docker:
   ```sh
   sh pangolin-cli/build.sh
   ```
   The script clones fosrl/cli at the spec's `pangolin_version`, applies
   `pangolin-cli/olm-systemd238.patch` to the exact olm version that release uses, and builds
   `rpm/pangolin-cli_linux_arm64` with the upstream Makefile's flags.
2. Open `harbour-pangolin-ui.pro` in the SDK's Qt Creator, choose the **aarch64** target,
   and deploy as RPM. The spec compiles `helper/pangolin-helper.c` and installs it setuid.

**Updating the bundled CLI:** bump `%define pangolin_version` in `rpm/harbour-pangolin-ui.spec`,
check that the patch still applies (or is no longer needed upstream), re-run `build.sh`, and
release a new RPM. The app intentionally doesn't offer `pangolin update`: it would replace the
patched binary that belongs to the package.

## Limitations

- **Not for Jolla Harbour.** The setuid helper, the disabled Sailjail sandbox (needed for TUN and
  root), and the files installed under `/usr/share/jolla-settings` and `/usr/lib/systemd/user`
  keep it out of the Jolla Store. Distribute it via OpenRepos, Chum, or sideloading.
- If the client was started from Terminal (detached), the quick toggle shows *off*, and tapping
  it fails because a client is already running. Stop it from the app first.
- Stopping relies on `pangolin down`. If that fails, the service ends up `failed` after 15 s.
- Whether connman overwrites the link's DNS settings after a Wi-Fi ↔ mobile data switch is untested.
  Stop and Start fixes it.
- The CLI writes files in `~/.config/pangolin` as root (e.g. `accounts.json` after new client
  credentials), so they may end up root-owned.
- `pangolin service install` (system-wide autostart) is not exposed.

## Security notes

Any process running as your user can bring the tunnel up as root through the helper, with the
allow-listed flags. That is the trade-off for "no password needed". It is much narrower than
making the whole app setuid. The symlink check is not race-free, and the CLI itself writes `accounts.json` as root. Read `helper/pangolin-helper.c`
(about 150 lines) before installing if this matters to you.

## Credits

- [Pangolin](https://github.com/fosrl/pangolin), [Pangolin CLI](https://github.com/fosrl/cli) and
  [olm](https://github.com/fosrl/olm) by Fossorial

## License

The app (everything in this repository) is MIT-licensed.

The RPM also bundles the **Pangolin CLI** binary, which Fossorial licenses under the
**GNU AGPL-3.0** ([LICENSE](https://github.com/fosrl/cli/blob/main/LICENSE)). It is built from
[fosrl/cli 0.19.0](https://github.com/fosrl/cli/tree/0.19.0) and [olm v1.10.1](https://github.com/fosrl/olm)
plus the modification in [`pangolin-cli/olm-systemd238.patch`](pangolin-cli/olm-systemd238.patch)
(AGPL-3.0, like olm). `pangolin-cli/build.sh` reproduces the build. If you redistribute the RPM,
the AGPL-3.0 terms apply to that binary.
