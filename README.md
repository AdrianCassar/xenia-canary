# Netplay Fork

This is a fork of [Xenia-Canary Netplay](https://github.com/ahnewark/sunrise-xenia-canary-netplay) which implements online multiplayer features.
It has been built from the ground up and a handful of games found to work, though many games are yet to be tested. Check out the [Issues](https://github.com/ahnewark/sunrise-xenia-canary-netplay/issues) list for more details.

The REST API powering this fork can be found [here](https://github.com/AdrianCassar/Xenia-WebServices).

Massive thanks to @SarahGreyWolf for testing this fork with me for about a month, couldn't have done it without her.
Also, thank you to @Bo98 for creating the burnout5 xenia fork, I used that as a basis for this, and some of the code is still here I think.

Peace and hair grease

Codie

---

## FAQ:

Is UPnP implemented?
- Yes, **UPnP** is now implemented therefore manual port forwarding is no longer required.

Is **Systemlink** or **XLink Kai** supported?

- No, Systemlink and XLink Kai are not supported.

Can I host Xenia Web Services?

- Yes, [Xenia Web Services](https://github.com/AdrianCassar/Xenia-WebServices).

Is there a Netplay mousehook build?

- Yes, download it from [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay).

Are games dependant on servers?

- Yes a lot of games are dependant on servers therefore will not work, unless a server is developed for that game. For example many games requires EA servers, without them netplay will not work. 

Can I use multiple instances for the same game?

- No, you cannot use multiple instances due to the first instance using up ports. You will need to use a VM or a [Shadow PC](https://shadow.tech/).

Can I use multiple PCs on the same network?

- No, you currently cannot use different PCs on the same network to connect to a session hosted on the same network. However a VPN will get around this issue.

Where can I **download** the Canary Netplay build?

- You can download it from [releases](https://github.com/AdrianCassar/xenia-canary/releases).

## Config Setup

To connect to a **Xenia WebServices** server you can either privately host it yourself or connect to my server.

```toml
api_address = "https://xenia-netplay-2a0298c0e3f4.herokuapp.com"
```

UPnP is disabled by default, you can enable it in the config.
```toml
upnp = true
```

## Linux Notes

<details>
  <summary>Failure to Bind to Ports</summary>

### Failure to Bind to Ports
Binding to ports <= 1024 will usually fail on Linux as they are protected by default. To verify this is an issue you are encountering, search your log for the following message:
`NetDll_WSAGetLastError: 10013`

To fix this run this command:
```console
sudo sysctl net.ipv4.ip_unprivileged_port_start=999
echo 'sysctl net.ipv4.ip_unprivileged_port_start=999' | sudo tee /etc/sysctl.d/99-xenia.conf
```
This command configures privileged ports to start at port 999 instead of port 1024 in this logon session and future logons. This should allow for most games to now bind. 

If you are still seeing `NetDll_WSAGetLastError: 10013` in logs after running this, you can try rerunning the previous commands with a number lower than `999`. `23` should solve every case. You can try `0` but it will prevent you from running ssh.

It should also be noted that due to the way Steam Decks handle configuration, you will need to rerun this command on every reboot.

</details>

## Supported Games

| Game | Notes | Gameplay | Patches/Plugins | 
|---|---|---|---|
| CS:GO | Mousehook |
| CS:GO Beta | Mousehook |
| Call of Duty 2 | ```launch_module = "default_mp.xex"``` | [Deathmatch](https://www.youtube.com/watch?v=DR9Op_f1UUw) |
| Death Tank | |
| DiRT | | [Race](https://www.youtube.com/watch?v=udMf-MUzpEc) |
| GRID | |
| GTA V Beta | Requires ```protect_zero = false``` or use patches. | [Beta Showcase](https://www.youtube.com/watch?v=nIjZ7sRGZlo), [Beta Showcase](https://www.youtube.com/watch?v=YIBjy5ZJcq4) | [TU 13](https://github.com/AdrianCassar/Xenia-WebServices/blob/main/patches/545408A7%20-%20Grand%20Theft%20Auto%20V%20(TU13).patch.toml), [TU 10](https://github.com/AdrianCassar/Xenia-WebServices/blob/main/patches/545408A7%20-%20Grand%20Theft%20Auto%20V%20(TU10).patch.toml) |
| GTA V TU 2-13 | Must complete prologue, download gamesave [here](https://cdn.discordapp.com/attachments/641360906495983616/1101132116441440366/545408A7.rar). Unstable and often crashes. | [Solo Session](https://www.youtube.com/watch?v=lap7liW6pco) |
| Guilty Gear 2: Overture | |
| Gundam Operation Troy | [English Patch](https://github.com/Eight-Mansions/MSGOT/releases)
| Halo 3 ODST v13.2 using [Sunrise Server](https://github.com/ahnewark/Sunrise-Halo3-WebServices) | Mousehook | [Head to Head](https://www.youtube.com/watch?v=amS8OxH3exs) | [Halo 3 Patch](https://github.com/AdrianCassar/Xenia-WebServices/blob/main/patches/4D5307E6%20-%20Halo%203.patch.toml)
| Juiced 2 | |
| Kung Fu Panda: SLL | |
| Left 4 Dead | Mousehook | Compatible with GOTY. |
| Left 4 Dead GOTY | Mousehook | |
| Left 4 Dead 2 | Mousehook | |
| Left 4 Dead 2 Demo |
| Marble Blast Ultra | |
| Marvel Ultimate Alliance | |
| Marvel Ultimate Alliance 2 | |
| MotoGP 14 | Sprint Season only works |
| MotoGP 15 | Sprint Season only works |
| OutRun Online Arcade | | [Race](https://www.youtube.com/watch?v=-UqxjFgGvhk) |
| Portal 2 | Mousehook |
| Resident Evil 5 | | [Chapter 1](https://www.youtube.com/watch?v=SKgnUVairqs) |
| Ridge Racer 6 | |
| Saints Row 2 | | [Co-op](https://www.youtube.com/watch?v=YTw84keeWfs), [Setup Guide](https://www.youtube.com/watch?v=nf7TDOtTEIE) |
| Saints Row the Third / The Full Package | Unplayable due to broken graphics. Requires [Online Pass](https://www.xbox.com/en-GB/games/store/online-pass/BS7JTR0MN356) + license_mask |
| Saints Row IV | Unplayable due to broken graphics. Requires Online Pass + license_mask |
| Screwjumper! | |
| Splinter Cell: Double Agent | |
| Star Wars Battlefront III (Unreleased Game) | Alpha, Mar 17 2008 | [Conquest Taoonie](https://www.youtube.com/watch?v=C54jCqFnCmQ), [MP Event Stream](https://www.youtube.com/watch?v=xSpTmsSvP4s) |
| Team Fortress 2 | Mousehook |
| The Outfit | |
---

### Non-Supported Games

| Game  | Description  |
|---|---|
| Minecraft | Requires friend lists to invite friends. |
| Red Dead Redemption  | Connects online but cannot play with others. |
| Forza Motorsport 4 | Connects online but cannot play with others. |
| Grand Theft Auto 4 | Connects online but cannot play with others. |
| Saints Row 1 | Connects online but cannot play with others. |
| Gears of War 3 | Connects online but cannot play with others. |
| Chromehounds | Requires a file from a dedicated Xbox server. |

#### Requires Servers
- Activision Games
- EA Games
- Ubisoft Games

---

### [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/tree/netplay_canary_experimental#mousehook)
- [Releases](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay)

Netplay mousehook is a fork of netplay which adds support for playing games with mouse and keyboard.

---

<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary - Xbox 360 Emulator</h1>

Xenia Canary is an experimental fork of the Xenia emulator. For more information, see the
[Xenia Canary wiki](https://github.com/xenia-canary/xenia-canary/wiki).

Come chat with us about **emulator-related topics** on [Discord](https://discord.gg/Q9mxZf9).
For developer chat join `#dev` but stay on topic. Lurking is not only fine, but encouraged!
Please check the [FAQ](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page before asking questions.
We've got jobs/lives/etc, so don't expect instant answers.

Discussing illegal activities will get you banned.

## Status

Buildbot | Status | Releases
-------- | ------ | --------
Windows | [![CI](https://github.com/xenia-canary/xenia-canary/actions/workflows/Windows_build.yml/badge.svg?branch=canary_experimental)](https://github.com/xenia-canary/xenia-canary/actions/workflows/Windows_build.yml) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/cd506034fd8148309a45034925648499)](https://app.codacy.com/gh/xenia-canary/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/xenia-canary/xenia-canary/releases/latest) ◦ [All](https://github.com/xenia-canary/xenia-canary/releases)
Linux | Curently unsupported
Netplay Build | | [Latest](https://github.com/AdrianCassar/xenia-canary/releases/latest)

## Quickstart

See the [Quickstart](https://github.com/xenia-canary/xenia-canary/wiki/Quickstart) page.

## FAQ

See the [frequently asked questions](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page.

## Game Compatibility

See the [Game compatibility list](https://github.com/xenia-canary/game-compatibility/issues)
for currently tracked games, and feel free to contribute your own updates,
screenshots, and information there following the [existing conventions](https://github.com/xenia-canary/game-compatibility/blob/master/README.md).

## Building

See [building.md](docs/building.md) for setup and information about the
`xb` script. When writing code, check the [style guide](docs/style_guide.md)
and be sure to run clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

**For general rules and guidelines please see [CONTRIBUTING.md](.github/CONTRIBUTING.md).**

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through [missing functionality/bugs in games](https://github.com/xenia-canary/xenia-canary/labels/compat)
* Reduce the size of Xenia's [huge log files](https://github.com/xenia-canary/xenia-canary/issues/1526)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/xenia-canary/xenia-canary/labels/platform-linux)

See more projects [good for contributors](https://github.com/xenia-canary/xenia-canary/labels/good%20first%20issue). It's a good idea to ask on Discord and check the issues page before beginning work on
something.

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).
