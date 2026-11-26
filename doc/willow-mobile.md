# Willow Mobile

Use the iPhone as a touch controller and game screen for mGBA running on Windows.
The iPhone opens an HTTPS link such as
`https://desktop.example.ts.net:10000`. Tailscale issues and renews the
certificate, so there is nothing to install or trust on the phone. The game and
its saves stay on the PC. Audio plays on the PC.

## Pair an iPhone

1. Connect the Windows Tailscale app on the PC. Pairing on Windows also needs
   Node.js 20 or newer.
2. Double-click **Launch mGBA.cmd** as usual. Click **Pair iPhone** in the game
   library or choose **Tools → Pair iPhone** during gameplay.
3. Scan the QR code with the iPhone Camera and open the link in any browser.
4. Pick a game. The phone opens on your Willow game library, with the same
   games and cover art as the PC. Tapping a game starts it on the PC and opens
   the game screen and controls.

If a game is already running on the PC, it appears at the top under **Playing
on PC**. Tap **Continue** to control it. The phone never closes a running game,
because it may have unsaved progress. To switch games, save on the PC and
return to the game library there first.

Tap **Auto Play**, choose a model, and tap **Start Auto Play**. The picker
starts with the PC's **AI → Auto Play Model** setting. Your choice applies to
that run. Tap **Stop Auto Play** on the phone or PC to stop it. Auto Play waits
while the game is paused and keeps running on the PC if you return to **Games**
or close the mobile page.

Open **Settings → Game speed** to choose **1×**, **2×**, **4×**, **8×**, or **Maximum** for
manual play and Auto Play. **PC speed** follows Willow’s desktop settings.
Desktop fast-forward controls take priority when active. Willow returns to
**PC speed** when the phone disconnects or returns to **Games**.

You can close the pairing dialog after scanning. To turn off mobile access,
click **Stop** in **Pair iPhone** or close Willow. **Games** on the phone
releases control and returns to the game list. The game keeps running on the
PC. Starting mobile access again creates a
new pairing code. Anyone with the current link can pair, so keep the QR code
private.

## Video stream

**Settings → Video stream** defaults to **Auto (prefer NVIDIA HEVC)**. If the
browser supports HEVC decoding, Willow tries encoding HEVC (H.265) on the PC's
NVIDIA GPU with NVENC. It falls back to H.264 if either side cannot use HEVC.
For H.264, the PC tries NVENC, AMD AMF, Intel Quick Sync, and Windows Media
Foundation, then x264 on the CPU. Settings shows the active codec and encoder.
Frames are doubled with sharp pixel edges before encoding, so a GBA game
typically streams at 480×320.

You can also select **H.264** or **PNG** directly. Willow uses PNG stills at up
to 20 frames per second when the browser or PC cannot use video. To force a
PC encoder for testing, set `WILLOW_MOBILE_ENCODER` before launching Willow,
for example `WILLOW_MOBILE_ENCODER=libx264`.

## GPU rendering on the PC

Willow renders GBA games with mGBA's OpenGL renderer by default (**Settings →
Enhancements → Video renderer: OpenGL**). While an AI agent controls a game,
Willow switches to the software renderer and switches back when the agent
disconnects. Only the software renderer's frames line up exactly with the game
state an agent reads (`atomic:true`). Game Boy games always use the software
renderer. The bridge's `session` reply shows the active `renderer`.

## Connection modes

By default Willow publishes the link with
[Tailscale Funnel](https://tailscale.com/kb/1223/funnel). Funnel puts the PC's
`ts.net` name in public DNS. The phone can then open the link without Tailscale
DNS, and even without Tailscale. Willow runs `tailscale funnel` in the
foreground, so the public link exists only while pairing is on.

Funnel only allows ports 443, 8443 and 10000. Willow uses the first one that no
other Serve or Funnel configuration on the PC already uses. The first start
can take up to a minute while Tailscale publishes the name and fetches the
certificate. Willow waits for both before it shows the QR code.

The page is public while pairing is on, so access depends on the pairing code
in the link. The code is 192 random bits and lives in the URL fragment, which
browsers never send to the server. Only one phone can control a game at a time.
Connections that do not pair within five seconds are closed. The oldest
unpaired connection is closed whenever more than eight are waiting, so
strangers cannot block the phone from pairing.

Set `WILLOW_MOBILE_ACCESS` before launching Willow to choose another mode:

- `tailnet`: Tailscale Serve HTTPS on port 9444, reachable only from your
  tailnet. The iPhone must have **Use Tailscale DNS** turned on in the
  Tailscale app to resolve the `ts.net` name.
- `http`: plain `http://` on the PC's numeric Tailscale IP, port 9444. It needs
  no DNS or certificate. The browser shows **Not Secure**, though WireGuard
  still encrypts the traffic.

If Funnel isn't allowed for the PC, Willow uses `tailnet`. If HTTPS
certificates are also turned off for the tailnet, it uses `http`.

## Troubleshooting

- **The QR code never appears:** check Funnel and HTTPS for the PC on the
  Tailscale admin console's **DNS** and **Access controls** pages.
- **Port already in use:** set `WILLOW_MOBILE_PORT`. For Funnel it must be 443,
  8443 or 10000.
- **Page doesn't load in `tailnet` mode:** turn on **Use Tailscale DNS** on the
  iPhone, or use the default Funnel mode.
- **The phone can't see the game:** keep **AI → MCP Enabled** checked in
  Willow and open a single-player game. Resume a paused game on the PC.
