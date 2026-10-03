<#
  build_3dsx.ps1 - DungeonRush 3DS build for Windows.
  Put this file and build_3dsx.bat in your port's root folder and run the .bat.
  It installs devkitPro + the 3DS SDL2 libraries if they are missing, then builds
  and drops the result in .\dist\  (both a .3dsx and a .cia).
  Flags:
  -Clean (wipe build output first)
  -RegenMakefile (rewrite Makefile.3ds)
  -RebuildLibs (rebuild the SDL2 libraries for 3DS)
  -UseGeneratedMakefile (back up your Makefile.3ds and use the auto-generated one)
  -SkipCia (only produce the .3dsx, do not build a .cia)
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$RegenMakefile,
    [switch]$RebuildLibs,
    [switch]$UseGeneratedMakefile,
    [switch]$SkipCia
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # makes downloads much faster in PS 5.1

# ------------------------------ settings you may edit ------------------------------
$AppName   = 'DungeonRush'                 # output file name (no spaces)
$AppTitle  = 'DungeonRush'                 # shown in the Homebrew Launcher / Home Menu
$AppDesc   = 'DungeonRush 3DS port'
$AppAuthor = 'PurpleStingray'

# --- CIA settings -------------------------------------------------------------
# These become the title shown on the 3DS Home Menu. Change $TitleId/$UniqueId if
# you want a truly unique install (last 5 hex digits of $TitleId must equal $UniqueId).
$TitleId     = '0x0004000008db5a00'
$UniqueId    = '0x8db5a'
$ProductCode = 'CTR-P-' + ((($AppName.ToUpper() -replace '[^A-Z0-9]','') + 'XXXX').Substring(0,4))
# Expected assets in res\ (used for the .cia's icon + banner):
#   res\icon.png   - 48x48 (or 24x24) PNG
#   res\banner.png - 256x128 PNG
#   res\banner.wav - optional short WAV for the banner sound
# ------------------------------------------------------------------------------

# If there is no romfs\ folder but there IS a res\ folder, copy res\ into romfs\res
# on every build so assets get packed inside the .3dsx (load them as "romfs:/res/...").
$AutoRomfsFromRes = $true
# Source files left out of the build (LAN multiplayer needs SDL2_net, which the 3DS doesn't have).
# Paths are relative to this folder, forward slashes.
$ExcludeSources = @('src/net.c')
# Some files still `#include <SDL_net.h>`. When $true (and no real SDL_net.h exists) the script
# writes a harmless stand-in into 3ds_shims\ (types + stub functions that always report failure).
$ShimSdlNet = $true
# Stand-ins for the symbols that lived in the excluded net.c (the linker asked for these).
# Functions become `int name() { return 0; }`, variables become `void *name = 0;`.
# If the linker reports more undefined references to net.c things, add them here.
$NetStubFunctions = @('sendPlayerMovePacket','recvLanPacket','sendGameOverPacket','hostGame','joinGame')
$NetStubVariables = @('lanClientSocket')
# Route stderr / SDL_Log to the emulator log (and 3dslink) so you can see what the game prints.
# In Azahar set Emulation > Configure > Debug > Global Log Filter to *:Debug. Set to $false for a release build.
$DebugLog = $true
# -----------------------------------------------------------------------------------

$Project = $PSScriptRoot
if (-not $Project) { $Project = (Get-Location).Path }
Set-Location $Project

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }

Start-Transcript -Path (Join-Path $Project 'build.log') -Force | Out-Null
$failed = $false
try {
    # ---------- sanity checks ----------
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'devkitPro needs 64-bit Windows.' }
    if ($Project -match '\s') {
        throw "The project path contains spaces:`n  $Project`nMove the folder somewhere like C:\dev\DungeonRush and try again (make cannot handle spaces)."
    }
    $anyC = Get-ChildItem -Path $Project -Recurse -Filter *.c -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\(build|dist|romfs|\.git)\\' } | Select-Object -First 1
    if (-not $anyC) { throw "No .c files found under $Project - put this script in the root of your port." }

    # ---------- devkitPro ----------
    $Dkp = 'C:\devkitPro'
    if ($env:DEVKITPRO -and ($env:DEVKITPRO -match '^[A-Za-z]:\\') -and (Test-Path $env:DEVKITPRO)) {
        $Dkp = $env:DEVKITPRO.TrimEnd('\')
    }
    $Bash = Join-Path $Dkp 'msys2\usr\bin\bash.exe'

    if (-not (Test-Path $Bash)) {
        Step 'devkitPro not found - downloading the installer'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $installer = Join-Path $env:TEMP 'devkitProUpdater.exe'
        try {
            $rel = Invoke-RestMethod 'https://api.github.com/repos/devkitPro/installer/releases/latest' `
                                     -Headers @{ 'User-Agent' = 'dungeonrush-3ds-build' }
            $asset = $rel.assets | Where-Object { $_.name -like '*.exe' } | Select-Object -First 1
            if (-not $asset) { throw 'no .exe asset in latest release' }
            Invoke-WebRequest $asset.browser_download_url -OutFile $installer -UseBasicParsing
        } catch {
            Write-Host "Could not download automatically ($($_.Exception.Message))." -ForegroundColor Yellow
            Write-Host 'Opening the download page - install devkitPro manually, then re-run this script.'
            Start-Process 'https://github.com/devkitPro/installer/releases/latest'
            throw 'devkitPro is not installed yet.'
        }
        Step 'Running the devkitPro installer (accept the UAC prompt)'
        Write-Host 'Keep the default install folder (C:\devkitPro) and tick "Nintendo 3DS" if it is offered.'
        Write-Host 'The installer downloads a few hundred MB; wait for it to finish, then this script continues.'
        Start-Process -FilePath $installer -Wait
        if (-not (Test-Path $Bash)) { throw "Installer finished but $Bash is missing. Re-run this script once the install completes." }
    }

    # Runs a bash snippet inside devkitPro's MSYS2. The snippet is written to a temp .sh file
    # (avoids Windows quoting problems). Returns the exit code.
    function Run-Msys([string]$Body, [string]$Dir = $Project, [switch]$Quiet) {
        $sh = Join-Path $Project '.msys_cmd.sh'
        $lines = @('set -e',
                   'exec 2>&1',      # merge stderr so compiler errors land in build.log
                   'export DEVKITPRO=/opt/devkitpro',
                   'export DEVKITARM=$DEVKITPRO/devkitARM',
                   'export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$DEVKITPRO/portlibs/3ds/bin:$PATH"',
                   "cd `"`$(cygpath -u '$Dir')`"",
                   $Body)
        $text = (($lines -join "`n") + "`n") -replace "`r", ''      # bash needs LF endings
        [IO.File]::WriteAllText($sh, $text, (New-Object Text.UTF8Encoding $false))
        $env:CHERE_INVOKING = '1'      # keep the current directory in the login shell
        $env:MSYSTEM = 'MSYS'
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        try {
            if ($Quiet) { & $Bash -l ($sh -replace '\\', '/') *> $null }
            else        { & $Bash -l ($sh -replace '\\', '/') | Out-Host }
            $code = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $old
            Remove-Item $sh -Force -ErrorAction SilentlyContinue
        }
        return $code
    }
    function Invoke-Msys([string]$Body, [string]$Dir = $Project) {
        $code = Run-Msys -Body $Body -Dir $Dir
        if ($code -ne 0) { throw "Command failed (exit $code): $Body" }
    }
    function Install-Pkgs([string[]]$Names) {
        $ok = @(); $bad = @()
        foreach ($p in $Names) {
            if ((Run-Msys -Body "pacman -Si $p" -Quiet) -eq 0) { $ok += $p } else { $bad += $p }
        }
        if ($ok.Count  -gt 0) { Invoke-Msys "pacman -S --needed --noconfirm $($ok -join ' ')" }
        if ($bad.Count -gt 0) { Write-Host "Not in the devkitPro repo (skipped): $($bad -join ', ')" -ForegroundColor Yellow }
    }
    function Get-Sdl2Tag([string]$Repo, [string]$Fallback) {
        try {
            $refs = Invoke-RestMethod "https://api.github.com/repos/libsdl-org/$Repo/git/matching-refs/tags/release-2." `
                                      -Headers @{ 'User-Agent' = 'dungeonrush-3ds-build' }
            $best = $null
            foreach ($r in $refs) {
                if ($r.ref -match '^refs/tags/release-(2\.\d+\.\d+)$') {
                    $v = [version]$Matches[1]
                    if (-not $best -or $v -gt $best) { $best = $v }
                }
            }
            if ($best) { return "release-$best" }
        } catch { }
        return $Fallback
    }
    # Downloads a Windows .exe out of a GitHub release .zip and drops it in devkitPro's tools\bin
    # (which is already on the MSYS PATH). Used for makerom + bannertool, which are not in pacman.
    function Install-WindowsTool([string]$Name, [string]$Url, [string]$ExeName, [string]$DestDir) {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $zip     = Join-Path $env:TEMP "$Name.zip"
        $extract = Join-Path $env:TEMP "$Name-extract"
        if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
        Write-Host "Downloading $Name from $Url ..."
        Invoke-WebRequest $Url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        $exe = Get-ChildItem -Path $extract -Recurse -Filter $ExeName -File | Select-Object -First 1
        if (-not $exe) { throw "$ExeName not found inside $Name.zip" }
        New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
        Copy-Item $exe.FullName (Join-Path $DestDir $ExeName) -Force
        Remove-Item $zip, $extract -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Installed $ExeName to $DestDir" -ForegroundColor DarkGray
    }

    # ---------- 3DS toolchain ----------
    $toolchainMissing = @("$Dkp\libctru\lib\libctru.a",
                          "$Dkp\portlibs\3ds\bin\arm-none-eabi-pkg-config") | Where-Object { -not (Test-Path $_) }
    if ($toolchainMissing.Count -gt 0) {
        Step 'Installing the 3DS toolchain (pacman)'
        Invoke-Msys 'pacman -Syu --noconfirm'
        Invoke-Msys 'pacman -S --needed --noconfirm 3ds-dev'
        Install-Pkgs @('3ds-pkg-config')
    } else {
        Write-Host '3DS toolchain already installed.' -ForegroundColor DarkGray
    }

    # ---------- SDL2 for 3DS ----------
    # devkitPro only ships SDL 1.2 for the 3DS. SDL2 has an official 3DS backend, so we
    # build SDL2 + image/mixer/ttf from the libsdl-org release tags, once, into portlibs.
    $sdlFiles = 'libSDL2.a','libSDL2main.a','libSDL2_image.a','libSDL2_mixer.a','libSDL2_ttf.a'
    $sdlMissing = @($sdlFiles | Where-Object { -not (Test-Path "$Dkp\portlibs\3ds\lib\$_") })
    if ($RebuildLibs) { $sdlMissing = $sdlFiles }
    if ($sdlMissing.Count -gt 0) {
        Step 'Building SDL2 for the 3DS from source (one time, takes a few minutes)'
        Install-Pkgs @('3ds-cmake','cmake','make','3ds-zlib','3ds-libpng','3ds-freetype')
        if (-not (Test-Path "$Dkp\cmake\3DS.cmake")) { throw "$Dkp\cmake\3DS.cmake is missing (package 3ds-cmake did not install)." }

        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $work = Join-Path $Dkp 'sdl2-build'
        if (Test-Path $work) { Remove-Item $work -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $work | Out-Null

        $libs = @(
            @{ Repo = 'SDL';       Fallback = 'release-2.32.10'
               Args = '-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_INSTALL_TESTS=OFF' },
            @{ Repo = 'SDL_image'; Fallback = 'release-2.8.8'
               Args = '-DBUILD_SHARED_LIBS=OFF -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_JXL=OFF' },
            @{ Repo = 'SDL_mixer'; Fallback = 'release-2.8.1'
               Args = '-DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_DEPS_SHARED=OFF -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_CMD=OFF -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_GME=OFF -DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_MP3_MPG123=OFF -DSDL2MIXER_VORBIS=STB' },
            @{ Repo = 'SDL_ttf';   Fallback = 'release-2.24.0'
               Args = '-DBUILD_SHARED_LIBS=OFF -DSDL2TTF_VENDORED=OFF -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_PLUTOSVG=OFF' }
        )
        $tpl = @'
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEVKITPRO/portlibs/3ds" -DCMAKE_PREFIX_PATH="$DEVKITPRO/portlibs/3ds" @@ARGS@@
cmake --build build -j"$(nproc)"
cmake --install build
'@
        foreach ($l in $libs) {
            $tag = Get-Sdl2Tag $l.Repo $l.Fallback
            Step "$($l.Repo) ($tag)"
            $zip = Join-Path $work "$($l.Repo).zip"
            Invoke-WebRequest "https://github.com/libsdl-org/$($l.Repo)/archive/refs/tags/$tag.zip" -OutFile $zip -UseBasicParsing
            Expand-Archive -Path $zip -DestinationPath $work -Force
            $src = Get-ChildItem -Path $work -Directory | Where-Object { $_.Name -like "$($l.Repo)-*" } | Select-Object -First 1
            if (-not $src) { throw "Could not find extracted sources for $($l.Repo)." }
            Invoke-Msys ($tpl.Replace('@@ARGS@@', $l.Args)) $src.FullName
        }
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host 'SDL2 libraries for 3DS already installed.' -ForegroundColor DarkGray
    }

    # ---------- assets -> romfs ----------
    $romfsMarker = Join-Path $Project 'romfs\.autogen'
    if ($AutoRomfsFromRes -and (Test-Path (Join-Path $Project 'res')) -and
        ((-not (Test-Path (Join-Path $Project 'romfs'))) -or (Test-Path $romfsMarker))) {
        Step 'Packing res\ into romfs\res'
        New-Item -ItemType Directory -Force -Path (Join-Path $Project 'romfs') | Out-Null
        robocopy (Join-Path $Project 'res') (Join-Path $Project 'romfs\res') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
        Set-Content -Path $romfsMarker -Value 'created by build_3dsx.ps1 - delete this file to manage romfs yourself'
        $global:LASTEXITCODE = 0
    }

    # ---------- SDL2_net stand-in ----------
    $shimDir = Join-Path $Project '3ds_shims'
    $realNet = Get-ChildItem -Path $Project -Recurse -Filter SDL_net.h -File -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch '\\3ds_shims\\' } | Select-Object -First 1
    if ($ShimSdlNet -and -not $realNet) {
        Step 'Writing SDL_net stand-in (3ds_shims\)'
        New-Item -ItemType Directory -Force -Path $shimDir | Out-Null
        $hdr = @'
/* AUTO-GENERATED by build_3dsx.ps1 - stand-in for SDL2_net, which does not exist on the 3DS.
   Types and prototypes only, so code that includes <SDL_net.h> still compiles. */
#ifndef DR3DS_SDL_NET_SHIM_H
#define DR3DS_SDL_NET_SHIM_H
#include <SDL.h>
#define DR_NO_NET 1
#define SDLNET_MAX_UDPCHANNELS 32
typedef struct { Uint32 host; Uint16 port; } IPaddress;
typedef struct _TCPsocket *TCPsocket;
typedef struct _UDPsocket *UDPsocket;
typedef struct _SDLNet_SocketSet *SDLNet_SocketSet;
typedef struct _SDLNet_GenericSocket *SDLNet_GenericSocket;
typedef struct { int channel; Uint8 *data; int len; int maxlen; int status; IPaddress address; } UDPpacket;
int SDLNet_Init(void);
void SDLNet_Quit(void);
const char *SDLNet_GetError(void);
int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port);
const char *SDLNet_ResolveIP(const IPaddress *ip);
TCPsocket SDLNet_TCP_Open(IPaddress *ip);
TCPsocket SDLNet_TCP_Accept(TCPsocket server);
IPaddress *SDLNet_TCP_GetPeerAddress(TCPsocket sock);
int SDLNet_TCP_Send(TCPsocket sock, const void *data, int len);
int SDLNet_TCP_Recv(TCPsocket sock, void *data, int maxlen);
void SDLNet_TCP_Close(TCPsocket sock);
SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets);
void SDLNet_FreeSocketSet(SDLNet_SocketSet set);
int SDLNet_AddSocket(SDLNet_SocketSet set, SDLNet_GenericSocket sock);
int SDLNet_DelSocket(SDLNet_SocketSet set, SDLNet_GenericSocket sock);
int SDLNet_CheckSockets(SDLNet_SocketSet set, Uint32 timeout);
int SDLNet_SocketReady(void *sock);
#define SDLNet_TCP_AddSocket(set, sock) SDLNet_AddSocket(set, (SDLNet_GenericSocket)(sock))
#define SDLNet_TCP_DelSocket(set, sock) SDLNet_DelSocket(set, (SDLNet_GenericSocket)(sock))
#define SDLNet_UDP_AddSocket(set, sock) SDLNet_AddSocket(set, (SDLNet_GenericSocket)(sock))
#define SDLNet_UDP_DelSocket(set, sock) SDLNet_DelSocket(set, (SDLNet_GenericSocket)(sock))
#define SDLNet_SocketReady(sock) SDLNet_SocketReady((void *)(sock))
UDPsocket SDLNet_UDP_Open(Uint16 port);
void SDLNet_UDP_Close(UDPsocket sock);
int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet);
int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet);
UDPpacket *SDLNet_AllocPacket(int size);
void SDLNet_FreePacket(UDPpacket *packet);
static inline void SDLNet_Write16(Uint16 value, void *area) { Uint8 *p = (Uint8 *)area; p[0] = (Uint8)(value >> 8); p[1] = (Uint8)value; }
static inline void SDLNet_Write32(Uint32 value, void *area) { Uint8 *p = (Uint8 *)area; p[0] = (Uint8)(value >> 24); p[1] = (Uint8)(value >> 16); p[2] = (Uint8)(value >> 8); p[3] = (Uint8)value; }
static inline Uint16 SDLNet_Read16(const void *area) { const Uint8 *p = (const Uint8 *)area; return (Uint16)((p[0] << 8) | p[1]); }
static inline Uint32 SDLNet_Read32(const void *area) { const Uint8 *p = (const Uint8 *)area; return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | (Uint32)p[3]; }
#endif
'@
        $stub = @'
/* AUTO-GENERATED by build_3dsx.ps1 - every network call fails cleanly (Init "succeeds" so startup code keeps going). */
#include <SDL_net.h>
#include <stddef.h>
#undef SDLNet_SocketReady
int SDLNet_Init(void) { return 0; }
void SDLNet_Quit(void) {}
const char *SDLNet_GetError(void) { return "SDL2_net is not available on the 3DS"; }
int SDLNet_ResolveHost(IPaddress *a, const char *h, Uint16 p) { (void)a; (void)h; (void)p; return -1; }
const char *SDLNet_ResolveIP(const IPaddress *ip) { (void)ip; return ""; }
TCPsocket SDLNet_TCP_Open(IPaddress *ip) { (void)ip; return NULL; }
TCPsocket SDLNet_TCP_Accept(TCPsocket s) { (void)s; return NULL; }
IPaddress *SDLNet_TCP_GetPeerAddress(TCPsocket s) { (void)s; return NULL; }
int SDLNet_TCP_Send(TCPsocket s, const void *d, int n) { (void)s; (void)d; (void)n; return -1; }
int SDLNet_TCP_Recv(TCPsocket s, void *d, int n) { (void)s; (void)d; (void)n; return -1; }
void SDLNet_TCP_Close(TCPsocket s) { (void)s; }
SDLNet_SocketSet SDLNet_AllocSocketSet(int n) { (void)n; return NULL; }
void SDLNet_FreeSocketSet(SDLNet_SocketSet s) { (void)s; }
int SDLNet_AddSocket(SDLNet_SocketSet s, SDLNet_GenericSocket k) { (void)s; (void)k; return -1; }
int SDLNet_DelSocket(SDLNet_SocketSet s, SDLNet_GenericSocket k) { (void)s; (void)k; return -1; }
int SDLNet_CheckSockets(SDLNet_SocketSet s, Uint32 t) { (void)s; (void)t; return -1; }
int SDLNet_SocketReady(void *k) { (void)k; return 0; }
UDPsocket SDLNet_UDP_Open(Uint16 p) { (void)p; return NULL; }
void SDLNet_UDP_Close(UDPsocket s) { (void)s; }
int SDLNet_UDP_Send(UDPsocket s, int c, UDPpacket *p) { (void)s; (void)c; (void)p; return 0; }
int SDLNet_UDP_Recv(UDPsocket s, UDPpacket *p) { (void)s; (void)p; return -1; }
UDPpacket *SDLNet_AllocPacket(int n) { (void)n; return NULL; }
void SDLNet_FreePacket(UDPpacket *p) { (void)p; }
'@
        $enc = New-Object Text.UTF8Encoding $false
        [IO.File]::WriteAllText((Join-Path $shimDir 'SDL_net.h'),       (($hdr  -replace "`r`n", "`n")), $enc)
        [IO.File]::WriteAllText((Join-Path $shimDir 'sdlnet_stub.c'),   (($stub -replace "`r`n", "`n")), $enc)
    } elseif (Test-Path $shimDir) {
        Remove-Item (Join-Path $shimDir 'SDL_net.h'), (Join-Path $shimDir 'sdlnet_stub.c') -Force -ErrorAction SilentlyContinue
    }

    # ---------- stand-ins for the excluded net.c ----------
    $netStub = Join-Path $shimDir 'net_stub.c'
    if (($NetStubFunctions.Count + $NetStubVariables.Count) -gt 0) {
        New-Item -ItemType Directory -Force -Path $shimDir | Out-Null
        $fn = ($NetStubFunctions | ForEach-Object { "int $_() { return 0; }" }) -join "`n"
        $vr = ($NetStubVariables | ForEach-Object { "void *$_ = 0;" }) -join "`n"
        $txt = "/* AUTO-GENERATED by build_3dsx.ps1 - LAN multiplayer is not available on the 3DS. */`n$fn`n$vr`n"
        [IO.File]::WriteAllText($netStub, $txt, (New-Object Text.UTF8Encoding $false))
    } elseif (Test-Path $netStub) {
        Remove-Item $netStub -Force
    }

    # ---------- debug output (stderr / SDL_Log -> svcOutputDebugString) ----------
    $dbgFile = Join-Path $shimDir 'debug_log.c'
    if ($DebugLog) {
        New-Item -ItemType Directory -Force -Path $shimDir | Out-Null
        $dbg = @'
/* AUTO-GENERATED by build_3dsx.ps1 - sends stderr and SDL_Log output to the emulator/debug log. */
#include <3ds.h>
#include <stdio.h>
#include <SDL.h>
__attribute__((constructor)) static void dr_debug_init(void)
{
    consoleDebugInit(debugDevice_SVC);
    setvbuf(stderr, NULL, _IONBF, 0);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    fprintf(stderr, "[debug_log] started\n");
}
'@
        [IO.File]::WriteAllText($dbgFile, ($dbg -replace "`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
    } elseif (Test-Path $dbgFile) {
        Remove-Item $dbgFile -Force
    }

    # ---------- pick a Makefile ----------
    $Makefile = $null
    if ($UseGeneratedMakefile -and (Test-Path (Join-Path $Project 'Makefile.3ds')) -and
        -not (Select-String -Path (Join-Path $Project 'Makefile.3ds') -SimpleMatch '# AUTO-GENERATED by build_3dsx.ps1' -Quiet)) {
        $bak = "Makefile.3ds.$(Get-Date -Format yyyyMMddHHmmss).bak"
        Move-Item (Join-Path $Project 'Makefile.3ds') (Join-Path $Project $bak)
        Write-Host "Backed up your Makefile.3ds to $bak"
    }
    $genMarker = '# AUTO-GENERATED by build_3dsx.ps1'
    $custom = Join-Path $Project 'Makefile.3ds'
    $main   = Join-Path $Project 'Makefile'
    if ((Test-Path $custom) -and -not (Select-String -Path $custom -SimpleMatch $genMarker -Quiet)) {
        $Makefile = 'Makefile.3ds'
        Write-Host 'Using your own Makefile.3ds.'
    } elseif (-not $UseGeneratedMakefile -and (Test-Path $main) -and (Select-String -Path $main -SimpleMatch 'DEVKITARM' -Quiet)) {
        $Makefile = 'Makefile'
        Write-Host 'Using your existing devkitARM Makefile.'
    } else {
        $Makefile = 'Makefile.3ds'
        if ($RegenMakefile -or -not (Test-Path $custom) -or (Select-String -Path $custom -SimpleMatch $genMarker -Quiet)) {
            Step 'Generating Makefile.3ds'
            $mk = @'
# AUTO-GENERATED by build_3dsx.ps1 -- delete this first line to keep your own edits
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set - run this through build_3dsx.bat)
endif
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set - run this through build_3dsx.bat)
endif

TARGET      := @@NAME@@
APP_TITLE   := @@TITLE@@
APP_DESC    := @@DESC@@
APP_AUTHOR  := @@AUTHOR@@
ICON        ?= $(if $(wildcard res/icon.png),res/icon.png,$(DEVKITPRO)/libctru/default_icon.png)
ROMFS       := romfs
BUILD       := build
SHIMDIR     := 3ds_shims
OUTDIR      := dist

LIBCTRU     := $(DEVKITPRO)/libctru
PORTLIBS    := $(DEVKITPRO)/portlibs/3ds
PKGCONF     := $(PORTLIBS)/bin/arm-none-eabi-pkg-config

ifeq ($(wildcard $(PKGCONF)),)
$(error 3ds-pkg-config is missing - run build_3dsx.bat again to install it)
endif

CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
export PATH := $(DEVKITPRO)/tools/bin:$(PORTLIBS)/bin:$(DEVKITARM)/bin:$(PATH)

EXCLUDE := -not -path './$(SHIMDIR)/*' -not -path './$(BUILD)/*' -not -path './$(OUTDIR)/*' -not -path './$(ROMFS)/*' -not -path './.git/*'
EXCLUDE_SRC := @@EXCLUDE@@
SRC     := $(filter-out $(EXCLUDE_SRC),$(patsubst ./%,%,$(shell find . -name '*.c' $(EXCLUDE))))
SRC     += $(wildcard $(SHIMDIR)/*.c)
OBJ     := $(SRC:%.c=$(BUILD)/%.o)
INCDIRS := . include $(patsubst %/,%,$(sort $(dir $(shell find . -name '*.h' $(EXCLUDE)))))
INCLUDE := $(addprefix -I,$(INCDIRS))

SDLPKGS := sdl2 SDL2_image SDL2_mixer SDL2_ttf
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS= -g -Wall -O3 -mword-relocations -ffunction-sections -fdata-sections \
-fomit-frame-pointer -std=gnu11 -march=armv6k -mtune=mpcore \
-mfloat-abi=hard -mtp=soft -D__3DS__ \
-I. -Iinclude -I./src -I./src/platform/3ds \
-idirafter 3ds_shims \
-I/opt/devkitpro/libctru/include \
-I/opt/devkitpro/portlibs/3ds/include \
-I/opt/devkitpro/portlibs/3ds/include/SDL2 \
-I/opt/devkitpro/portlibs/3ds/include/freetype2 \
-I/opt/devkitpro/portlibs/3ds/include/libpng16 \
-fno-math-errno -fno-trapping-math -ffast-math
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections
LIBDIRS := -L$(LIBCTRU)/lib -L$(PORTLIBS)/lib
LIBS    := -lSDL2main $(shell $(PKGCONF) --libs --static $(SDLPKGS)) -lcitro2d -lcitro3d -lctru -lm
ROMFS_FLAG := $(if $(wildcard $(ROMFS)),--romfs=$(ROMFS),)

.PHONY: all clean
all: $(OUTDIR)/$(TARGET).3dsx

$(BUILD)/%.o: %.c
@@mkdir -p $(dir $@)
@@echo CC  $<
@@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(OUTDIR)/$(TARGET).elf: $(OBJ)
@@mkdir -p $(OUTDIR)
@@echo LD  $@
@@$(CC) $(LDFLAGS) $(OBJ) $(LIBDIRS) $(LIBS) -o $@

$(OUTDIR)/$(TARGET).smdh:
@@mkdir -p $(OUTDIR)
@@smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(OUTDIR)/$(TARGET).3dsx: $(OUTDIR)/$(TARGET).elf $(OUTDIR)/$(TARGET).smdh
@@3dsxtool $< $@ --smdh=$(OUTDIR)/$(TARGET).smdh $(ROMFS_FLAG)
@@echo Built $@

clean:
@@rm -rf $(BUILD) $(OUTDIR)

-include $(OBJ:.o=.d)
'@
            $mk = $mk.Replace('@@NAME@@', $AppName).Replace('@@TITLE@@', $AppTitle).
                      Replace('@@DESC@@', $AppDesc).Replace('@@AUTHOR@@', $AppAuthor).
                      Replace('@@EXCLUDE@@', ($ExcludeSources -join ' '))
            $mk = ($mk -replace "`r`n", "`n") -replace '(?m)^@@', "`t"
            [IO.File]::WriteAllText($custom, $mk, (New-Object Text.UTF8Encoding $false))
        }
    }

    # ---------- build ----------
    if ($Clean) { Step 'Cleaning'; Invoke-Msys "make -f $Makefile clean" }
    Step 'Building'
    $code = Run-Msys -Body "make -f $Makefile -j`$(nproc) -Otarget"
    if ($code -ne 0) {
        Write-Host "`nParallel build failed - re-running serially to get a readable error..." -ForegroundColor Yellow
        Invoke-Msys "make -f $Makefile"
    }

    # ---------- collect output ----------
    $out = Get-ChildItem -Path $Project -Recurse -Filter *.3dsx -File |
           Where-Object { $_.FullName -notmatch '\\romfs\\' } |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $out) { throw 'Build finished but no .3dsx was produced.' }
    $dist = Join-Path $Project 'dist'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    if ($out.DirectoryName -ne $dist) { Copy-Item $out.FullName $dist -Force; $out = Get-Item (Join-Path $dist $out.Name) }

    Write-Host "`nSUCCESS: $($out.FullName)  ($([math]::Round($out.Length/1KB)) KB)" -ForegroundColor Green

    # ---------- CIA ----------
    $ciaOut = Join-Path $dist "$AppName.cia"
    if (-not $SkipCia) {
        Step 'Building CIA'

        # makerom and bannertool are NOT in the devkitPro pacman repo. They are standalone
        # Windows binaries hosted on GitHub. Download them into devkitPro's tools\bin,
        # which is already on the MSYS PATH (see Run-Msys above).
        $toolsDir = Join-Path $Dkp 'tools\bin'
        New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

        if ((Run-Msys -Body 'command -v makerom' -Quiet) -ne 0) {
            Step 'Installing makerom (download from GitHub)'
            Install-WindowsTool -Name 'makerom' `
                -Url 'https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-win_x86_64.zip' `
                -ExeName 'makerom.exe' -DestDir $toolsDir
        } else {
            Write-Host 'makerom already installed.' -ForegroundColor DarkGray
        }
        if ((Run-Msys -Body 'command -v bannertool' -Quiet) -ne 0) {
            Step 'Installing bannertool (download from GitHub)'
            Install-WindowsTool -Name 'bannertool' `
                -Url 'https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip' `
                -ExeName 'bannertool.exe' -DestDir $toolsDir
        } else {
            Write-Host 'bannertool already installed.' -ForegroundColor DarkGray
        }
        if ((Run-Msys -Body 'command -v makerom'    -Quiet) -ne 0) { throw 'makerom is still not on PATH.' }
        if ((Run-Msys -Body 'command -v bannertool' -Quiet) -ne 0) { throw 'bannertool is still not on PATH.' }

        # Assets. icon/banner are expected under res\.
        $iconPng   = Join-Path $Project 'res\icon.png'
        $bannerPng = Join-Path $Project 'res\banner.png'
        $bannerWav = Join-Path $Project 'res\banner.wav'
        if (-not (Test-Path $iconPng))   { throw "res\icon.png not found - needed for the CIA (48x48 or 24x24 PNG)." }
        if (-not (Test-Path $bannerPng)) { throw "res\banner.png not found - needed for the CIA (256x128 PNG)." }

        # Complete RSF describing the title. makerom 0.18.4 requires a full
        # AccessControlInfo block (Priority, MemoryMapping, SystemCallAccess, etc.)
        # and a matching SystemControlInfo block; otherwise it fails with
        # "[EXHEADER ERROR] Parameter Not Found: AccessControlInfo/Priority".
        # This template is based on a known-good homebrew RSF.
        $rsfPath = Join-Path $Project "$AppName.rsf"
        $rsf = @"
BasicInfo:
  Title                   : "$AppTitle"
  CompanyCode             : "00"
  ProductCode             : "$ProductCode"
  ContentType             : Application
  Logo                    : Homebrew

RomFs:
  RootPath                : ./romfs

TitleInfo:
  Category                : Application
  UniqueId                : $UniqueId

Option:
  UseOnSD                 : true
  FreeProductCode         : true
  MediaFootPadding        : false
  EnableCrypt             : false
  EnableCompress          : true

AccessControlInfo:
  CoreVersion             : 2
  DescVersion             : 2
  ReleaseKernelMajor      : "02"
  ReleaseKernelMinor      : "33"
  MemoryType              : Application
  SystemMode              : 64MB
  IdealProcessor          : 0
  AffinityMask            : 1
  Priority                : 16
  MaxCpu                  : 0
  HandleTableSize         : 0x200
  DisableDebug            : false
  EnableForceDebug        : false
  CanWriteSharedPage      : false
  CanUsePrivilegedPriority : false
  CanUseNonAlphabetAndNumber : true
  PermitMainFunctionArgument : true
  CanShareDeviceMemory    : true
  RunnableOnSleep         : false
  SpecialMemoryArrange    : false
  SystemModeExt           : Legacy
  CpuSpeed                : 268MHz
  EnableL2Cache           : false
  CanAccessCore2          : false
  IORegisterMapping:
    - 1ff00000-1ff7ffff
  MemoryMapping:
    - 1f000000-1f5fffff:r
  SystemCallAccess:
    ArbitrateAddress: 34
    Break: 60
    CancelTimer: 28
    ClearEvent: 25
    ClearTimer: 29
    CloseHandle: 35
    ConnectToPort: 45
    ControlMemory: 1
    CreateAddressArbiter: 33
    CreateEvent: 23
    CreateMemoryBlock: 30
    CreateMutex: 19
    CreateSemaphore: 21
    CreateThread: 8
    CreateTimer: 26
    DuplicateHandle: 39
    ExitProcess: 3
    ExitThread: 9
    GetCurrentProcessorNumber: 17
    GetHandleInfo: 41
    GetProcessId: 53
    GetProcessIdOfThread: 54
    GetProcessIdealProcessor: 6
    GetProcessInfo: 43
    GetResourceLimit: 56
    GetResourceLimitCurrentValues: 58
    GetResourceLimitLimitValues: 57
    GetSystemInfo: 42
    GetSystemTick: 40
    GetThreadContext: 59
    GetThreadId: 55
    GetThreadIdealProcessor: 15
    GetThreadInfo: 44
    GetThreadPriority: 11
    MapMemoryBlock: 31
    OutputDebugString: 61
    QueryMemory: 2
    ReleaseMutex: 20
    ReleaseSemaphore: 22
    SendSyncRequest1: 46
    SendSyncRequest2: 47
    SendSyncRequest3: 48
    SendSyncRequest4: 49
    SendSyncRequest: 50
    SetThreadPriority: 12
    SetTimer: 27
    SignalEvent: 24
    SleepThread: 10
    UnmapMemoryBlock: 32
    WaitSynchronization1: 36
    WaitSynchronizationN: 37
    Backdoor: 123
  ServiceAccessControl:
    - cfg:u
    - fs:USER
    - gsp::Gpu
    - hid:USER
    - ndm:u
    - pxi:dev
    - APT:U
    - ac:u
    - act:u
    - am:net
    - boss:U
    - cam:u
    - cecd:u
    - csnd:SND
    - frd:u
    - http:C
    - ir:USER
    - ir:u
    - ir:rst
    - ldr:ro
    - mic:u
    - news:u
    - nfc:u
    - nim:aoc
    - nwm::UDS
    - ptm:u
    - qtm:u
    - soc:U
    - ssl:C
    - y2r:u

SystemControlInfo:
  SaveDataSize          : 0KB
  RemasterVersion       : 0
  StackSize             : 0x40000
"@
        [IO.File]::WriteAllText($rsfPath, ($rsf -replace "`r`n", "`n"), (New-Object Text.UTF8Encoding $false))

        # 1) Banner: bannertool makebanner -i banner.png [-a banner.wav] -o out.bnr
        $bannerArgs = if (Test-Path $bannerWav) { '-i res/banner.png -a res/banner.wav' } else { '-i res/banner.png' }
        Step 'Creating banner (bannertool)'
        Invoke-Msys "bannertool makebanner $bannerArgs -o dist/$AppName.bnr"

        # 2) SMDH with the user's icon (the Makefile already produced one with res/icon.png
        # if it exists, but regenerating here guarantees it matches for the CIA even for
        # custom Makefile.3ds users).
        Step 'Creating SMDH (smdhtool)'
        Invoke-Msys "smdhtool --create `"$AppTitle`" `"$AppDesc`" `"$AppAuthor`" res/icon.png dist/$AppName.smdh"

        # 3) Wrap ELF + SMDH + BNR into a CIA.
        # NOTE 1: no trailing "+" at end of line - Windows PowerShell 5.1 does NOT continue
        #         expressions across lines with a trailing operator.
        # NOTE 2: "$($AppName).rsf" - using $AppName.rsf would try to read a property named
        #         "rsf" on the string; and 'x', $AppName + '.rsf' inside @( ) gets parsed
        #         as array-concat and splits the ".rsf" into its own element.
        $rsfName     = "$AppName.rsf"
        $ciaName     = "$AppName.cia"
        $elfName     = "$AppName.elf"
        $smdhName    = "$AppName.smdh"
        $bnrName     = "$AppName.bnr"
        $makeromArgs = @(
            'makerom',
            '-f', 'cia',
            '-target', 't',
            '-o', "dist/$ciaName",
            '-elf', "dist/$elfName",
            '-rsf', $rsfName,
            '-icon', "dist/$smdhName",
            '-banner', "dist/$bnrName",
            '-exefslogo'
        ) -join ' '
        Step 'Running makerom'
        Invoke-Msys $makeromArgs

        if (Test-Path $ciaOut) {
            Write-Host "SUCCESS: $ciaOut  ($([math]::Round((Get-Item $ciaOut).Length/1KB)) KB)" -ForegroundColor Green
        }
    } else {
        Write-Host 'Skipping CIA (-SkipCia).' -ForegroundColor DarkGray
    }

    Write-Host 'Copy the .3dsx to the SD card under /3ds/ and launch from the Homebrew Launcher,'
    Write-Host 'or install the .cia with FBI (or use it in an emulator).'
    Start-Process explorer.exe -ArgumentList $dist
}
catch {
    $failed = $true
    Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Full log: build.log in the project folder.'
}
finally {
    Stop-Transcript | Out-Null
}
if ($failed) { exit 1 }