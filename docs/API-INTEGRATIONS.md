# Streaming platform integrations

HeartCast only uses each platform's **official** public API. Nothing is scraped, and users never type
their social-media password into the app. Where a platform does not allow third-party apps to do
something, HeartCast says so and uses the supported alternative: Stream URL + Stream Key.

## Developer credentials: never compiled into the app
OAuth **Client IDs** are read at runtime from, in order:
1. Environment variables `HEARTCAST_TWITCH_CLIENT_ID`, `HEARTCAST_GOOGLE_CLIENT_ID`, `HEARTCAST_GOOGLE_CLIENT_SECRET`
2. `%APPDATA%\HeartCast\developer.json`:
```json
{ "twitchClientId": "…", "googleClientId": "…apps.googleusercontent.com", "googleClientSecret": "…" }
```
Without credentials, the Connect button reports that a Client ID is needed, and manual keys still work.

## Twitch: implemented, needs a Client ID
1. Go to <https://dev.twitch.tv/console/apps> → *Register Your Application*.
2. **Client Type: Public**. OAuth Redirect URL: `http://localhost` (unused by the device flow, but required).
3. Copy the Client ID. No client secret is needed: HeartCast uses the **Device Code Grant**. The user
   opens twitch.tv/activate, enters the code shown in HeartCast and approves.

| Feature | API | Scope |
|---|---|---|
| Fetch stream key | `GET /helix/streams/key` | `channel:read:stream_key` |
| Title / category | `GET /helix/search/categories`, `PATCH /helix/channels` | `channel:manage:broadcast` |
| Chat read/send | IRC over WebSocket `wss://irc-ws.chat.twitch.tv` | `chat:read`, `chat:edit` |
| Follows | EventSub WebSocket `channel.follow` v2 | `moderator:read:followers` |
| Subs, gifts, resubs | `channel.subscribe`, `channel.subscription.gift`, `channel.subscription.message` | `channel:read:subscriptions` |
| Cheers | `channel.cheer` | `bits:read` |
| Raids | `channel.raid` | none |
| Channel points | `channel.channel_points_custom_reward_redemption.add` | `channel:read:redemptions` |
| Delete / timeout / ban | `DELETE /helix/moderation/chat`, `POST /helix/moderation/bans` | `moderator:manage:chat_messages`, `moderator:manage:banned_users` |
| Slow / follower / sub-only | `PATCH /helix/chat/settings` | `moderator:manage:chat_settings` |
| Block user | `PUT /helix/users/blocks` | `user:manage:blocked_users` |
| Viewer count | `GET /helix/streams` | none |

Tokens refresh automatically and are revoked on Disconnect. They are stored with DPAPI.

## YouTube Live: implemented, needs a Google OAuth client
1. In <https://console.cloud.google.com>, create a project and enable **YouTube Data API v3**.
2. Set up the OAuth consent screen with scope `https://www.googleapis.com/auth/youtube`. While in
   "Testing", only listed test users can sign in. Publishing requires Google verification.
3. *Credentials → Create OAuth client ID → **Desktop app***. Google documents the desktop "client
   secret" as non-confidential. It is still read from config, not compiled in.
4. Sign-in uses the system browser, a loopback redirect to `127.0.0.1:<random port>`, PKCE (S256)
   and a state check.

| Feature | API |
|---|---|
| Channel | `channels.list mine=true` |
| Go live | `liveStreams.insert` (reusable, so the stream key stays stable) → `liveBroadcasts.insert` (auto start/stop) → `liveBroadcasts.bind` |
| Title / description / category | `videos.update` |
| Chat | `liveChatMessages.list` (honours `pollingIntervalMillis`), `liveChatMessages.insert` |
| Super Chat / Stickers / memberships | chat item types → alerts |
| Delete / timeout / ban | `liveChatMessages.delete`, `liveChatBans.insert` |
| Viewers | `videos.list part=liveStreamingDetails` |

Live streaming must be enabled on the channel (it can take 24 h the first time). API quota applies.

## Platforms that use Stream URL + Key (by design)
| Platform | Why | Where users find it |
|---|---|---|
| **Kick** | Public developer API is new and gated by approval; chat/events integration is planned once access is granted | Creator Dashboard → Settings → Stream URL & Key |
| **TikTok LIVE** | No public API for third-party apps to create LIVE sessions, read chat or fetch keys; key access is granted by TikTok per account | LIVE Center / LIVE Producer |
| **Facebook** | Live Video API requires a Meta app that has passed App Review | Live Producer → Streaming software → persistent key |
| **Instagram** | No public third-party Live API; keys are per session (professional accounts) | Live Producer |
| **X** | Requires Media Studio Producer | Media Studio → Producer → Sources |
| **Trovo** | Manual key supported | Trovo Studio → Stream settings |
| **Custom** | Any RTMP / RTMPS / SRT ingest | your server |

## Adding a platform
Add an entry to `resources/config/platforms.json` (manual platforms need no code). For API
platforms, subclass `PlatformAccount` (see `src/platforms/Twitch.*`) and register it in
`PlatformHub::init()`. Platform integrations can also ship as app plugins.
