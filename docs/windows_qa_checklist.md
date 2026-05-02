# Windows QA Checklist (BlockForge)

## Prerequisites

- Windows 10/11 x64
- GPU drivers installed
- Java:
  - Java 17 for Minecraft 1.20.1
  - Java 21 for Minecraft 1.21.x
- Microsoft App Registration client id (optional, for online mode)

## Smoke: UI start

- Launch `BlockForge.exe`
- Create a vanilla instance (e.g. `1.20.1`)
- Set `Java path` to `javaw.exe` (or keep `java` if it is on PATH)
- Set `Max RAM (MB)`

## Smoke: vanilla offline

- Ensure no account is selected (or logout)
- Click `Play (offline)`
- Expect:
  - preparation steps in log
  - game starts to title screen

## Smoke: Microsoft login + online

- Paste Microsoft client id
- Click `Add account`
- Follow device code flow
- Select the account in `Account` dropdown
- Click `Play (online)`
- Expect:
  - token refresh is automatic when expired
  - online session works (no auth errors in log)

## Smoke: Fabric

- Create instance with `fabric` loader (same MC version)
- Click `Play`
- Expect:
  - fabric version is installed automatically
  - game starts and Fabric appears in logs

## Smoke: Forge

- Create instance with `forge` loader (same MC version)
- Click `Play`
- Expect:
  - forge installer is cached under `installers/`
  - processors run and produce patched artifacts
  - game starts and Forge appears in logs

## Mods UI

- Put a `*.jar` mod into `<dataDir>/instances/<id>/game/mods`
- Click `Refresh`
- Toggle mod checkbox:
  - enabled: `*.jar`
  - disabled: `*.jar.disabled`
- Click `Open folder` to open mods directory

## Import / Export

- `Export` an instance to a folder
- `Import` the exported instance folder
- Expect:
  - new instance id
  - metadata preserved (name/version/loader)

## Installer (NSIS)

- Install using `BlockForge-Setup-*.exe`
- Verify:
  - shortcuts created
  - appears in Windows “Apps & features”
- Uninstall
- Verify:
  - shortcuts removed
  - install dir removed

