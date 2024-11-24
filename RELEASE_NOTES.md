# Release Notes

## 2.64

Some tweaks for better integration with https://bsky.brid.gy (the BlueSky bridge by brid.gy).

A corner case bug in the media proxying code has been fixed.

Hashtags can now include underscores.

The server now creates a pidfile inside the data directory.

Mastodon API: fixed a crash in the notification code, fixed autocapitalization in the OAuth login field (contributed by fkooman).

## 2.63

The server can now act as a proxy for all image, audio or video media coming from other account's posts (both from the Web UI and the Mastodon API). This way, other servers will see media requests coming from the server IP, not the user's, improving privacy. This is controlled by setting the `proxy_media` boolean field to `server.json` to true.

The `strict_public_timelines` option introduced in the previous release now works correctly.

Fixed a crash when posting from the links browser.

Fixed some repeated images from Lemmy posts.

Fixed a crash when posting an image from the tooot mobile app.

Updated FreeBSD rc script: the server process is now managed by the daemon(8) utility (contributed by @stefano@bsd.cafe).

RSS feeds are now in 2.0 version instead of 0.91.

## 2.62

Added a new user option to collapse top level threads by default.

Added a new `disable_block_notifications` boolean field to `server.json` to disable the notifications of Block activities.

Added a new `strict_public_timelines` boolean field to `server.json` to only show an account's posts and boosts (no trees) in public timelines.

Fixed repeated images in posts from some implementations (those that include an image both as an attachment and as an `<img>` tag inside the post content).

Added a small HTML/CSS tweak to improve post previews from some implementations (contributed by nyanide).

Notifications for `EmojiReact` activities now show the emoji.

New command-line action `insert`, to insert a post by its URL in a user's timeline.

Fixed bad processing/rendering of URLs with two or more @ symbols.

## 2.61

Migration from a Mastodon account to a snac one has been implemented. Please see `snac(8)` (the administrator manual) for more information.

Mastodon API: fixed `timelines/public` (it was showing less posts that it should).

Fixed over-zealous caching to the public timeline.

Some HTML tweaks and fixes to the web interface.

## 2.60

Migration from a snac account to a Mastodon one has been implemented. Please see `snac(8)` (the administrator manual) for more information.

Fixed a bug in argument parsing regarding some special characters (contributed by ndagestad).

Fixed image emojis in the public profile bio.

## 2.59

Added post drafts: new or edit post edition dialogs include a checkbox that will cause the message to not be posted, but stored in a list of drafts, directly accesible from the private timeline. From that list, drafts can be edited and definitely posted (or re-stored as a draft for later editing).

Fixed a bug that caused `Block` notifications to not be shown properly.

Fixed crash in a corner case of the URL parameter parsing code.

## 2.58

Search by content also looks into alt-text from attachments (images, videos, etc.).

Search by content is case insensitive for all Unicode-supported alphabets (previously was only for ASCII characters).

Added support for bookmarking posts.

Fixed a failed compilation under Solaris/Illumos.

The actor `Person` object now includes a `featured` property, that returns the list of pinned posts.

The delete post button can now be hidden server-wise (it's not very useful and clutters the space).

The lists of pinned and bookmarked posts are directly accesible from the private timeline.

## 2.57

Added support for listening on UNIX-like sockets.

Don't allow empty polls.

Added support for Markdown-style images (converted to attachments) and strikethrough text.

Added support for incoming `Move` messages: when received, the new account is followed and the old one is unfollowed.

Incoming `EmojiReact` messages are internally converted to `Like` (more proper support will happen eventually).

Blocks are also notified.

Mastodon API: fixed a cache bug when following local users.

## 2.56

Don't show instance announcements in public timelines (contributed by sarahpw).

Added auto boosting. If an account has this flag enabled, all mentions to that account are automatically boosted to all its followers (contributed by sarahpw).

Fixed typos in manpages (contributed by sergiodj).

Some minor performance improvements (contributed by Claire).

## 2.55

Mastodon API: fixed a last-minute bug that broke some API functions.

## 2.54

Markdown-style links are now supported.

The alt text in attachments that have one is also shown in a dropdown just below it (useful for environments where mouseover is not available, i.e. for phones and tablets).

Instance-wide notifications has been implemented (contributed by louis77). A small tweak to the default CSS was made to suit this change; please consider updating your style.css (see doc/style.css as an example).

The avatar and/or the header images can now be deleted (contributed by louis77).

Code cleaning: HTTP status codes use names instead of hardcoded integers (contributed by louis77).

Mastodon API: fixed login problems with the official Mastodon API, IceCube and Toot! on iOS, some fixes for Mona and Tokodon apps, user credentials can now be edited from apps (all contributed by louis77), fixed crash on unset content-type header.

The webfinger content-type response header is now RFC-compliant (contributed by steve-bate).

Improved support for the HTTP OPTIONS method, allowing sites like https://mastodonlistmanager.org to work.

Fixed non-working content warnings in the public instance timeline.

## 2.53

New user feature to search by post content (using regular expressions) or tag.

Added some (partial) support for `Event` object types.

Minor fixes: Allow unboosting your own posts (contributed by khm), CSS fixes for the Dillo browser (contributed by kvibber).

## 2.52

Posts that were liked or boosted can now be unliked and unboosted.

Outgoing message timeouts are no longer hardcoded and can be configured (see `snac(8)` for more information).

Fixed a bug that caused some incorrect unfollows under special conditions (with shared inboxes enabled and users from the same instance that follow each other, the internal message distributor was confused).

Mastodon API: Added support for lists.

Added a header to avoid over-zealous caching in some browsers (contributed by louis77).

Added support for running and federating inside hidden networks like Tor, I2P or Loki (contributed by iwojima).

Fixed an error processing polls coming from Pleroma instances.

## 2.51

Support for custom Emojis has been added; they are no longer hardcoded, but read from the `emojis.json` file at the server base directory. Also, they are no longer limited to string substitutions, but images as external URLs are also supported (see `snac(8)` for more information).

Fixed a bug that caused some notifications to be lost when coming from a user in the same instance.

Added an additional check for blocked instances (sometimes, posts from blocked sites that were ancestors of legit posts were 'leaking' into the timeline).

On OpenBSD, if the `disable_email_notifications` server flag is set to `true`, `unveil()` is not called for the execution of the `/usr/sbin/sendmail` binary and `pledge()` doesn't set the `exec` promise.

## 2.50

Incoming posts can now be filtered out by content using regular expressions on a server level (these regexes are written in the `filter_reject.txt` file at the server base directory; see `snac(5)` and `snac(8)`).

Improved page position after hitting the `Hide` or `MUTE` buttons (for most cases).

Use a shorter maximum conversation thread level (also, this maximum value is now configurable at compilation level with the `MAX_CONVERSATION_LEVELS` define).

Fixed a bug where editing a post made the attached media or video to be lost.

The way of refreshing remote actor data has been improved.

Posting from the command-line now allows attachments.

Added defines for time to enable MacOS builds (contributed by andypiper).

## 2.49

Mastodon API: Fixed a bug in how validated links are reported.

Mastodon API: Fixed a bug in search by account.

Mastodon API: Fixed missing `Video` type objects in timelines.

Mastodon API: Added search by hashtag.

Some cosmetic fixes to the web interface.

## 2.48

A new instance page, that shows all posts by users in the same instance (like the public instance timeline, but interactive). This will help building communities.

Follower-only replies to unknown users are not shown in timelines.

Added verification of metadata links: if the linked page contains a link back to the snac user with a rel="me" attribute, it's marked as verified.

Added a new server-level configuration parameter: `min_account_age`. If this value (in seconds) is set in `server.json`, any activity coming from accounts that were created newer than that will be discarded. This can be used to mitigate spam.

Added a profile-page relation to links in webfinger responses (contributed by khm).

Fixed some regressions and a crash.

## 2.47

Added pagination to the notification page.

The *New Post...* option now includes an optional field to set the URL of an ActivityPub post to be a reply to.

Fixed spurious notifications from the same user.

Fixed repeated mentions in the reply text field.

One-post only pages include the post content instead of the user bio in their metadata (improving post previews from other software).

Mastodon API: Added support for timelines by tag (for sites like e.g. https://fediwall.social to work).

## 2.46

Added support for Peertube videos.

Mastodon API: Tweaks to support the Subway Tooter app (contributed by pswilde), added support for editing posts, fixed an error related to the edit date of a post, fixed some crashes.

Added a handshake emoji next to a user name if it's a mutual relation (follower and followed), because friendship is bliss.

Tweaked some retry timeout values for better behaviour in larger instances (thanks to me@mysmallinstance.homelinux.org for their help).

## 2.45

Fixed a collision in webfinger caching. This may affect federation with some software, so I recommend an upgrade.

Fixed crashes in some command-line options.

New command-line option `state`, that dumps some information about the running server and the state of each thread (note: this feature uses shared memory blocks and you may need an argument to the `make` call in older Linux distributions; please see the `README` file for details).

Fixed a bug that may leave an inconsistent state for a followed actor in a special case of repeated messages.

Mastodon API: added some fixes for integration with the Mona iOS app (contributed by jamesoff).

Added support for ntfy notifications, both using a self-hosted server or the official ntfy.sh (contributed by Stefano Marinelli).

## 2.44

Fixed a nasty bug that caused the in-memory output queue to be corrupted under heavy traffic loads. This is a good reason to upgrade (thanks to Víctor Moral and Stefano Marinelli for helping me in fixing this).

Shared inboxes are now supported. This is not a user visible feature (hopefully, they will not feel any change), but it will significantly improve traffic for snac instances with many users and will open room for new features that are only feasible with these kind of input channels (this is not enabled by default; see `snac(8)`).

I've refactored all HTML code because it was somewhat of a mess; now it's much more maintainable (at least for me). I think I haven't broken anything.

Fixed crash in a special case of malformed query.

Mastodon API: some tweaks for better integration with more clients, and fixed a crash when processing boosts from kbin.

Fixed crash in the FastCGI code (thanks to Yonle for helping me debug this).

Added apache2 configuration information (contributed by Víctor Moral).

Added FreeBSD and NetBSD setup information and examples (contributed by draga79).

## 2.43

Tags in posts are now indexed and searching by tag is possible. It's still not very useful, but at least new user posts contain links that return real search-by-tag results. Also, no "re-indexing" of the previously stored data is done (only new local and remote posts will be searchable by tag).

Added FastCGI support. This allows for simpler setups (e.g. on #OpenBSD, it's no longer necessary to enable and configure `relayd`, as the native `httpd` daemon includes FastCGI support).

History can be disabled by administrators by setting the `disable_history` field to true.

New command-line option `deluser`, to delete a user (including unfollowing everybody).

Mastodon API: account lookups has been implemented.

Fixed a minor memory leak :facepalm:

Fixed some HTML errors :facepalm:

## 2.42

It's now possible to add profile metadata.

Accounts can be marked as 'private', so that they are not accesible from any non-authorized web UI (i.e. only through the Fediverse).

Outgoing connections that fail with a timeout are retried with a higher timeout limit. But, if the instance keeps timing out, it's penalized by skipping one retry.

If a post comes from a group or community (i.e. it has an `audience` field set), the buttons `Follow Group` or `Unfollow Group` are shown.

Pinned posts are never (incorrectly) purged.

Some RSS validation fixes.

Mastodon API: some tweaks to better match Mastodon behaviour in timeline entries and boosted posts are correctly returned, and some fixes for crashes.

Don't allow creating users which user name strings only differ in case. This was creating some problems (e.g. the webfinger interface doesn't allow case sensitivity).

## 2.41

Added support for `Article` ActivityPub objects to correctly process reviews from #Bookwyrm instances and other similar software.

Show `Link` objects that are image, audio or video media as if they were attachments.

Added more aggressive caching to timelines and history files.

The history at the end of the public page is listed in reverse chronological order.

Mastodon API: minor fixes.

Be more strict when serving note objects.

Additional HTTP response headers can be defined by adding an `http_headers` object with header/content pairs into the `server.json` configuration file.

If you hover the mouse pointer over a vote option in a pool, the current count (as per the last update) is shown. This may be considered cheating in some cases, so try to be honest (or not).

The `nodeinfo` URL now returns more accurate information.

## 2.40

Announces (boosts) can now be disabled/reenabled on a per-people basis (to limit those boost-trigger-happy friends from flooding your timeline). This is operated from the people page.

It's now possible to show an instance timeline instead of the classic greeting static page at the base URL. This can be enabled by setting the `show_instance_timeline` field to `true` in the server configuration file. Other metadata fields can be set to add information to this page; please see `snac(8)` (the Administrator manual page) for details.

Users can now upload an image (preferably in landscape mode) that will be shown as a banner in their public pages.

Added hints to web UI buttons.

Mastodon API: some minor fixes to allow the semaphore.social web client to work.

Fixed miscalculation of the `votersCount` field in multiple-choice polls.

DNS or TLS errors in outgoing connections are considered fatal errors and not retried.

## 2.39

Added support for Follow confirmation messages that only return the follow request id (this fixes following Guppe channels).

Fixed some Gotosocial ActivityPub compatibility (details: support fields like `tag` and `attachment` being single objects instead of arrays).

Fixed ActivityPub outbox (it listed 'Note' objects instead of 'Create' activities).

Show the 'audience' field (channel URL) if a post has one (like pages from lemmy channels and other forums).

Some web UI tweaks: the new post field is hidden by default (wasting less screen space), added a 'back to top' link at the bottom of the page and other minor tuning to the HTML and default CSS (contributed by yonle).

Fixed RSS (contributed by yonle).

Fixed interactive text processes (like instance or user creating) by calling `fflush()` after printing text prompts (it was broken on systems that use musl like Alpine Linux).

## 2.38

More vulnerability fixes (contributed by yonle).

Added support for ActivityPub `Group` and `Page` objects. This allows subscribing to Lemmy channels and interacting with them.

Confirmation messages to follow requests are sent as soon as possible (i.e. bypassing the usual output queue).

Fixed crash when accessing some static or history objects.

## 2.37

Fixed several XSS vulnerabilities (contributed by yonle).

Fixed a bug that prevented working in systemd restricted environments (details: `mkdir()` failed if trying to set the setgid bit).

## 2.36

Added proper HTTP caching when serving static files (attached images, avatars, etc.)

Sensitive content messages can now have a summary.

Added a way to block full instances (from the command-line tool, as I consider this to be an administration priviledge).

If the user style.css does not exist, the server-wide one if served instead.

Added support for pinning user posts. These posts are shown first in the public AND in the private timelines.

Fixed some crashes on badly-formatted input messages.

The image alt-text is also inserted in the 'title' attribute, so it's shown on mouse hover in browsers like Firefox (contributed by Haijo7).

Some improvements to HTML sanitization.

For polls, show the time left before it closes.

Fixed a bug that prevented unfollows to be shown in the notification area.

## 2.35

Fixed broken URL links with the # symbol on them.

Fixed people-being-followed data loss after not logging in for a long time (actor objects were purged too soon).

Fixed bug that made impossible to vote on polls that were replied to.

Don't show 'ghost' no-longer-followers in the people list.

When sanitizing HTML input, unsupported tags are deleted instead of escaped.

Fixed crash on missing headers while checking signatures.

Mastodon API: several bug fixes (contributed by Poesty), improved account object (contributed by Haijo7).

There is new a user configuration setup checkbox to mark an account as a bot.

## 2.34

Polls can now be created from the web interface.

New Mastodon API features: polls are shown and can be voted on.

The user@host identifier is now shown next to the user avatar (contributed by Haijo7). A small tweak to the default CSS was made to suit this change; please consider updating your style.css.

Clicking on an image attachment opens it in a new tab (contributed by Haijo7).

Bots are marked as such using an emoji (contributed by Haijo7).

## 2.33

Polls (in #ActivityPub lingo, Questions) are shown and can be voted on. Creating a poll is not yet implemented, though.

If an URL written in a post links to an image, it's converted to an attachment.

Fixed a bug in the semaphore code that caused total hangs on OpenBSD.

## 2.32

New user configuration switch to drop direct messages from people you don't follow, in order to mitigate spam from Mastodon open registration instances.

When updating user information (e.g. the display name or the bio), the changes are also sent to the people being followed (so they have an updated description about who the user is).

Mastodon API: account search has been implemented, so mention completion works from apps; new support for blocking users (this operation is called MUTE here).

## 2.31

Fixed webfinger and curl issues, which crashed snac while trying to follow Mitra users (contributed by Poesty).

Use named semaphores instead of anonymous ones, to make it work under MacOS (contributed by Saagar Jha).

Fixed semaphore name conflicts (contributed by Poesty).

Fix warning in ugly pointer substraction (contributed by Saagar Jha).

Added user-agent to webfinger queries (contributed by Poesty).

Mastodon API: implemented notification type filters, fixed (partially) the issue with mentions without server.

## 2.30

Fixed a bug that made some notifications to be missed.

New Mastodon API features: the instance public timeline is now a real one, unfavourite / unreblog is supported (somewhat). Some regression bugs regarding image posting were also fixed.

The non-standard `Ping` and `Pong` #ActivityPub activities have been implemented as proposed by @tedu@honk.tedunangst.com in the https://humungus.tedunangst.com/r/honk/v/tip/f/docs/ping.txt document (with a minor diversion: retries are managed in the same way as the rest of #snac messages).

The build process now includes the `-Wextra` flag.

## 2.29

New Mastodon API features: account search, relationships (so the Follow/Unfollow buttons now appear for each account), follow and unfollow accounts, an instance-level timeline (very kludgy), custom emojis for accounts and statuses, many bug fixes (sadly, the Mastodon official app still does not work).

## 2.28

Added a new notification area to the web interface, accessible from a link at the top, that also shows the number of unseen events. This area lists all notifications in reverse chronological order and provides a button to clear all.

More work in the Mastodon API. The new supported features are: notifications, post of new and reply messages (including attached images). Some API v2 entry points had to be implemented, so you'll need to update your HTTPS proxy configuration again, see `snac(8)`. #Tusky no longer crashes, or so I think. The official app and close relatives like #Megalodon still don't work, though.

If you are not interested in this Mastodon API crap, you can compile it out of your #snac by defining the `NO_MASTODON_API` preprocessor directive and forget about it.

Fixed an HTML cache bug (it was not refreshed after editing a post).

## 2.27

Started Mastodon API support, so you can use Mastodon-compatible apps to access #snac accounts. What works so far: login, private and public timelines, full post information (replies and ancestors), liking and boosting posts. Things that don't work yet but eventually will: following accounts, posting and replying to messages (I still have to figure out how some things work, like posting images), notifications (needs some internal support), the instance timeline (snac does not have one, but it can be simulated with not much effort) and probably many other things. Things that will never work: bookmarks, pinning, a federated timeline, many other things that I don't remember right now. Please note that if you want to use this API in your instance, you must add some lines to your HTTP proxy configuration, see the `snac(8)` (administrator documentation) manual page. I'm doing my tests using the #Tusky (which sometimes crashes, surely my fault), #AndStatus, #Fedilab and #Husky Android apps. Success or failure reports will be appreciated.

Fixed some buffer overflows (contributed by Saagar Jha).

Fixed overzealous rejection of some local boosts.

## 2.26

The OpenSSL code has been refactored to avoid using deprecated functions.

Added more aggressive filtering on unwanted `Announce` (boost) messages.

## 2.25

Federation with other instances have been improved by collecting shared inbox information from input messages more thoroughly.

Fixed an obscure bug that caused connection rejections from some instances.

Some rules regarding incoming messages have been tightened; messages that are not related to the user are not added to the timeline. This has to be implemented because some ill-behaving ActivityPub implementations were found injecting unwanted messages to user inboxes.

Messages from MUTEd users are rejected as soon as possible with a 403 Forbidden HTTP status.

Fixed a minor bug regarding the scope of the 'Update' activity (edited posts were sent to more recipients that it should).

More aggressive input sanitization (some posts were found that included strange ASCII control codes).

Added "Open Graph" HTML meta tags for better previsualization of `snac` links from other social media.

## 2.24

Sending non-public messages is now much easier: a checkbox to post a message to only those people mentioned in the message body has been added.

Fixed an over-optimization bug that caused some mentioned recipients to be skipped.

Added some new administrator tweaks: email notifications can be globally disabled.

## 2.23

The user avatar can now be uploaded (instead of given as an URL).

Minor speed improvements int output messages.

Minor improvements in error logging.

## 2.22

Fixed a bug with the Delete button in the web interface: sometimes, instead of the post (as the user intended), the follower was deleted instead :facepalm:

Fixed a bug in the command-line option `follow` (and probably others), that made it fail silently if there was no running server.

Fixed a crash under OpenBSD (a recent change needed a new permission to the `pledge()` call that was forgotten).

## 2.21

Users can now specify an expire time for the entries in their timelines (both their own and others').

Added support for sending notifications (replies, follows, likes, etc.) via Telegram.

Followers can now be deleted (from the *people* page in the web interface). Yes, to stop sending in vain your valuable and acute posts to those accounts that disappeared long ago and flood the log with connection errors.

The internal way of processing connections have been rewritten to be more efficient or, as technical people say, "scalable". This way, `snac` is much faster in processing outgoing connections and less prone to choke on an avalanche of incoming messages. This is a big step towards the secret and real purpose of the creation of this software: being able to host the account of #StephenKing when he finally leaves that other site.

The `note` action from the command-line tool can also accept the post content from the standard input.

## 2.20

Image attachments in posts can now have a description (a.k.a. "alt text").

## 2.19

You can edit your own posts from now on.

Fixed the breakage of Emojis I introduced when implementing HashTags because I am a moron.

Added adaptative timeouts when sending messages to other instances.

## 2.18

Added support for #HashTags (they are not internally indexed yet, only propagated to other instances).

Added support for OpenBSD enhanced security functions `unveil()` and `pledge()` (contributed by alderwick).

The purge ttl for stray global objects has been shortened.

In the HTML interface, don't show the collapse widget for non-existent children.

Added support for HTTP signature pseudo-headers `(created)` and `(expires)`, that are used by some ActivityPub implementations (e.g. Lemmy).

When replying, the mentioned people inherited from the original post will be clearly labelled with a CC: prefix string instead of just being dropped out there like noise like Mastodon and others do. (I hope) this will help you realise that you are involving other people in the conversation.

## 2.17

Fixed a header bug (contributed by alderwick).

Fixed a bug in the Boost by URL option when the URL of the boosted message is not the canonical id for the message (e.g. Mastodon's host/@user/NNN vs. host/users/user/statuses/NNN).

Fixed crash on a corner case regarding failed webfinger requests.

## 2.16

Some outgoing connection tweaks: the protocol is no longer forced to be HTTP/1.1 and timeouts are less restrictive. This has proven useful for some unreachable instances.

Conversations can be collapsed.

Added support for edited posts on input. These updated messages will be marked by two dates (creation and last update).

Some tweaks to the docker configuration to generate a smaller image (contributed by ogarcia).

## 2.15

Fixed bug in message posting that may result in 400 Bad Request errors (contributed by tobyjaffey).

Fixed crash and a deletion error in the unfollow code.

Added configuration files and examples for running snac with docker (contributed by tobyjaffey).

Serve /robots.txt (contributed by kensanata).

## 2.14

Previous posts in the public and private timelines can be reached by a "More..." post at the end (contributed by kensanata).

Clicking the 'Like' and 'Boost' buttons don't move the full conversation up; after that, the page is reloaded to a more precise position. Still not perfect, but on the way.

New command-line operation, `resetpwd`, to reset a user's password to a new, random one.

Added a user setup option to toggle if sensitive content is shown or not by default (contributed by kensanata).

All images are loaded in lazy mode for a snappier feel (contributed by kensanata).

Fixed crash in the data storage upgrade process when debug level >= 2 (contributed by kensanata).

Log message improvements for excelence (contributed by kensanata).

The logging of "new 'Delete'..." messages has been moved to debug level 1, because I'm fed up of seeing my logs swamped with needless cruft.

Don't show the 'Boost' button for private messages.

Added (partial) support for /.well-known/nodeinfo site information. This is not mandatory at all, but if you want to serve it, remember that you need to proxy this address from your web server to the `snac` server.

Some internal structure improvements.

## 2.13

A big disk layout rework, to make it more efficient when timelines get very big. Please take note that you must run `snac upgrade` when you install this version over an already existing one.

Fixed HTML loose close tag (contributed by kensanata).

Fixed bug when closing sendmail pipe (contributed by jpgarcia and themusicgod1).

## 2.12

Fixed some bugs triggered when a GET query does not have an `Accept:` header.

RSS feeds are a bit less ugly.

## 2.11

Marking posts and replies as sensitive content is now possible.

On output, shared inboxes are used on instances that support it. This reduces bandwidth usage greatly, specially for very popular users with thousands of followers.

Added RSS output support for the user posts (by adding .rss to the actor URL or by requesting the actor with an Accept HTTP header of `text/xml` or `application/rss+xml`).

Much more aggresive HTML sanitization.

## 2.10

The local timeline purge has been implemented. The maximum days to live can be configured by setting the field `local_purge_days` in the server configuration file. By default it's 0 (no purging).

Some memory usage fixes.

More formatting fixes.

The only JavaScript snippet has been replaced by pure HTML, so `snac` is now purely JavaScript-free.

## 2.09

New button `Hide`, to hide a post and its children.

You can boost your own posts indefinitely, because Social Media is about repeating yourself until the others care about you.

Fixed bug in the webfinger query (uid queries should no longer fail).

Updated documentation.

## 2.08

A new page showing the list of people being followed and that follows us has been implemented, with all operations associated (including sending Direct Messages).

Messages marked as sensitive are now hidden behind a dropdown.

More aggressive HTML sanitization.

Fixed possible crash when following somebody by its @user@host.

Some fixes to formatting in post creation.

Fixed a small memory leak.

After a like or boost, the scrolling position is set to the next entry in the timeline and not to the top of the page. This is still somewhat confusing so it may change again in the future.

## 2.07

Fixed some minor memory leaks.

Mail notifications are sent through the queue (and retried if failed).

## 2.06

Fixed a nasty bug in newly created users.

The purge is managed internally, so there is no longer a need to add a cron job for that. If you don't want any timeline data to be purged, set `timeline_purge_days` to 0.

The :shortnames: or Emoji or whatever the fuck these things are called are now shown in actor names and post contents.

Added optional email notifications.

## 2.05

Image upload is now supported (one image per post).

Added support for HEAD methods (who knew it was necessary?).

Fixed bug in greeting.html completion (%host% was not being replaced).

Referrers (actors that like or announce posts) are not overwritten.

## 2.04

More multithreading.

Image URLs can also be attached to reply messages.

Improved default mentions in reply text fields.

The static directory now works.

New command line `unfollow`.

## 2.03

Notes can now attach images and other media. The web interface still limits this feature to only one attachment (given by URL, so the file must have been uploaded elsewhere).

Videos attached to notes are now shown.

A small set of ASCII emoticons are translated to emojis.

The new (optional) server configuration directive, `disable_cache`, disables the caching of timeline HTML output if set to `true` (useful only for debugging, don't use it otherwise).

## 2.02

Fixed a bug that caused empty actor names look like crap.

Fixed a severe bug in number parsing and storage.

Children entries are not indented further than 4 levels, so that fucking Twitter-like threads are readable.

Added some asserts for out-of-memory situations.

The publication timestamp only shows the date, so timezone configuration and conversion is no longer an issue.

The reply textareas are pre-filled with the user ids mentioned in the message.

Fixed bug in the web command `Boost (by URL)`.

New command-line option `purge`.

Fixed a null-termination bug in Basic Authentication parsing.

Updated documentation.

New systemd and OpenBSD startup script examples in the examples/ directory.

## 2.01

This is the first public release of the 2.x branch.
