# Patch 55 — two of the three fixed, one needs a sample line

## Fixed: the mojibake in pin labels

Visible in the screenshots: `CUR_SIG_ASPECT · slrp Â· 81_1`. The first middle
dot renders, the second does not.

    QStringLiteral("%1 · %2").arg(p.field, narrowing.join(QLatin1String(" · ")))
                        ^ fine                            ^ broken

The source file is UTF-8, so `·` is two bytes (C2 B7). `QStringLiteral`
decodes them as UTF-8 and gets one character; `QLatin1String` takes them as
two Latin-1 characters and gets `Â·`. Same dot, same line, two constructors.

Both are `QStringLiteral` now. A scan of the whole codebase for non-ASCII
passed through `QLatin1String` or `QLatin1Char` found this one instance and
no others.

## Fixed: the session key dialog reads one loco at a time

`SessionKeyStore` has been per-loco all along — `locos()` lists them and every
accessor takes a `locoId` — but the dialog called them all with the default,
which means "whichever loco is active". On a two-loco capture you got
whichever spoke last, with no way to say otherwise.

The live section now has a **loco selector**: "active loco" (the old
behaviour, still the default) plus one entry per loco the store has seen. It
drives both the displayed session key and what "Load into form" fills in.

The whole load comes from **one** loco. Key sets and randoms are per loco, and
mixing one loco's set with another's random derives a key that never existed
on either link — which is the kind of wrong answer that looks plausible.

The list rebuilds when the store changes but keeps the operator's choice if
that loco is still present. Rebuilding blindly would snap the selection back
to the first loco every time a frame arrived, which on a live capture is
several times a second.

## Not fixed yet: empty auth-key and random tabs

Traced, not guessed. `LocoConsoleWindow` keys every state by
`CaptureLine::key()`, which is `<locoId>_<ctrlId>` taken from the last two
underscore-separated parts of the tag. A type tab shows
`st.latest[type]` for the **selected** key, and prints "(no data yet)" when
that key has no frame of that type.

So a tab is empty whenever the auth-key or random lines carry a different
loco/ctrl id from the loco being viewed — and the code comment beside the tab
list says these come from the LCU, which is exactly the thing likely to tag
them with its own id. They would then appear under their own entry in the loco
selector rather than under 81_1.

The other possibility is that `parseLine` rejects them outright. It requires
the tag to have at least three underscore-separated parts and both trailing
parts to be numbers, so `@authkeys` or `@auth_keys 2026-...` without the
`_<loco>_<ctrl>` suffix is dropped before anything else runs.

Both are one-line fixes but they are different one-line fixes, and guessing
would mean changing routing for every capture type. **One raw auth-key line
and one random-number line from the log settles it.**

## Files touched

    pinpanel.cpp                the separator
    sessionkeydialog.{h,cpp}    per-loco live section

## Verification

    119 suites, 3031 checks, 0 failed
    headless smoke run clean
