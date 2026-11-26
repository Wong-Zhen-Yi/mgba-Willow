# Willow Mobile

Use the iPhone as a touch controller and game screen for mGBA running on Windows or Linux.
The iPhone opens an HTTPS link such as
`https://desktop.example.ts.net:10000`. Tailscale issues and renews the
certificate, so there is nothing to install or trust on the phone. The game and
its saves stay on the PC. Audio plays on the PC.

## Pair an iPhone

1. Connect the Tailscale app on the PC. Pairing also needs Node.js 20 or newer.
   On Linux, let your user run Tailscale Serve and Funnel once with
   `sudo tailscale set --operator=$USER`.
2. Double-click **Launch mGBA.cmd** (Windows) or the **mGBA Willow** desktop
   icon (Linux, added by `./"Launch mGBA.sh" --install-desktop`) as usual. Willow starts serving the phone
   link in the background a few seconds after it opens. Click **Pair iPhone**
   in the game library or choose **Tools → Pair iPhone** during gameplay to
   see the QR code.

   Run `"Launch mGBA.cmd" --install-startup` to add or update **mGBA Willow**
   in Windows Startup apps. Willow opens minimized when you sign in and uses
   the last build. The next normal launch still rebuilds changed sources.
   `"Launch mGBA.cmd" --remove-startup` removes the startup entry. On Linux,
   `./"Launch mGBA.sh" --install-startup` and `--remove-startup` do the same
   through `~/.config/autostart`.

   The **mGBA Willow** system tray icon turns green and shows a notification
   when Willow Mobile is ready. Hover over the icon to check the server
   status, or right-click it to open Willow or pair an iPhone. Windows may
   place the icon in the hidden icons menu next to the clock.
3. Scan the QR code with the iPhone Camera and open the link in any browser.
   You can also click **Copy Link** under the QR code (or **Copy Pairing
   Link** in the tray menu) and send the link to the phone or another device
   in a message. The link includes the pairing code, so send it only to
   yourself.
4. Pick a game. The phone opens on your Willow game library, with the same
   games and cover art as the PC. Tapping a game starts it on the PC and opens
   the game screen and controls.

If a game is already running on the PC, it appears at the top under **Playing
on PC**. Tap **Continue** to control it. The phone never closes a running game,
because it may have unsaved progress. To switch games, save on the PC and
return to the game library there first.

Tap **Auto Play** to let Codex play. It uses the model chosen in
**Settings → Auto Play → Model** on the phone. **PC default** (the starting
choice) follows the PC's **AI → Auto Play Model** setting. Tap **Stop Auto Play**
on the phone or PC to stop it. Auto Play waits
while the game is paused and keeps running on the PC if you return to **Games**
or close the mobile page.

You can close the pairing dialog after scanning. Mobile access stays on while
Willow is open. To turn it off, click **Stop** in **Pair iPhone** or close
Willow. If Tailscale is not connected yet, or the link stops working, Willow
tries again after 30 seconds, then waits twice as long each time, up to five
minutes. Nothing runs between attempts, and while it waits for a phone, the
server only listens for connections. **Games** on the phone
releases control and returns to the game list. The game keeps running on the
PC. Starting mobile access again creates a
new pairing code. Anyone with the current link can pair, so keep the QR code
private.

## Settings

Tap **Settings** at the top of the game list or the game screen. It opens a
full page; tap **Done** or swipe back to return. A game keeps running on the PC
while Settings is open.

- **Game → Game speed** chooses **1×**, **2×**, **4×**, **8×**, or **Maximum** for
  manual play and Auto Play. It works once a game is open. **PC speed** follows
  Willow’s desktop settings. Desktop fast-forward controls take priority when
  active. Willow returns to **PC speed** when the phone disconnects or returns
  to **Games**.
- **Auto Play → Model** chooses the model for the next Auto Play run. **Pi ·**
  models use the Pi agent harness with your Codex sign-in.
  The list comes from the PC, so it fills in the first time a game opens.
- **Video → Video stream** picks the stream format (see below), and shows the
  codec and encoder in use.
- **Video → Screen scaling** chooses **Sharp pixels** (the default) or
  **Smooth**.
- **Controls → Button size** chooses **Standard** or **Large** buttons.
- **Controls → Keep screen awake** stops the phone from dimming while a game
  is open, for example while you watch Auto Play. It needs iOS 16.4 or newer.
- **Connection** shows the PC the phone is connected to. **Unpair this phone**
  forgets the pairing link. To play again, scan the QR code on the PC.

The phone remembers the Auto Play model, video, screen, button and
screen-awake choices.

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
