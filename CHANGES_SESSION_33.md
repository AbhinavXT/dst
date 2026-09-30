# Session 33 — Find in a compare pane

| File | Change |
|---|---|
| `findbar.{h,cpp}` | the bar follows the view's current model; `refreshModelBinding()` |
| `comparewindow.cpp` | tells its find bar when a pane is rebound |
| `tests/test_comparefind.cpp` | new: 11 checks |

Reported: *the console crashes as soon as I write anything in the find field
in a compare tab.*

---

# The crash

It was real, it was in patch 30, and the patch 31 rewrite of `scanMatches()`
had already removed it — so this session is the test that proves it and stops
it coming back, plus a second fault the reproduction turned up.

The old scan:

```cpp
auto *sortProxy = qobject_cast<QSortFilterProxyModel*>(proxy);
const LogModel *lm = sortProxy ? qobject_cast<const LogModel*>(sortProxy->sourceModel())
                               : qobject_cast<const LogModel*>(proxy);   // bound directly
...
} else if (lm && colSel == LogModel::ColMessage) {
    const QModelIndex sIdx = sortProxy->mapToSource(sortProxy->index(r, 0));
```

The `lm` line handles a view with no proxy. The line that uses it does not.
A **compare pane binds its LogModel straight to the view** — that is why the
window exists in the shape it does, and half a dozen places cast
`view->model()` to `LogModel` — so `sortProxy` is null, `lm` is not, and the
default column is Message. Every branch that could run dereferenced null.
First keystroke, every time. The query branch had the same line.

The rewrite in patch 31 routed both through `entryAtProxyRow()`, which was
already handling proxy and no-proxy because the compare window needed that
when its find bars were added. The crash went with it — silently, which is
its own small lesson: a fix nobody knew they were making is a fix nobody
wrote a test for.

There is a test now. It builds a real `CompareWindow`, binds a real source,
types, and drives every search mode against a model with no proxy. Reaching
the end of the suite is most of the check — a crash takes the process, not
the assertion.

---

# What the reproduction turned up

A compare pane's view is built **empty**. It is given a model when a source is
picked, and a different one every time the operator changes that pick. The
find bar wires its model-change connections in its constructor, which for a
log tab is fine — the tab's model exists first — and for a compare pane means
wiring to nothing at all.

Two consequences, both live until this patch:

- **Find never re-scanned.** New traffic arriving in the pane did not reach
  the bar, so the count sat at whatever it first reported while the log grew
  underneath it. Retyping the query was the only way to refresh it.
- **A source switch left the old matches in place.** Match positions are row
  numbers, and a row number means something different once the view is showing
  another model. Navigating them took the operator to unrelated messages.

`ensureModelWiring()` re-points the connections at whatever the view shows now
and drops the match set when the model underneath actually changed. It is a
pointer comparison, so it is called on every scan and on `activate()` without
being worth thinking about.

## The one thing Qt would not tell us

`QAbstractItemView::setModel()` emits nothing. A widget cannot notice that the
view it watches has been handed a different model, and polling for it would
mean a timer running forever against something that changes a handful of times
a session.

So the compare window says so: `rebindPane()` already re-wires the scroll-bar
and selection connections after `setModel()`, since the selection model is
invalidated there, and it now tells the find bar too. One call, in the one
place in the program that swaps a model under a view that is already on
screen.

---

# Tests

`comparefind`, 11 checks: that a pane binds a LogModel with no proxy at all,
that typing into it finds what is there, that all five modes survive it —
including Hex reading `rawBytes` and a query evaluating fields, the two that
need the entry rather than a rendered cell — that a message arriving is picked
up without retyping, and that switching the pane's source empties the match
set rather than carrying row numbers across.

Full suite: **100 suites, 2574 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.

---

# Also this session

Received LSRP: **dropped**, on your word that it does not arrive. The decode
path still handles it if it ever does — the header size comes from the frame
either way — but nothing has been built on the assumption that it will.

---

# The Packet Maker's two ARP entries

Left open at the end of patch 32: whether a built `arprecv` should carry the
8-byte received shape and a built `arp` the 10-byte transmitted one, since
today both come out identical.

The answer is in what the program is. **DLConsole is always the peer
transmitting to the loco**, so what it sends is by definition what the loco
receives — and the received form is the 8-byte header it already builds, with
the default `src=7 dest=2` that the real receive buffer shows as `07 02`. The
10-byte form with the station id belongs to a loco's own radio, which this
program does not emit. So there is nothing to change: both entries are
correct, and identical for a reason.

What was missing is that nothing said so. Two combo entries producing
byte-identical dumps reads as a bug, and the way that gets resolved is an
operator diffing two hex dumps to find out. `MessageHeader::shapeNote()` now
says it once, in the preview, under the header line:

> this is the RECEIVED form: 8-byte header, no station id. A loco's own
> transmission carries a 2-byte station id and a 10-byte header, which
> DLConsole does not send.

The test pins the identity rather than a difference, with the reason attached,
so that nobody later "fixes" it into putting a station id on a frame the loco
is meant to receive without one.

Full suite: **100 suites, 2582 checks, 0 failed.**
