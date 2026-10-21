<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary Netplay - Xbox 360 Emulator</h1>

[![Release](https://img.shields.io/github/v/release/AdrianCassar/xenia-canary?label=release)](https://github.com/AdrianCassar/xenia-canary/releases/latest)
[![CI](https://github.com/AdrianCassar/xenia-canary/actions/workflows/Orchestrator.yml/badge.svg?branch=netplay_canary_experimental)](https://github.com/AdrianCassar/xenia-canary/actions?query=branch%3Anetplay_canary_experimental)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/d814c4b6aa444dcc9c1631e0224b2739)](https://app.codacy.com/gh/AdrianCassar/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)

A fork of [Xenia Canary](https://github.com/xenia-canary/xenia-canary) that adds online multiplayer to Xbox 360 games, over the internet (emulated Xbox Live) or on a local network (Systemlink).

Sessions are matched through [Xenia Web Services](https://github.com/AdrianCassar/Xenia-WebServices#xenia-web-services), a REST API that anyone can self-host. Sessions currently online on the public server are listed at **[xenia-netplay-2a0298c0e3f4.herokuapp.com](https://xenia-netplay-2a0298c0e3f4.herokuapp.com/)**.

## Features

* **Xbox Live** - Create, search and join online sessions through a Xenia Web Services server.
* **Systemlink** - LAN play, including over tunnels/VPNs such as XLink Kai.
* **UPnP** - Automatic port forwarding on supported routers.
* **Friends & profiles** - Friends lists, gamerpics and Discord rich presence.
* **XStorage** - Title storage content served from the backend, with a local fallback.
* **Update checker** - Notifies you when a new netplay build is available.
* **Windows & Linux** builds.

## Builds

* [Latest release](https://github.com/AdrianCassar/xenia-canary/releases/latest)
* [Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay) - Netplay with mouse and keyboard support for a limited number of games.
